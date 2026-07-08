#pragma once

// ──────────────────────────────────────────────────────
// sysfs_reader.hpp
// Reads Linux sysfs files (/sys/class/thermal, /sys/class/power_supply, etc.)
// using RAII file descriptors and low-level read(2)
// ──────────────────────────────────────────────────────

#include <linux_sysmon/sysmon.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <fstream>
#include <charconv>
#include <cstring>
#include <array>

#include <unistd.h>
#include <fcntl.h>

namespace fs = std::filesystem;

namespace linux_sysmon::sysfs {

namespace detail {
  // Read entire sysfs file (usually single value) via low-level open/read
  [[nodiscard]] inline Result<std::string> read_sysfs_file(const fs::path& path) {
    procfs::Fd fd(path.c_str(), O_RDONLY);
    if (!fd)
      return std::unexpected(make_error(errno,
        std::format("Cannot open {}", path.c_str())));

    std::array<char, 4096> buf{};
    auto n = ::read(fd.get(), buf.data(), buf.size() - 1);
    if (n < 0)
      return std::unexpected(make_error(errno,
        std::format("Cannot read {}", path.c_str())));

    // Trim trailing newline/whitespace
    auto len = static_cast<size_t>(n);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == ' ' || buf[len - 1] == '\r'))
      --len;

    return std::string(buf.data(), len);
  }

  // Parse string as double
  [[nodiscard]] inline Result<double> parse_double(std::string_view sv) {
    double val = 0.0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    if (ec != std::errc())
      return std::unexpected(make_error(EINVAL, std::format("Cannot parse '{}' as double", sv)));
    return val;
  }
} // namespace detail

// ── Thermal zones: /sys/class/thermal/thermal_zone* ──
[[nodiscard]] inline Result<std::vector<ThermalZone>> read_thermal_zones() {
  const fs::path thermal_dir{"/sys/class/thermal"};
  std::vector<ThermalZone> zones;

  if (!fs::is_directory(thermal_dir))
    return zones;  // not an error — system may not have thermal zones

  for (const auto& entry : fs::directory_iterator(thermal_dir)) {
    auto dirname = entry.path().filename().string();
    if (dirname.find("thermal_zone") != 0) continue;

    auto type_path = entry.path() / "type";
    auto temp_path = entry.path() / "temp";

    auto type = detail::read_sysfs_file(type_path);
    auto temp_raw = detail::read_sysfs_file(temp_path);
    if (!type || !temp_raw) continue;

    auto temp_d = detail::parse_double(*temp_raw);
    if (!temp_d) continue;

    // /sys/class/thermal/thermal_zone*/temp is in millidegrees Celsius
    ThermalZone tz;
    tz.name        = std::move(*type);
    tz.temp_celsius = *temp_d / 1000.0;
    zones.push_back(std::move(tz));
  }

  return zones;
}

} // namespace linux_sysmon::sysfs
