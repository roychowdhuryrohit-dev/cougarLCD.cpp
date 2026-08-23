#include "hid_transport.hpp"
#include "renderer.hpp"
#include "telemetry.hpp"

#include <hidapi/hidapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <clocale>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic_bool stop_requested{false};

void on_signal(int) {
    stop_requested.store(true);
}

struct Options {
    bool probe = false;
    bool render_only = false;
    bool quiet = false;
    std::string render_path;
    std::string upload_path;
    std::string font;
    std::string serial;
    std::string cpu_temp_dir = "/mnt/c/ProgramData/CougarLCD";
    unsigned frames = 0;
    unsigned interval_ms = 1000;
    unsigned brightness = 75;
};

void usage() {
    std::cout
        << "cougar-lcd - independent COUGAR CFV235 LCD client\n\n"
        << "Usage: cougar-lcd [options]\n"
        << "  --probe                 Enumerate only; send no reports\n"
        << "  --render-only PATH      Render one dashboard PNG; no USB access\n"
        << "  --upload PATH           Upload one PNG and exit\n"
        << "  --font PATH             Optional user-supplied TrueType/OpenType font\n"
        << "  --serial SERIAL         Require one exact LCD serial\n"
        << "  --cpu-temp-dir PATH     AMD SDK CSV directory\n"
        << "  --brightness N          LCD brightness, 0..100 (default 75)\n"
        << "  --frames N              Stop after N live frames, 1..3600\n"
        << "  --interval-ms N         Update interval, 500..60000 (default 1000)\n"
        << "  --quiet                 Suppress per-frame status output\n"
        << "  -h, --help              Show this help\n";
}

bool parse_unsigned(const char* text, unsigned& value) {
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (!text[0] || !end || *end || parsed > 0xffffffffUL) return false;
    value = static_cast<unsigned>(parsed);
    return true;
}

enum class ParseResult { ok, help, error };

ParseResult parse_options(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") return ParseResult::help;
        if (arg == "--probe") options.probe = true;
        else if (arg == "--quiet") options.quiet = true;
        else if (arg == "--render-only" && i + 1 < argc) {
            options.render_only = true;
            options.render_path = argv[++i];
        } else if (arg == "--upload" && i + 1 < argc) {
            options.upload_path = argv[++i];
        } else if (arg == "--font" && i + 1 < argc) options.font = argv[++i];
        else if (arg == "--serial" && i + 1 < argc) options.serial = argv[++i];
        else if (arg == "--cpu-temp-dir" && i + 1 < argc) {
            options.cpu_temp_dir = argv[++i];
        } else if (arg == "--brightness" && i + 1 < argc) {
            if (!parse_unsigned(argv[++i], options.brightness) ||
                options.brightness > 100) return ParseResult::error;
        } else if (arg == "--frames" && i + 1 < argc) {
            if (!parse_unsigned(argv[++i], options.frames) || options.frames < 1 ||
                options.frames > 3600) return ParseResult::error;
        } else if (arg == "--interval-ms" && i + 1 < argc) {
            if (!parse_unsigned(argv[++i], options.interval_ms) ||
                options.interval_ms < 500 || options.interval_ms > 60000) {
                return ParseResult::error;
            }
        } else {
            std::cerr << "Unknown or incomplete option: " << arg << '\n';
            return ParseResult::error;
        }
    }
    const unsigned modes = static_cast<unsigned>(options.probe) +
                           static_cast<unsigned>(options.render_only) +
                           static_cast<unsigned>(!options.upload_path.empty());
    return modes > 1 ? ParseResult::error : ParseResult::ok;
}

cougar::Metrics sample_metrics(cougar::CpuLoad& cpu,
                               cougar::AmdCsvTelemetry& amd,
                               cougar::NvmlTelemetry& nvml,
                               bool have_nvml) {
    cougar::Metrics metrics;
    metrics.cpu_load = cpu.sample();
    amd.sample(metrics.cpu_temperature, metrics.cpu_load);
    if (have_nvml) {
        nvml.sample(metrics.gpu_temperature, metrics.gpu_load);
    }
    return metrics;
}

} // namespace

int main(int argc, char** argv) {
    std::setlocale(LC_ALL, "");
    Options options;
    const ParseResult parsed = parse_options(argc, argv, options);
    if (parsed == ParseResult::help) {
        usage();
        return 0;
    }
    if (parsed == ParseResult::error) {
        usage();
        return 2;
    }

    cougar::CpuLoad cpu;
    cougar::AmdCsvTelemetry amd(options.cpu_temp_dir);
    cougar::NvmlTelemetry nvml;
    const bool have_nvml = nvml.initialize();
    cpu.sample();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    cougar::Metrics metrics = sample_metrics(cpu, amd, nvml, have_nvml);

    if (options.render_only) {
        if (!cougar::render_dashboard(options.render_path, metrics, options.font)) {
            std::cerr << "Could not render PNG.\n";
            return 1;
        }
        std::cout << "Rendered " << options.render_path << '\n';
        return 0;
    }

    if (hid_init() != 0) {
        std::cerr << "Could not initialize hidapi.\n";
        return 1;
    }
    struct HidExit { ~HidExit() { hid_exit(); } } hid_exit_guard;

    if (options.probe) {
        std::cout << "COUGAR CFV235 LCD Linux HID probe (read-only)\n";
        cougar::HidTransport::enumerate();
        return 0;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    cougar::HidTransport device;
    if (!device.open(options.serial)) return 1;
    if (!device.set_brightness(options.brightness) || !device.resume()) return 1;
    if (!options.upload_path.empty()) {
        return device.upload_png(options.upload_path) ? 0 : 1;
    }

    const std::string frame_path = "/tmp/cougar-lcd-live.png";
    unsigned frame = 0;
    const auto frame_interval = std::chrono::milliseconds(options.interval_ms);
    const auto steady_now = std::chrono::steady_clock::now();
    const auto wall_now = std::chrono::system_clock::now();
    auto next_frame = steady_now + frame_interval;
    if (options.interval_ms == 1000) {
        const auto next_wall_second =
            std::chrono::time_point_cast<std::chrono::seconds>(wall_now) +
            std::chrono::seconds(1) + std::chrono::milliseconds(100);
        next_frame = steady_now + (next_wall_second - wall_now);
    }
    std::cout << "COUGAR LCD live dashboard started.\n";
    while (!stop_requested.load()) {
        metrics = sample_metrics(cpu, amd, nvml, have_nvml);
        if (!cougar::render_dashboard(frame_path, metrics, options.font) ||
            !device.upload_png(frame_path)) {
            std::cerr << "Live frame render/upload failed.\n";
            std::remove(frame_path.c_str());
            return 1;
        }
        ++frame;
        if (!options.quiet) {
            std::cout << "Frame " << frame << ": CPU "
                      << (metrics.cpu_temperature
                              ? std::to_string(metrics.cpu_temperature) + "C/"
                              : "--C/")
                      << metrics.cpu_load << "% GPU "
                      << (metrics.gpu_temperature
                              ? std::to_string(metrics.gpu_temperature) + "C/"
                              : "--C/")
                      << metrics.gpu_load << "%\n";
        }
        if (options.frames && frame >= options.frames) break;
        while (!stop_requested.load()) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_frame) break;
            std::this_thread::sleep_until(
                std::min(next_frame, now + std::chrono::milliseconds(20)));
        }
        next_frame += frame_interval;
        while (next_frame <= std::chrono::steady_clock::now()) {
            next_frame += frame_interval;
        }
    }
    std::remove(frame_path.c_str());
    std::cout << "COUGAR LCD live dashboard stopped.\n";
    return 0;
}
