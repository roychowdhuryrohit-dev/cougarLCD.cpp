#include "telemetry.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <sys/stat.h>

namespace cougar {

unsigned CpuLoad::sample() {
    std::ifstream stat("/proc/stat");
    std::string label;
    std::uint64_t user = 0, nice = 0, system = 0, idle = 0, io_wait = 0;
    std::uint64_t irq = 0, soft_irq = 0, steal = 0;
    if (!(stat >> label >> user >> nice >> system >> idle >> io_wait >> irq >>
          soft_irq >> steal) || label != "cpu") return 0;
    const std::uint64_t idle_all = idle + io_wait;
    const std::uint64_t total = user + nice + system + idle + io_wait + irq +
                                soft_irq + steal;
    unsigned load = 0;
    if (ready_ && total > total_) {
        const std::uint64_t total_delta = total - total_;
        const std::uint64_t idle_delta = idle_all - idle_;
        load = static_cast<unsigned>(std::min<std::uint64_t>(100,
            ((total_delta - std::min(total_delta, idle_delta)) * 100 +
             total_delta / 2) / total_delta));
    }
    total_ = total;
    idle_ = idle_all;
    ready_ = true;
    return load;
}

AmdCsvTelemetry::AmdCsvTelemetry(std::string directory)
    : directory_(std::move(directory)) {}

std::vector<std::string> AmdCsvTelemetry::split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        const auto comma = line.find(',', start);
        fields.push_back(line.substr(start, comma == std::string::npos
            ? std::string::npos : comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return fields;
}

std::string AmdCsvTelemetry::trim(std::string value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
                value.end());
    return value;
}

std::string AmdCsvTelemetry::newest_log() const {
    namespace fs = std::filesystem;
    std::error_code error;
    fs::file_time_type newest_time{};
    std::string newest;
    for (fs::directory_iterator it(directory_, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        const std::string name = it->path().filename().string();
        if (name.rfind("RMSDK_Parameter_log_cougar_", 0) != 0 ||
            it->path().extension() != ".csv") continue;
        const auto modified = it->last_write_time(error);
        if (!error && (newest.empty() || modified > newest_time)) {
            newest_time = modified;
            newest = it->path().string();
        }
    }
    return newest;
}

bool AmdCsvTelemetry::parse_file(const std::string& path, unsigned& temperature,
                                 unsigned& load) const {
    constexpr std::streamoff scan_bytes = 256 * 1024;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamoff size = file.tellg();
    if (size <= 0) return false;

    const auto read_range = [&](std::streamoff begin, std::streamoff end) {
        file.clear();
        file.seekg(begin);
        std::string data(static_cast<std::size_t>(end - begin), '\0');
        file.read(data.data(), static_cast<std::streamsize>(data.size()));
        data.resize(static_cast<std::size_t>(file.gcount()));
        return data;
    };
    const std::string head = read_range(0, std::min(size, scan_bytes));
    const std::streamoff tail_start = std::max<std::streamoff>(0, size - scan_bytes);
    const std::string tail = tail_start == 0 ? head : read_range(tail_start, size);

    std::optional<std::size_t> temp_column;
    std::vector<std::size_t> residency_columns;
    std::istringstream headings(head);
    for (std::string line; std::getline(headings, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto fields = split_csv(line);
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const std::string heading = trim(fields[i]);
            if (heading == "Current Temperature (Celsius)") {
                temp_column = i;
            } else if (heading.rfind("Core ", 0) == 0 &&
                       heading.find(" C0 Residency (%)") != std::string::npos) {
                residency_columns.push_back(i);
            }
        }
        if (temp_column) break;
    }
    if (!temp_column) return false;

    std::vector<std::string> lines;
    std::istringstream rows(tail);
    for (std::string line; std::getline(rows, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        const auto fields = split_csv(*it);
        if (*temp_column >= fields.size()) continue;
        const std::string value = trim(fields[*temp_column]);
        char* end = nullptr;
        const double parsed = std::strtod(value.c_str(), &end);
        if (!end || end == value.c_str() || *end || parsed < 1.0 || parsed > 120.0) {
            continue;
        }
        temperature = static_cast<unsigned>(std::lround(parsed));

        double residency_total = 0.0;
        std::size_t residency_count = 0;
        for (const std::size_t column : residency_columns) {
            if (column >= fields.size()) continue;
            const std::string text = trim(fields[column]);
            char* residency_end = nullptr;
            const double residency = std::strtod(text.c_str(), &residency_end);
            if (!residency_end || residency_end == text.c_str() || *residency_end ||
                residency < 0.0 || residency > 100.0) continue;
            residency_total += residency;
            ++residency_count;
        }
        if (residency_count) {
            load = static_cast<unsigned>(std::lround(
                residency_total / static_cast<double>(residency_count)));
        }
        return true;
    }
    return false;
}

bool AmdCsvTelemetry::sample(unsigned& temperature, unsigned& load) {
    const std::string path = newest_log();
    if (path.empty()) return false;
    struct stat details{};
    if (::stat(path.c_str(), &details) != 0 ||
        std::difftime(std::time(nullptr), details.st_mtime) > 15.0) return false;
    return parse_file(path, temperature, load);
}

bool NvmlTelemetry::initialize() {
    module_ = dlopen("libnvidia-ml.so.1", RTLD_NOW);
    if (!module_) module_ = dlopen("/usr/lib/wsl/lib/libnvidia-ml.so.1", RTLD_NOW);
    if (!module_) return false;
    init_ = reinterpret_cast<InitFn>(dlsym(module_, "nvmlInit_v2"));
    shutdown_ = reinterpret_cast<ShutdownFn>(dlsym(module_, "nvmlShutdown"));
    handle_ = reinterpret_cast<HandleFn>(
        dlsym(module_, "nvmlDeviceGetHandleByIndex_v2"));
    temperature_ = reinterpret_cast<TempFn>(
        dlsym(module_, "nvmlDeviceGetTemperature"));
    utilization_ = reinterpret_cast<UtilFn>(
        dlsym(module_, "nvmlDeviceGetUtilizationRates"));
    return init_ && handle_ && temperature_ && init_() == 0 &&
           handle_(0, &device_) == 0;
}

bool NvmlTelemetry::sample(unsigned& temperature, unsigned& load) {
    Utilization utilization{};
    if (!device_ || temperature_(device_, 0, &temperature) != 0) return false;
    load = utilization_ && utilization_(device_, &utilization) == 0
        ? utilization.gpu : 0;
    return true;
}

NvmlTelemetry::~NvmlTelemetry() {
    if (shutdown_ && device_) shutdown_();
    if (module_) dlclose(module_);
}

} // namespace cougar
