#pragma once

// ──────────────────────────────────────────────────────
// linux-sysmon — Main include header (base types only)
// Linux System Monitor: low-level hardware info via
// sysfs, procfs, ioctl, mmap, statfs
//
// NOTE: Sub-headers (procfs_reader.hpp, etc.) include this
// for base types. Include them explicitly in your .cpp files.
// ──────────────────────────────────────────────────────

#include <expected>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <print>
#include <format>

namespace linux_sysmon {

// ── Aggregated system info ──
struct CpuInfo {
  std::string  model_name;
  unsigned     cores       = 0;
  unsigned     threads     = 0;
  double       freq_mhz    = 0.0;
  uint64_t     total_jiffies  = 0;
  uint64_t     idle_jiffies   = 0;
};

struct MemInfo {
  uint64_t total_kb  = 0;
  uint64_t free_kb   = 0;
  uint64_t avail_kb  = 0;
  uint64_t used_kb() const noexcept { return total_kb - avail_kb; }
  double   used_pct() const noexcept {
    return total_kb ? (100.0 * used_kb() / static_cast<double>(total_kb)) : 0.0;
  }
};

struct ThermalZone {
  std::string name;
  double      temp_celsius = 0.0;
};

struct DiskUsage {
  std::string  mount_point;
  uint64_t     total_bytes  = 0;
  uint64_t     free_bytes   = 0;
  uint64_t     used_bytes() const noexcept { return total_bytes - free_bytes; }
  double       used_pct()   const noexcept {
    return total_bytes ? (100.0 * used_bytes() / static_cast<double>(total_bytes)) : 0.0;
  }
};

struct SysInfo {
  CpuInfo                cpu;
  MemInfo                mem;
  std::vector<ThermalZone> temps;
  std::vector<DiskUsage>  disks;
  std::chrono::seconds   uptime{0};
  std::string            hostname;
  std::string            kernel_version;

  [[nodiscard]] bool has_temps() const noexcept { return !temps.empty(); }
};

// ── Error type ──
struct Error {
  int         code    = -1;
  std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

// ── Convenience ──
inline Error make_error(int code, std::string_view msg) {
  return Error{code, std::string(msg)};
}

namespace detail {
  // Concept: types that can be read from procfs lines
  template <typename T>
  concept ProcParsable = requires(std::string_view s, T& v) {
    { v.parse(s) } -> std::same_as<void>;
  };
} // namespace detail

} // namespace linux_sysmon
