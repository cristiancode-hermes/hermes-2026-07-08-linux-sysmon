#pragma once

// ──────────────────────────────────────────────────────
// disk_info.hpp
// Disk usage via statfs(2) system call
// ──────────────────────────────────────────────────────

#include <linux_sysmon/sysmon.hpp>

#include <expected>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <sys/vfs.h>    // statfs
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace linux_sysmon::disk {

namespace detail {
  // Parse /proc/mounts to get mounted filesystems
  // Returns list of mount points (skip pseudo-fs)
  [[nodiscard]] inline std::vector<std::string> get_mount_points() {
    std::vector<std::string> mounts;
    std::ifstream f("/proc/mounts");
    if (!f) return mounts;

    std::string line;
    while (std::getline(f, line)) {
      // Format: device mount_point fs_type options dump pass
      std::istringstream ss(line);
      std::string device, mount_point, fs_type;
      ss >> device >> mount_point >> fs_type;

      // Skip pseudo-filesystems
      if (fs_type == "proc" || fs_type == "sysfs" || fs_type == "tmpfs" ||
          fs_type == "devtmpfs" || fs_type == "devpts" || fs_type == "cgroup" ||
          fs_type == "cgroup2" || fs_type == "pstore" || fs_type == "bpf" ||
          fs_type == "securityfs" || fs_type == "selinuxfs" ||
          fs_type == "hugetlbfs" || fs_type == "mqueue" ||
          mount_point.find("/run") == 0 || mount_point.find("/sys") == 0 ||
          mount_point.find("/dev") == 0)
        continue;

      if (device == "rootfs" || device == "none") continue;
      mounts.push_back(mount_point);
    }
    return mounts;
  }
} // namespace detail

// ── Get disk usage for a mount point using statfs(2) ──
[[nodiscard]] inline Result<DiskUsage> usage_for(const std::string& mount_point) {
  struct statfs buf{};
  if (::statfs(mount_point.c_str(), &buf) < 0)
    return std::unexpected(make_error(errno,
      std::format("statfs({}) failed: {}", mount_point, ::strerror(errno))));

  DiskUsage du;
  du.mount_point = mount_point;
  du.total_bytes = static_cast<uint64_t>(buf.f_blocks) * static_cast<uint64_t>(buf.f_bsize);
  du.free_bytes  = static_cast<uint64_t>(buf.f_bfree)  * static_cast<uint64_t>(buf.f_bsize);
  return du;
}

// ── Get all disk usage ──
// Note: callers should log per-mount errors as appropriate
[[nodiscard]] inline Result<std::vector<DiskUsage>> read_all() {
  auto mounts = detail::get_mount_points();
  std::vector<DiskUsage> result;
  result.reserve(mounts.size());

  for (const auto& mp : mounts) {
    auto du = usage_for(mp);
    if (du) result.push_back(std::move(*du));
  }

  return result;
}

} // namespace linux_sysmon::disk
