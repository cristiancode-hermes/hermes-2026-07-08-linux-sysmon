// ──────────────────────────────────────────────────────
// test_main.cpp — Unit tests for linux-sysmon
// Catch2 v3 — unit + integration + benchmark
// ──────────────────────────────────────────────────────

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <spdlog/spdlog.h>

#include <cmath>

#include <linux_sysmon/sysmon.hpp>
#include <linux_sysmon/procfs_reader.hpp>
#include <linux_sysmon/sysfs_reader.hpp>
#include <linux_sysmon/terminal.hpp>
#include <linux_sysmon/disk_info.hpp>

using namespace linux_sysmon;
using Catch::Matchers::ContainsSubstring;

// ════════════════════════════════════════════════════
// Tests de las estructuras de datos
// ════════════════════════════════════════════════════

TEST_CASE("MemInfo::used_pct() returns correct percentage", "[core][unit]") {
  MemInfo mi;
  mi.total_kb = 10000;
  mi.avail_kb = 2500;
  REQUIRE(mi.used_kb() == 7500);
  REQUIRE(std::abs(mi.used_pct() - 75.0) < 0.001);
}

TEST_CASE("MemInfo::used_pct() zero total returns 0", "[core][unit]") {
  MemInfo mi;
  REQUIRE(std::abs(mi.used_pct() - 0.0) < 0.001);
}

TEST_CASE("DiskUsage::used_pct() works correctly", "[core][unit]") {
  DiskUsage du;
  du.total_bytes = 1024 * 1024 * 100;  // 100 MB
  du.free_bytes  = 1024 * 1024 * 30;   // 30 MB
  REQUIRE(du.used_bytes() == 1024 * 1024 * 70);
  REQUIRE(std::abs(du.used_pct() - 70.0) < 0.001);
}

TEST_CASE("DiskUsage::used_pct() zero total", "[core][unit]") {
  DiskUsage du;
  REQUIRE(std::abs(du.used_pct() - 0.0) < 0.001);
}

TEST_CASE("SysInfo::has_temps() returns false when empty", "[core][unit]") {
  SysInfo si;
  REQUIRE_FALSE(si.has_temps());
  si.temps.push_back({"cpu", 45.0});
  REQUIRE(si.has_temps());
}

TEST_CASE("Error can be created and read", "[core][unit]") {
  auto err = make_error(42, "test error");
  REQUIRE(err.code == 42);
  REQUIRE(err.message == "test error");
}

TEST_CASE("Result with value works via std::expected", "[core][unit]") {
  Result<int> r = 42;
  REQUIRE(r.has_value());
  REQUIRE(*r == 42);
  REQUIRE(r.value() == 42);
}

TEST_CASE("Result with error works via std::expected", "[core][unit]") {
  Result<int> r = std::unexpected(make_error(500, "fail"));
  REQUIRE_FALSE(r.has_value());
  REQUIRE(r.error().code == 500);
  REQUIRE(r.error().message == "fail");
}

// ════════════════════════════════════════════════════
// Tests de split_kv (parser de /proc)
// ════════════════════════════════════════════════════

TEST_CASE("split_kv parses standard /proc line", "[parser][unit]") {
  auto [key, val] = procfs::split_kv("MemTotal:       32768 kB");
  REQUIRE(key == "MemTotal");
  REQUIRE(val == "32768 kB");
}

TEST_CASE("split_kv handles no space after colon", "[parser][unit]") {
  auto [key, val] = procfs::split_kv("key:value");
  REQUIRE(key == "key");
  REQUIRE(val == "value");
}

TEST_CASE("split_kv returns empty for no colon", "[parser][unit]") {
  auto [key, val] = procfs::split_kv("no colon here");
  REQUIRE(key.empty());
  REQUIRE(val.empty());
}

// ════════════════════════════════════════════════════
// Tests de LineIterator
// ════════════════════════════════════════════════════

TEST_CASE("LineIterator iterates lines", "[parser][unit]") {
  std::string_view data = "line1\nline2\nline3\n";
  procfs::LineIterator it(data);
  REQUIRE(it.next() == "line1");
  REQUIRE(it.next() == "line2");
  REQUIRE(it.next() == "line3");
  REQUIRE(it.done());
}

TEST_CASE("LineIterator handles trailing newline", "[parser][unit]") {
  std::string_view data = "only line\n";
  procfs::LineIterator it(data);
  REQUIRE(it.next() == "only line");
  REQUIRE(it.done());
}

TEST_CASE("LineIterator handles empty input", "[parser][unit]") {
  std::string_view data = "";
  procfs::LineIterator it(data);
  REQUIRE(it.next().empty());
  REQUIRE(it.done());
}

TEST_CASE("LineIterator handles Windows CRLF", "[parser][unit]") {
  std::string_view data = "a\r\nb\r\n";
  procfs::LineIterator it(data);
  REQUIRE(it.next() == "a");
  REQUIRE(it.next() == "b");
  REQUIRE(it.done());
}

// ════════════════════════════════════════════════════
// ════════════════════════════════════════════════════
// Tests de Fd (RAII file descriptor)
// ════════════════════════════════════════════════════

TEST_CASE("Fd opens existing file", "[io][unit]") {
  auto fd = procfs::Fd("/proc/version");
  REQUIRE(fd);
  REQUIRE(fd.get() >= 0);
}

TEST_CASE("Fd fails on nonexistent file", "[io][unit]") {
  auto fd = procfs::Fd("/nonexistent/path");
  REQUIRE_FALSE(fd);
}

TEST_CASE("Fd move semantics work", "[io][unit]") {
  procfs::Fd fd1("/proc/version");
  REQUIRE(fd1);
  auto fd2 = std::move(fd1);
  REQUIRE_FALSE(fd1);  // moved-from is invalid
  REQUIRE(fd2);        // moved-to has fd
}

// ════════════════════════════════════════════════════
// Tests de MmapRegion (regular files only — /proc
// doesn't support mmap since st_size is 0 for procfs)
// ════════════════════════════════════════════════════

TEST_CASE("MmapRegion maps existing regular file", "[mmap][unit]") {
  // Use /etc/hostname which is a regular file with known content
  procfs::MmapRegion mr("/etc/hostname");
  REQUIRE(mr);
  REQUIRE_FALSE(mr.str().empty());
}

TEST_CASE("MmapRegion fails on nonexistent", "[mmap][unit]") {
  procfs::MmapRegion mr("/nonexistent");
  REQUIRE_FALSE(mr);
  REQUIRE(mr.str().empty());
}

TEST_CASE("MmapRegion fails on procfs (zero size)", "[mmap][unit]") {
  // /proc files have st_size == 0, so mmap should gracefully fail
  procfs::MmapRegion mr("/proc/version");
  REQUIRE_FALSE(mr);
}

TEST_CASE("MmapRegion move semantics", "[mmap][unit]") {
  procfs::MmapRegion mr1("/etc/hostname");
  REQUIRE(mr1);
  auto mr2 = std::move(mr1);
  REQUIRE_FALSE(mr1);     // moved-from
  REQUIRE(mr2);           // moved-to has data
  REQUIRE_FALSE(mr2.str().empty());
}

// ════════════════════════════════════════════════════
// Tests de integration (/proc reading on a real Linux)
// ════════════════════════════════════════════════════

TEST_CASE("Read CPU info from /proc/cpuinfo", "[integration][procfs]") {
  auto result = procfs::read_cpu_info();
  REQUIRE(result.has_value());
  auto& cpu = *result;
  REQUIRE_FALSE(cpu.model_name.empty());
  REQUIRE(cpu.cores > 0);
  REQUIRE(cpu.threads > 0);
  REQUIRE(cpu.freq_mhz > 0.0);
  spdlog::info("CPU: {} ({} cores, {} threads, {:.0f} MHz)",
    cpu.model_name, cpu.cores, cpu.threads, cpu.freq_mhz);
}

TEST_CASE("Read memory info from /proc/meminfo", "[integration][procfs]") {
  auto result = procfs::read_mem_info();
  REQUIRE(result.has_value());
  auto& mem = *result;
  REQUIRE(mem.total_kb > 0);
  REQUIRE(mem.avail_kb > 0);
  REQUIRE(mem.avail_kb <= mem.total_kb);
  spdlog::info("Memory: {} KB total, {} KB available", mem.total_kb, mem.avail_kb);
}

TEST_CASE("Read uptime from /proc/uptime", "[integration][procfs]") {
  auto result = procfs::read_uptime();
  REQUIRE(result.has_value());
  REQUIRE(result->count() > 0);
  spdlog::info("Uptime: {} seconds", result->count());
}

TEST_CASE("Read hostname", "[integration][procfs]") {
  auto result = procfs::read_hostname();
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->empty());
  spdlog::info("Hostname: {}", *result);
}

TEST_CASE("Read kernel version", "[integration][procfs]") {
  auto result = procfs::read_kernel_version();
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->empty());
  // Should start with a kernel major version number
  bool valid_kernel = result->find("6.") == 0 || result->find("5.") == 0 || result->find("4.") == 0;
  REQUIRE(valid_kernel);
  spdlog::info("Kernel: {}", *result);
}

TEST_CASE("Read all procfs info", "[integration][procfs]") {
  auto result = procfs::read_all();
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->hostname.empty());
  REQUIRE_FALSE(result->kernel_version.empty());
  REQUIRE(result->uptime.count() > 0);
  REQUIRE(result->cpu.cores > 0);
  REQUIRE(result->mem.total_kb > 0);
}

TEST_CASE("Read thermal zones from sysfs", "[integration][sysfs]") {
  auto result = sysfs::read_thermal_zones();
  REQUIRE(result.has_value());
  // Thermal zones may or may not exist — not an error either way
  if (!result->empty()) {
    for (const auto& tz : *result) {
      REQUIRE_FALSE(tz.name.empty());
      REQUIRE(tz.temp_celsius >= 0.0);
      spdlog::info("Thermal zone: {} = {:.1f}°C", tz.name, tz.temp_celsius);
    }
  }
}

TEST_CASE("Get terminal size via ioctl", "[integration][terminal]") {
  auto result = terminal::get_winsize();
  // In a TTY: returns window size. In a cron/CI env: returns error (ENOTTY).
  // Both are valid — we're testing the API works without crashing.
  if (result) {
    REQUIRE(result->cols > 0);
    REQUIRE(result->rows > 0);
    spdlog::info("Terminal: {}x{}", result->cols, result->rows);
  } else {
    spdlog::info("No terminal attached (expected in cron/CI): {}", result.error().message);
  }
}

TEST_CASE("Read disk usage for root", "[integration][disk]") {
  // Just test root — always exists
  auto result = disk::usage_for("/");
  REQUIRE(result.has_value());
  REQUIRE(result->total_bytes > 0);
  REQUIRE(result->free_bytes > 0);
  REQUIRE(result->used_bytes() < result->total_bytes);
  spdlog::info("Root disk: {} total, {} used",
    result->total_bytes, result->used_bytes());
}

TEST_CASE("Read all mounted disks", "[integration][disk]") {
  auto result = disk::read_all();
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->empty());
  for (const auto& d : *result) {
    REQUIRE(d.total_bytes > 0);
  }
}

// ════════════════════════════════════════════════════
// Benchmarks
// ════════════════════════════════════════════════════

TEST_CASE("Benchmark: read_all procfs info", "[benchmark]") {
  BENCHMARK("Read all procfs") {
    return procfs::read_all();
  };
}

TEST_CASE("Benchmark: mmap /proc/cpuinfo", "[benchmark]") {
  BENCHMARK("mmap cpuinfo") {
    return procfs::read_cpu_info();
  };
}

TEST_CASE("Benchmark: mmap /proc/meminfo", "[benchmark]") {
  BENCHMARK("mmap meminfo") {
    return procfs::read_mem_info();
  };
}

TEST_CASE("Benchmark: statfs(2) for root", "[benchmark]") {
  BENCHMARK("statfs /") {
    return disk::usage_for("/");
  };
}

TEST_CASE("Benchmark: thermal zones sysfs read", "[benchmark]") {
  BENCHMARK("thermal zones") {
    return sysfs::read_thermal_zones();
  };
}
