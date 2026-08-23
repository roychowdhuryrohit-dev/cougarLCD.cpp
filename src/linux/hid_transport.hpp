#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct hid_device_;
using hid_device = struct hid_device_;

namespace cougar {

inline constexpr unsigned short vendor_id = 0x1d6b;
inline constexpr unsigned short product_id = 0x0126;
inline constexpr int display_width = 1920;
inline constexpr int display_height = 462;

class HidTransport {
public:
    HidTransport() = default;
    HidTransport(const HidTransport&) = delete;
    HidTransport& operator=(const HidTransport&) = delete;
    ~HidTransport();

    static void enumerate();
    bool open(const std::string& requested_serial = {});
    void close();
    bool set_brightness(unsigned value);
    bool resume();
    bool upload_png(const std::string& path);

private:
    bool send_request(const std::string& command,
                      const std::optional<std::string>& body);
    bool wait_for_response(int timeout_ms, const std::string& required = {});
    bool write_report(const std::vector<std::uint8_t>& report);

    hid_device* handle_ = nullptr;
    unsigned sequence_ = 1;
};

} // namespace cougar

