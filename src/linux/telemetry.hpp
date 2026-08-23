#pragma once

#include "renderer.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cougar {

class CpuLoad {
public:
    unsigned sample();

private:
    std::uint64_t total_ = 0;
    std::uint64_t idle_ = 0;
    bool ready_ = false;
};

class AmdCsvTelemetry {
public:
    explicit AmdCsvTelemetry(std::string directory);
    bool sample(unsigned& temperature, unsigned& load);

private:
    std::string newest_log() const;
    bool parse_file(const std::string& path, unsigned& temperature,
                    unsigned& load) const;
    static std::vector<std::string> split_csv(const std::string& line);
    static std::string trim(std::string value);

    std::string directory_;
};

class NvmlTelemetry {
public:
    NvmlTelemetry() = default;
    NvmlTelemetry(const NvmlTelemetry&) = delete;
    NvmlTelemetry& operator=(const NvmlTelemetry&) = delete;
    ~NvmlTelemetry();

    bool initialize();
    bool sample(unsigned& temperature, unsigned& load);

private:
    struct Utilization { unsigned gpu; unsigned memory; };
    using Device = void*;
    using InitFn = int (*)();
    using ShutdownFn = int (*)();
    using HandleFn = int (*)(unsigned, Device*);
    using TempFn = int (*)(Device, unsigned, unsigned*);
    using UtilFn = int (*)(Device, Utilization*);

    void* module_ = nullptr;
    InitFn init_ = nullptr;
    ShutdownFn shutdown_ = nullptr;
    HandleFn handle_ = nullptr;
    TempFn temperature_ = nullptr;
    UtilFn utilization_ = nullptr;
    Device device_ = nullptr;
};

} // namespace cougar
