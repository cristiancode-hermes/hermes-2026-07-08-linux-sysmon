#pragma once

// ──────────────────────────────────────────────────────
// procfs_reader.hpp
// Reads /proc files using low-level I/O (open/pread)
// Uses RAII file descriptors and string_view parsing
// ──────────────────────────────────────────────────────

#include <linux_sysmon/sysmon.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <array>
#include <algorithm>
#include <charconv>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>

namespace linux_sysmon::procfs {

// ── RAII file descriptor ──
class Fd {
public:
  Fd() noexcept = default;
  explicit Fd(const char* path, int flags = O_RDONLY)
    : fd_(::open(path, flags)) {}

  ~Fd() { close(); }

  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;

  Fd(Fd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  Fd& operator=(Fd&& other) noexcept {
    if (this != &other) { close(); fd_ = other.fd_; other.fd_ = -1; }
    return *this;
  }

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] explicit operator bool() const noexcept { return fd_ >= 0; }

  void close() noexcept {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
  }

private:
  int fd_ = -1;
};

// ── MmapRegion: RAII mapped memory for regular files ──
// NOTE: /proc files don't support mmap (st_size == 0).
// This class works on regular files. For /proc, use read_file().
class MmapRegion {
public:
  MmapRegion() noexcept = default;

  MmapRegion(const char* path) {
    Fd fd(path);
    if (!fd) return;

    struct stat st{};
    if (::fstat(fd.get(), &st) < 0 || st.st_size == 0) return;

    void* p = ::mmap(nullptr, static_cast<size_t>(st.st_size),
                     PROT_READ, MAP_PRIVATE, fd.get(), 0);
    if (p == MAP_FAILED) return;

    data_ = static_cast<const char*>(p);
    size_ = static_cast<size_t>(st.st_size);
  }

  ~MmapRegion() { unmap(); }

  MmapRegion(const MmapRegion&) = delete;
  MmapRegion& operator=(const MmapRegion&) = delete;

  MmapRegion(MmapRegion&& other) noexcept
    : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
  }
  MmapRegion& operator=(MmapRegion&& other) noexcept {
    if (this != &other) { unmap(); data_ = other.data_; size_ = other.size_; other.data_ = nullptr; other.size_ = 0; }
    return *this;
  }

  [[nodiscard]] std::string_view str() const noexcept {
    return data_ ? std::string_view{data_, size_} : std::string_view{};
  }
  [[nodiscard]] explicit operator bool() const noexcept { return data_ != nullptr; }

private:
  const char* data_ = nullptr;
  size_t      size_ = 0;

  void unmap() noexcept {
    if (data_) { ::munmap(const_cast<char*>(data_), size_); data_ = nullptr; size_ = 0; }
  }
};

// ── Line-by-line parser (zero-copy on string_view) ──
class LineIterator {
public:
  explicit LineIterator(std::string_view data) : remaining_(data) {}

  [[nodiscard]] std::string_view next() {
    if (remaining_.empty()) return {};
    auto pos = remaining_.find('\n');
    std::string_view line;
    if (pos == std::string_view::npos) {
      line = remaining_;
      remaining_ = {};
    } else {
      line = remaining_.substr(0, pos);
      remaining_ = remaining_.substr(pos + 1);
    }
    // Trim trailing \r and spaces
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
      line.remove_suffix(1);
    return line;
  }

  [[nodiscard]] bool done() const noexcept { return remaining_.empty(); }
  void reset(std::string_view data) { remaining_ = data; }

private:
  std::string_view remaining_;
};

// ── Read a procfs file into a string via pread(2) ──
// /proc files don't support mmap (st_size is 0), so we read them.
[[nodiscard]] inline Result<std::string> read_file(const char* path, size_t buf_size = 65536) {
  Fd fd(path);
  if (!fd)
    return std::unexpected(make_error(errno, std::format("Cannot open {}", path)));

  std::string buf(buf_size, '\0');
  auto n = ::read(fd.get(), buf.data(), buf_size - 1);
  if (n < 0)
    return std::unexpected(make_error(errno, std::format("Cannot read {}", path)));

  buf.resize(static_cast<size_t>(n));
  return buf;
}

// ── Helper: parse key: value from /proc lines ──
// "MemTotal:       32768 kB" -> ("MemTotal", "32768 kB")
// Also handles "model name\t: value"
[[nodiscard]] inline std::pair<std::string_view, std::string_view>
split_kv(std::string_view line) {
  auto colon = line.find(':');
  if (colon == std::string_view::npos) return {};
  auto key = line.substr(0, colon);
  auto val = line.substr(colon + 1);
  // Trim leading whitespace from value
  while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
    val.remove_prefix(1);
  // Trim trailing whitespace from key (handles "key\t:" format)
  while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
    key.remove_suffix(1);
  return {key, val};
}

// ── Read /proc/cpuinfo via pread(2) ──
[[nodiscard]] inline Result<CpuInfo> read_cpu_info() {
  auto content = read_file("/proc/cpuinfo");
  if (!content) return std::unexpected(content.error());

  CpuInfo info;
  LineIterator it(*content);
  unsigned processor_count = 0;

  while (!it.done()) {
    auto line = it.next();
    auto [key, val] = split_kv(line);

    if (key == "model name" && info.model_name.empty())
      info.model_name = val;
    else if (key == "processor")
      processor_count = static_cast<unsigned>(std::stoul(std::string(val)));
    else if (key == "cpu cores")
      info.cores = static_cast<unsigned>(std::stoul(std::string(val)));
    else if (key == "siblings")
      info.threads = static_cast<unsigned>(std::stoul(std::string(val)));
    else if (key == "cpu MHz")
      info.freq_mhz = std::stod(std::string(val));
  }

  // If threads not explicit, assume 1 per core
  if (info.threads == 0) info.threads = processor_count + 1;
  if (info.cores == 0)   info.cores = info.threads;

  return info;
}

// ── Read /proc/meminfo via pread(2) ──
[[nodiscard]] inline Result<MemInfo> read_mem_info() {
  auto content = read_file("/proc/meminfo");
  if (!content) return std::unexpected(content.error());

  MemInfo info;
  LineIterator it(*content);

  while (!it.done()) {
    auto line = it.next();
    auto [key, val_str] = split_kv(line);

    // Parse numeric value (strip " kB" suffix)
    auto num_str = val_str;
    auto sp = num_str.find(' ');
    if (sp != std::string_view::npos) num_str = num_str.substr(0, sp);

    uint64_t val = 0;
    auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), val);
    if (ec != std::errc()) continue;

    if      (key == "MemTotal")     info.total_kb = val;
    else if (key == "MemFree")      info.free_kb  = val;
    else if (key == "MemAvailable") info.avail_kb = val;
  }

  return info;
}

// ── Read /proc/uptime via pread(2) ──
[[nodiscard]] inline Result<std::chrono::seconds> read_uptime() {
  auto content = read_file("/proc/uptime");
  if (!content) return std::unexpected(content.error());

  auto sv = std::string_view(*content);
  auto sp = sv.find(' ');
  if (sp == std::string_view::npos)
    return std::unexpected(make_error(EINVAL, "Invalid /proc/uptime format"));

  double secs = 0.0;
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sp, secs);
  if (ec != std::errc())
    return std::unexpected(make_error(EINVAL, "Cannot parse uptime"));

  return std::chrono::seconds{static_cast<long long>(secs)};
}

// ── Hostname via gethostname(2) ──
[[nodiscard]] inline Result<std::string> read_hostname() {
  std::array<char, 256> buf{};
  if (::gethostname(buf.data(), buf.size()) < 0)
    return std::unexpected(make_error(errno, ::strerror(errno)));
  return std::string(buf.data());
}

// ── Kernel version via uname(2) ──
[[nodiscard]] inline Result<std::string> read_kernel_version() {
  struct utsname uts{};
  if (::uname(&uts) < 0)
    return std::unexpected(make_error(errno, ::strerror(errno)));
  return std::string(uts.release);
}

// ── Aggregate all /proc info ──
[[nodiscard]] inline Result<SysInfo> read_all() {
  SysInfo si;

  auto cpu = read_cpu_info();
  if (cpu) si.cpu = *cpu;

  auto mem = read_mem_info();
  if (mem) si.mem = *mem;

  auto ut = read_uptime();
  if (ut) si.uptime = *ut;

  auto hn = read_hostname();
  if (hn) si.hostname = *hn;

  auto kv = read_kernel_version();
  if (kv) si.kernel_version = *kv;

  return si;
}

} // namespace linux_sysmon::procfs
