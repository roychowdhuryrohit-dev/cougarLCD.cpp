#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr USHORT target_vid = 0x1d6b;
constexpr USHORT target_pid = 0x0126;

struct Options {
    bool all = false;
    DWORD listen_ms = 0;
};

void usage() {
    std::wcout
        << L"cougar-hid-probe (strictly read-only)\n\n"
        << L"Usage: cougar-hid-probe [--all] [--listen-ms N]\n"
        << L"  --all          Show every HID interface, not only the COUGAR LCD\n"
        << L"  --listen-ms N  Capture incoming reports for N milliseconds\n";
}

bool parse_dword(const wchar_t* text, DWORD& value) {
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(text, &end, 10);
    if (!text[0] || !end || *end) return false;
    value = static_cast<DWORD>(parsed);
    return true;
}

bool parse_options(int argc, wchar_t** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"-h" || arg == L"--help") {
            usage();
            std::exit(0);
        }
        if (arg == L"--all") options.all = true;
        else if (arg == L"--listen-ms" && i + 1 < argc) {
            if (!parse_dword(argv[++i], options.listen_ms) ||
                options.listen_ms > 3'600'000) return false;
        } else return false;
    }
    return true;
}

void print_error(const wchar_t* operation, DWORD error) {
    wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wcerr << operation << L": error " << error;
    if (message) std::wcerr << L" - " << message;
    std::wcerr << L'\n';
    LocalFree(message);
}

using Handle = std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&CloseHandle)>;

Handle open_device(const wchar_t* path, DWORD access, DWORD flags = 0) {
    return Handle(CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, flags, nullptr), CloseHandle);
}

void print_string(HANDLE device, const wchar_t* label,
                  BOOLEAN(__stdcall* getter)(HANDLE, PVOID, ULONG)) {
    wchar_t value[256]{};
    if (getter(device, value, sizeof(value))) {
        std::wcout << L"  " << std::left << std::setw(14) << label << value << L'\n';
    }
}

void dump_report(const BYTE* bytes, DWORD count) {
    for (DWORD offset = 0; offset < count; offset += 16) {
        const DWORD line = std::min<DWORD>(16, count - offset);
        std::wcout << L"    " << std::hex << std::uppercase << std::setw(4)
                   << std::setfill(L'0') << offset << L"  ";
        for (DWORD i = 0; i < 16; ++i) {
            if (i < line) {
                std::wcout << std::setw(2) << static_cast<unsigned>(bytes[offset + i])
                           << L' ';
            } else {
                std::wcout << L"   ";
            }
        }
        std::wcout << L" | ";
        for (DWORD i = 0; i < line; ++i) {
            const unsigned char c = bytes[offset + i];
            std::wcout << static_cast<wchar_t>(c >= 32 && c < 127 ? c : '.');
        }
        std::wcout << std::dec << std::setfill(L' ') << L'\n';
    }
}

void listen(const wchar_t* path, USHORT input_bytes, DWORD duration_ms) {
    if (!duration_ms || !input_bytes) return;
    Handle device = open_device(path, GENERIC_READ, FILE_FLAG_OVERLAPPED);
    if (device.get() == INVALID_HANDLE_VALUE) {
        print_error(L"Open for read-only capture", GetLastError());
        return;
    }
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr), CloseHandle);
    if (!event || event.get() == INVALID_HANDLE_VALUE) {
        print_error(L"CreateEvent", GetLastError());
        return;
    }
    std::vector<BYTE> report(input_bytes);
    const ULONGLONG deadline = GetTickCount64() + duration_ms;
    unsigned number = 0;
    std::wcout << L"  Listening for input reports for " << duration_ms << L" ms...\n";
    while (GetTickCount64() < deadline) {
        OVERLAPPED overlap{};
        overlap.hEvent = event.get();
        ResetEvent(event.get());
        DWORD received = 0;
        const BOOL started = ReadFile(device.get(), report.data(), input_bytes,
                                      &received, &overlap);
        if (!started && GetLastError() != ERROR_IO_PENDING) {
            print_error(L"ReadFile", GetLastError());
            return;
        }
        const ULONGLONG now = GetTickCount64();
        const DWORD remaining = now < deadline
            ? static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, MAXDWORD)) : 0;
        const DWORD wait = WaitForSingleObject(event.get(), remaining);
        if (wait == WAIT_TIMEOUT) {
            CancelIoEx(device.get(), &overlap);
            WaitForSingleObject(event.get(), INFINITE);
            break;
        }
        if (wait != WAIT_OBJECT_0 ||
            !GetOverlappedResult(device.get(), &overlap, &received, FALSE)) {
            print_error(L"Read completion", GetLastError());
            return;
        }
        std::wcout << L"  Input report " << ++number << L" (" << received
                   << L" bytes)\n";
        dump_report(report.data(), received);
    }
    if (!number) std::wcout << L"  No input reports arrived.\n";
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        usage();
        return 2;
    }
    std::wcout << L"COUGAR CFV235 LCD HID diagnostic\n"
               << L"Mode: strictly read-only (no output or feature reports)\n\n";

    GUID hid_guid{};
    HidD_GetHidGuid(&hid_guid);
    HDEVINFO raw_set = SetupDiGetClassDevsW(&hid_guid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (raw_set == INVALID_HANDLE_VALUE) {
        print_error(L"SetupDiGetClassDevs", GetLastError());
        return 1;
    }
    struct DeviceSetGuard {
        HDEVINFO value;
        ~DeviceSetGuard() { SetupDiDestroyDeviceInfoList(value); }
    } set_guard{raw_set};

    unsigned shown = 0;
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interface_data{};
        interface_data.cbSize = sizeof(interface_data);
        if (!SetupDiEnumDeviceInterfaces(raw_set, nullptr, &hid_guid, index,
                                         &interface_data)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) {
                print_error(L"SetupDiEnumDeviceInterfaces", GetLastError());
            }
            break;
        }
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(raw_set, &interface_data, nullptr, 0,
                                         &needed, nullptr);
        std::vector<BYTE> detail_buffer(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
            detail_buffer.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(raw_set, &interface_data, detail,
                                               needed, nullptr, nullptr)) continue;

        Handle query = open_device(detail->DevicePath, 0);
        if (query.get() == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (!HidD_GetAttributes(query.get(), &attributes)) continue;
        const bool target = attributes.VendorID == target_vid &&
                            attributes.ProductID == target_pid;
        if (!target && !options.all) continue;

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps{};
        if (HidD_GetPreparsedData(query.get(), &preparsed)) {
            HidP_GetCaps(preparsed, &caps);
            HidD_FreePreparsedData(preparsed);
        }
        std::wcout << L'[' << ++shown << L"] VID_" << std::hex << std::uppercase
                   << std::setw(4) << std::setfill(L'0') << attributes.VendorID
                   << L" PID_" << std::setw(4) << attributes.ProductID << std::dec
                   << std::setfill(L' ') << (target ? L"  <target>" : L"") << L'\n';
        std::wcout << L"  Path          " << detail->DevicePath << L'\n';
        print_string(query.get(), L"Manufacturer", HidD_GetManufacturerString);
        print_string(query.get(), L"Product", HidD_GetProductString);
        print_string(query.get(), L"Serial", HidD_GetSerialNumberString);
        std::wcout << L"  Usage         page=0x" << std::hex << std::uppercase
                   << std::setw(4) << std::setfill(L'0') << caps.UsagePage
                   << L" usage=0x" << std::setw(4) << caps.Usage << std::dec
                   << std::setfill(L' ') << L'\n'
                   << L"  Report bytes  input=" << caps.InputReportByteLength
                   << L" output=" << caps.OutputReportByteLength
                   << L" feature=" << caps.FeatureReportByteLength << L'\n';
        if (target) listen(detail->DevicePath, caps.InputReportByteLength,
                           options.listen_ms);
        std::wcout << L'\n';
    }
    if (!shown) std::wcout << L"No matching HID interfaces found.\n";
    std::wcout << L"Done. No reports were sent to any device.\n";
    return 0;
}
