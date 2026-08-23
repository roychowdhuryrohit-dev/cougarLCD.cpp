#include "hid_transport.hpp"

#include <hidapi/hidapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <climits>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace cougar {
namespace {

constexpr std::size_t report_bytes = 1025;
constexpr std::size_t media_header_bytes = 25;
constexpr std::size_t media_data_bytes = 1000;

std::string narrow(const wchar_t* value) {
    if (!value) return {};
    std::mbstate_t state{};
    const wchar_t* source = value;
    const std::size_t count = std::wcsrtombs(nullptr, &source, 0, &state);
    if (count == static_cast<std::size_t>(-1)) return "<unprintable>";
    std::string output(count, '\0');
    state = {};
    source = value;
    std::wcsrtombs(output.data(), &source, output.size(), &state);
    return output;
}

std::uint64_t unix_time_ms() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count());
}

bool append_escaped(std::uint8_t value, std::vector<std::uint8_t>& output,
                    std::size_t& position) {
    if (value == 0x5a || value == 0x5b) {
        if (position + 2 > output.size()) return false;
        output[position++] = 0x5b;
        output[position++] = value == 0x5a ? 0x01 : 0x02;
    } else {
        if (position + 1 > output.size()) return false;
        output[position++] = value;
    }
    return true;
}

bool build_report(const std::string& payload, std::vector<std::uint8_t>& report) {
    if (payload.size() + 5 > 0xffff || report.size() != report_bytes) return false;
    const auto frame_length = static_cast<std::uint16_t>(payload.size() + 5);
    const auto high = static_cast<std::uint8_t>(frame_length >> 8);
    const auto low = static_cast<std::uint8_t>(frame_length);
    std::uint8_t checksum = static_cast<std::uint8_t>(high + low);
    for (const unsigned char value : payload) {
        checksum = static_cast<std::uint8_t>(checksum + value);
    }

    std::fill(report.begin(), report.end(), 0);
    report[0] = 0; // Unnumbered HID report ID.
    std::size_t position = 1;
    report[position++] = 0x5a;
    if (!append_escaped(high, report, position) ||
        !append_escaped(low, report, position)) return false;
    for (const unsigned char value : payload) {
        if (!append_escaped(value, report, position)) return false;
    }
    if (!append_escaped(checksum, report, position) || position >= report.size()) {
        return false;
    }
    report[position++] = 0x5a;
    return true;
}

std::optional<std::string> decode_frame(const std::uint8_t* raw,
                                        std::size_t raw_length) {
    std::size_t start = 0;
    while (start < raw_length && raw[start] != 0x5a) ++start;
    if (start == raw_length) return std::nullopt;

    std::vector<std::uint8_t> frame;
    frame.reserve(raw_length);
    frame.push_back(0x5a);
    for (std::size_t i = start + 1; i < raw_length;) {
        std::uint8_t value = raw[i++];
        if (value == 0x5a) {
            frame.push_back(value);
            break;
        }
        if (value == 0x5b && i < raw_length &&
            (raw[i] == 0x01 || raw[i] == 0x02)) {
            value = raw[i++] == 0x01 ? 0x5a : 0x5b;
        }
        frame.push_back(value);
    }
    if (frame.size() < 5 || frame.back() != 0x5a) return std::nullopt;
    const auto declared = static_cast<std::uint16_t>((frame[1] << 8) | frame[2]);
    if (declared != frame.size()) return std::nullopt;
    std::uint8_t checksum = 0;
    for (std::size_t i = 1; i + 2 < frame.size(); ++i) {
        checksum = static_cast<std::uint8_t>(checksum + frame[i]);
    }
    if (checksum != frame[frame.size() - 2]) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(frame.data() + 3),
                       frame.size() - 5);
}

std::string basename(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

HidTransport::~HidTransport() {
    close();
}

void HidTransport::enumerate() {
    hid_device_info* head = hid_enumerate(vendor_id, product_id);
    unsigned count = 0;
    for (hid_device_info* item = head; item; item = item->next) {
        ++count;
        std::cout << '[' << count << "] VID_" << std::hex << std::uppercase
                  << std::setw(4) << std::setfill('0') << item->vendor_id
                  << " PID_" << std::setw(4) << item->product_id << std::dec
                  << " usage_page=0x" << std::hex << item->usage_page
                  << " usage=0x" << item->usage << std::dec << '\n'
                  << "    manufacturer: " << narrow(item->manufacturer_string) << '\n'
                  << "    product:      " << narrow(item->product_string) << '\n'
                  << "    serial:       " << narrow(item->serial_number) << '\n'
                  << "    path:         " << (item->path ? item->path : "") << '\n';
    }
    if (!count) {
        std::cout << "No VID_1D6B/PID_0126 HID interface is visible in WSL.\n";
    }
    hid_free_enumeration(head);
}

bool HidTransport::open(const std::string& requested_serial) {
    close();
    hid_device_info* head = hid_enumerate(vendor_id, product_id);
    std::string selected;
    for (hid_device_info* item = head; item; item = item->next) {
        const bool usage_match =
            (item->usage_page == 0xff00 && item->usage == 0x0001) ||
            (item->usage_page == 0 && item->usage == 0);
        const bool serial_match = requested_serial.empty() ||
                                  narrow(item->serial_number) == requested_serial;
        if (usage_match && serial_match && item->path) {
            selected = item->path;
            break;
        }
    }
    hid_free_enumeration(head);
    if (selected.empty()) {
        std::cerr << "COUGAR VID_1D6B/PID_0126 HID interface is not visible.\n";
        return false;
    }
    handle_ = hid_open_path(selected.c_str());
    if (!handle_) {
        std::cerr << "Could not open COUGAR HID interface; check USB attachment "
                     "and permissions.\n";
        return false;
    }
    hid_set_nonblocking(handle_, 0);
    std::cout << "Opened confirmed COUGAR LCD HID interface.\n";
    return true;
}

void HidTransport::close() {
    if (handle_) hid_close(handle_);
    handle_ = nullptr;
}

bool HidTransport::send_request(const std::string& command,
                                const std::optional<std::string>& body) {
    std::ostringstream payload;
    payload << "POST " << command << " 1\r\n"
            << "SeqNumber=" << sequence_++ << "\r\n"
            << "Date=" << unix_time_ms() << "\r\n";
    if (body) {
        payload << "ContentType=json\r\n"
                << "ContentLength=" << body->size() << "\r\n\r\n"
                << *body;
    } else {
        payload << "\r\n";
    }
    std::vector<std::uint8_t> report(report_bytes);
    if (!build_report(payload.str(), report)) {
        std::cerr << "Protocol request does not fit one HID report.\n";
        return false;
    }
    return write_report(report);
}

bool HidTransport::wait_for_response(int timeout_ms, const std::string& required) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    std::array<unsigned char, report_bytes> input{};
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        const int count = hid_read_timeout(handle_, input.data(), input.size(),
            static_cast<int>(std::max<std::int64_t>(1, remaining)));
        if (count < 0) {
            std::cerr << "HID read failed: " << narrow(hid_error(handle_)) << '\n';
            return false;
        }
        if (count == 0) break;
        auto payload = decode_frame(input.data(), static_cast<std::size_t>(count));
        if (!payload) continue;
        if (payload->rfind("1 200", 0) == 0 &&
            (required.empty() || payload->find(required) != std::string::npos)) {
            return true;
        }
    }
    std::cerr << "Timed out waiting for the expected LCD response.\n";
    return false;
}

bool HidTransport::set_brightness(unsigned value) {
    value = std::min(value, 100u);
    return send_request("brightness", "{\"value\":" + std::to_string(value) + "}") &&
           wait_for_response(3000);
}

bool HidTransport::resume() {
    return send_request("power", "{\"event\":\"resume\"}") &&
           wait_for_response(3000);
}

bool HidTransport::upload_png(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Could not open image: " << path << '\n';
        return false;
    }
    const std::streamsize signed_size = file.tellg();
    if (signed_size <= 8 || signed_size > 64'000'000) {
        std::cerr << "PNG must be between 9 bytes and 64 MB.\n";
        return false;
    }
    const auto file_size = static_cast<std::uint64_t>(signed_size);
    file.seekg(0);
    constexpr std::array<unsigned char, 8> png_signature{
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::array<unsigned char, 8> signature{};
    file.read(reinterpret_cast<char*>(signature.data()), signature.size());
    if (!file || signature != png_signature) {
        std::cerr << "Refusing upload: file contents are not PNG.\n";
        return false;
    }
    file.clear();
    file.seekg(0);

    const std::string name = basename(path);
    if (name.empty() || name.size() > 127 ||
        !std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '.' || c == '_' || c == '-';
        })) {
        std::cerr << "Image filename contains unsupported characters.\n";
        return false;
    }
    const auto count64 = (file_size + media_data_bytes - 1) / media_data_bytes;
    if (count64 > std::numeric_limits<std::uint16_t>::max()) return false;
    const auto block_count = static_cast<std::uint16_t>(count64);

    std::ostringstream body;
    body << "{\"type\":\"media\",\"fileSize\":" << file_size
         << ",\"fileName\":\"" << name << "\"}";
    if (!send_request("transport", body.str()) ||
        !wait_for_response(5000, "\"blockMaxSize\":1024")) return false;

    std::vector<std::uint8_t> report(report_bytes, 0);
    for (std::uint16_t index = 0; index < block_count; ++index) {
        std::fill(report.begin(), report.end(), 0);
        const auto remaining = file_size -
            static_cast<std::uint64_t>(index) * media_data_bytes;
        const auto chunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, media_data_bytes));
        file.read(reinterpret_cast<char*>(report.data() + media_header_bytes),
                  static_cast<std::streamsize>(chunk));
        if (file.gcount() != static_cast<std::streamsize>(chunk)) return false;

        const auto frame_length = static_cast<std::uint16_t>(21 + chunk);
        report[0] = 0;
        report[1] = 0x5c;
        report[2] = static_cast<std::uint8_t>(frame_length >> 8);
        report[3] = static_cast<std::uint8_t>(frame_length);
        report[4] = 0x13;
        report[5] = static_cast<std::uint8_t>(block_count >> 8);
        report[6] = static_cast<std::uint8_t>(block_count);
        report[7] = static_cast<std::uint8_t>(index >> 8);
        report[8] = static_cast<std::uint8_t>(index);
        report[9] = 0x02;
        if (!write_report(report)) {
            std::cerr << "Media write failed at block " << (index + 1)
                      << " of " << block_count << ".\n";
            return false;
        }
    }
    if (!wait_for_response(10'000)) return false;

    const std::string completion =
        "{\"md5\":\"todo\",\"fileName\":\"" + name + "\"}";
    return send_request("transported", completion) &&
           wait_for_response(10'000, "\"state\":\"success\"");
}

bool HidTransport::write_report(const std::vector<std::uint8_t>& report) {
    if (!handle_ || report.size() != report_bytes) return false;
    const int written = hid_write(handle_, report.data(), report.size());
    if (written != static_cast<int>(report.size()) &&
        written != static_cast<int>(report.size() - 1)) {
        std::cerr << "HID write failed or was short (" << written << "): "
                  << narrow(hid_error(handle_)) << '\n';
        return false;
    }
    return true;
}

} // namespace cougar
