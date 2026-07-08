// ──────────────────────────────────────────────────────
// linux-sysmon — Linux System Monitor
// Low-level hardware info via sysfs, procfs, ioctl, mmap
// ──────────────────────────────────────────────────────

#include <linux_sysmon/sysmon.hpp>
#include <linux_sysmon/procfs_reader.hpp>
#include <linux_sysmon/sysfs_reader.hpp>
#include <linux_sysmon/terminal.hpp>
#include <linux_sysmon/disk_info.hpp>
#include <linux_sysmon/version.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <print>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <string_view>
#include <ranges>
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <csignal>

using namespace linux_sysmon;
using namespace std::chrono_literals;

// ── Global flags (for signal handler) ──
static volatile std::sig_atomic_t g_running = 1;
extern "C" void handle_signal(int) { g_running = 0; }

// ── CLI argument parser (manual, no deps) ──
struct CliArgs {
  bool   monitor     = false;
  bool   once        = true;
  bool   json        = false;
  bool   no_color    = false;
  int    interval    = 2;
  std::vector<std::string> errors;
};

[[nodiscard]] CliArgs parse_args(int argc, char** argv) {
  CliArgs args;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--monitor" || arg == "-m") {
      args.monitor = true;
      args.once    = false;
    } else if (arg == "--json" || arg == "-j") {
      args.json = true;
    } else if (arg == "--no-color" || arg == "-n") {
      args.no_color = true;
    } else if (arg == "--interval" && i + 1 < argc) {
      ++i;
      auto [ptr, ec] = std::from_chars(argv[i], argv[i] + std::strlen(argv[i]), args.interval);
      if (ec != std::errc()) args.errors.emplace_back("Invalid interval value");
    } else if (arg == "--help" || arg == "-h") {
      std::println("linux-sysmon v{} — Linux System Monitor", VERSION);
      std::println("Uso: linux-sysmon [opciones]");
      std::println("");
      std::println("Opciones:");
      std::println("  -m, --monitor       Modo monitor (refresco continuo)");
      std::println("  -j, --json          Salida en JSON");
      std::println("  -n, --no-color      Sin colores ANSI");
      std::println("  --interval SEG      Intervalo de refresco (defecto: 2s)");
      std::println("  --db PATH           Ruta opcional a SQLite (para logging)");
      std::println("  -h, --help          Esta ayuda");
      std::println("");
      std::println("Ejemplos:");
      std::println("  linux-sysmon                    # Vista única");
      std::println("  linux-sysmon --monitor          # Modo watch continuo");
      std::println("  linux-sysmon --json             # Salida JSON");
      std::println("  linux-sysmon -m --interval 5    # Refresco cada 5s");
      std::exit(0);
    } else {
      args.errors.emplace_back(std::format("Argumento desconocido: {}", arg));
    }
  }
  return args;
}

// ── Format helpers (colorized) ──
[[nodiscard]] std::string fmt_size(uint64_t bytes, bool no_color = false) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double val = static_cast<double>(bytes);
  int unit = 0;
  while (val >= 1024.0 && unit < 4) { val /= 1024.0; ++unit; }
  auto s = std::format("{:.1f} {}", val, units[unit]);
  return s;
}

[[nodiscard]] std::string fmt_pct(double pct, bool no_color = false) {
  auto s = std::format("{:.1f}%", pct);
  if (no_color) return s;
  return std::format("{}{}{}", terminal::ansi::color_pct(pct), s, terminal::ansi::RESET);
}

// ── Display system info (terminal) ──
void display_info(const SysInfo& si, const std::vector<DiskUsage>& disks, bool no_color) {
  namespace term = terminal::ansi;

  if (!no_color) {
    std::print("{}{}", term::BOLD, term::CYAN);
    std::println("╔══════════════════════════════════════════╗");
    std::println("║     Linux System Monitor v{:<23}║", VERSION);
    std::println("╚══════════════════════════════════════════╝");
    std::print("{}", term::RESET);
  } else {
    std::println("=== Linux System Monitor v{} ===", VERSION);
  }

  // Host info
  std::println("{}── Host ──{}", no_color ? "" : term::BOLD, no_color ? "" : term::RESET);
  std::println("  Hostname:  {}", si.hostname);
  std::println("  Kernel:    {}", si.kernel_version);
  std::println("  Uptime:    {:>4} días {:0>2}:{:0>2}:{:0>2}",
    si.uptime.count() / 86400,
    (si.uptime.count() % 86400) / 3600,
    (si.uptime.count() % 3600) / 60,
    si.uptime.count() % 60);

  // CPU
  std::println("{}── CPU ──{}", no_color ? "" : term::BOLD, no_color ? "" : term::RESET);
  std::println("  Modelo:    {}", si.cpu.model_name);
  std::println("  Núcleos:   {} físicos, {} lógicos", si.cpu.cores, si.cpu.threads);
  std::println("  Frecuencia: {:.0f} MHz", si.cpu.freq_mhz);

  // Memory
  std::println("{}── Memoria ──{}", no_color ? "" : term::BOLD, no_color ? "" : term::RESET);
  std::println("  Total:     {}", fmt_size(si.mem.total_kb * 1024, no_color));
  std::println("  Disponible:{}", fmt_size(si.mem.avail_kb * 1024, no_color));
  std::println("  Usado:     {} ({})",
    fmt_size(si.mem.used_kb() * 1024, no_color),
    fmt_pct(si.mem.used_pct(), no_color));

  // Temperatures
  if (si.has_temps()) {
    std::println("{}── Temperatura ──{}", no_color ? "" : term::BOLD, no_color ? "" : term::RESET);
    for (const auto& tz : si.temps) {
      auto color = tz.temp_celsius > 80.0 ? term::RED
                 : tz.temp_celsius > 60.0 ? term::YELLOW
                 : term::GREEN;
      std::print("  {:<20} ", tz.name);
      if (!no_color) std::print("{}{:.1f}°C{}", color, tz.temp_celsius, term::RESET);
      else std::print("{:.1f}°C", tz.temp_celsius);
      std::print("\n");
    }
  }

  // Disks
  if (!disks.empty()) {
    std::println("{}── Discos ──{}", no_color ? "" : term::BOLD, no_color ? "" : term::RESET);
    for (const auto& d : disks) {
      std::println("  {:<25} {:>8} de {:<8} ({})",
        d.mount_point,
        fmt_size(d.used_bytes(), no_color),
        fmt_size(d.total_bytes, no_color),
        fmt_pct(d.used_pct(), no_color));
    }
  }

  std::println("");  // trailing newline
}

// ── JSON output ──
void display_json(const SysInfo& si, const std::vector<DiskUsage>& disks) {
  std::println("{{");
  std::println("  \"hostname\": \"{}\",", si.hostname);
  std::println("  \"kernel\": \"{}\",", si.kernel_version);
  std::println("  \"uptime_seconds\": {},", si.uptime.count());
  std::println("  \"cpu\": {{");
  std::println("    \"model\": \"{}\",", si.cpu.model_name);
  std::println("    \"cores\": {},", si.cpu.cores);
  std::println("    \"threads\": {},", si.cpu.threads);
  std::println("    \"freq_mhz\": {:.1f}", si.cpu.freq_mhz);
  std::println("  }},");
  std::println("  \"memory\": {{");
  std::println("    \"total_kb\": {},", si.mem.total_kb);
  std::println("    \"available_kb\": {},", si.mem.avail_kb);
  std::println("    \"used_percent\": {:.1f}", si.mem.used_pct());
  std::println("  }},");
  std::println("  \"temperatures\": [");
  for (size_t i = 0; i < si.temps.size(); ++i) {
    std::print("    {{\"name\": \"{}\", \"celsius\": {:.1f}}}", si.temps[i].name, si.temps[i].temp_celsius);
    if (i < si.temps.size() - 1) std::println(",");
    else std::println("");
  }
  std::println("  ],");
  std::println("  \"disks\": [");
  for (size_t i = 0; i < disks.size(); ++i) {
    std::print("    {{\"mount\": \"{}\", \"total_bytes\": {}, \"free_bytes\": {}}}",
      disks[i].mount_point, disks[i].total_bytes, disks[i].free_bytes);
    if (i < disks.size() - 1) std::println(",");
    else std::println("");
  }
  std::println("  ]");
  std::println("}}");
}

// ── Gather all system info ──
[[nodiscard]] SysInfo gather_all() {
  SysInfo si;

  if (auto r = procfs::read_all()) si = std::move(*r);
  else spdlog::error("procfs::read_all: {}", r.error().message);

  // Thermal zones via sysfs
  if (auto r = sysfs::read_thermal_zones()) si.temps = std::move(*r);
  else spdlog::warn("sysfs thermal: {}", r.error().message);

  return si;
}

// ── Main ──
int main(int argc, char** argv) {
  // ── Signal handlers for clean exit ──
  std::signal(SIGINT,  handle_signal);
  std::signal(SIGTERM, handle_signal);

  // ── Logging ──
  try {
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger  = std::make_shared<spdlog::logger>("sysmon", console);
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
  } catch (...) {
    // Fallback: no logging
  }

  spdlog::info("linux-sysmon v{} iniciado", VERSION);

  // ── Parse CLI ──
  auto args = parse_args(argc, argv);
  if (!args.errors.empty()) {
    for (const auto& e : args.errors) {
      std::println(stderr, "Error: {}", e);
    }
    std::println(stderr, "Usa --help para ayuda.");
    return 1;
  }

  // ── Main loop ──
  do {
    auto si    = gather_all();
    auto disks = disk::read_all().value_or(std::vector<DiskUsage>{});

    if (args.json) {
      display_json(si, disks);
    } else {
      if (args.monitor) std::print("{}", terminal::ansi::CLEAR);
      display_info(si, disks, args.no_color);
    }

    if (args.monitor && g_running) {
      std::this_thread::sleep_for(std::chrono::seconds(args.interval));
    }
  } while (args.monitor && g_running);

  spdlog::info("linux-sysmon finalizado");
  return 0;
}
