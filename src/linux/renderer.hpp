#pragma once

#include <string>

namespace cougar {

struct Metrics {
    unsigned cpu_temperature = 0;
    unsigned cpu_load = 0;
    unsigned gpu_temperature = 0;
    unsigned gpu_load = 0;
};

bool render_dashboard(const std::string& path, const Metrics& metrics,
                      const std::string& font_file = {});

} // namespace cougar

