#pragma once

// ──────────────────────────────────────────────────────
// terminal.hpp
// Terminal utilities using ioctl(2) and ANSI escape codes
// ──────────────────────────────────────────────────────

#include <linux_sysmon/sysmon.hpp>

#include <expected>
#include <string>
#include <cstdint>

#include <unistd.h>
#include <sys/ioctl.h>

namespace linux_sysmon::terminal {

// ── Terminal dimensions via TIOCGWINSZ ioctl ──
struct Winsize {
  unsigned short rows = 0;
  unsigned short cols = 0;
};

[[nodiscard]] inline Result<Winsize> get_winsize(int fd = STDOUT_FILENO) {
  struct winsize ws{};
  if (::ioctl(fd, TIOCGWINSZ, &ws) < 0)
    return std::unexpected(make_error(errno,
      std::format("ioctl(TIOCGWINSZ) failed: {}", ::strerror(errno))));
  return Winsize{ws.ws_row, ws.ws_col};
}

// ── ANSI escape helpers ──
namespace ansi {
  inline constexpr auto RESET     = "\033[0m";
  inline constexpr auto BOLD      = "\033[1m";
  inline constexpr auto DIM       = "\033[2m";

  // Foreground colors
  inline constexpr auto BLACK     = "\033[30m";
  inline constexpr auto RED       = "\033[31m";
  inline constexpr auto GREEN     = "\033[32m";
  inline constexpr auto YELLOW    = "\033[33m";
  inline constexpr auto BLUE      = "\033[34m";
  inline constexpr auto MAGENTA   = "\033[35m";
  inline constexpr auto CYAN      = "\033[36m";
  inline constexpr auto WHITE     = "\033[37m";

  // Bright foreground
  inline constexpr auto BRIGHT_BLACK  = "\033[90m";
  inline constexpr auto BRIGHT_RED    = "\033[91m";
  inline constexpr auto BRIGHT_GREEN  = "\033[92m";
  inline constexpr auto BRIGHT_YELLOW = "\033[93m";
  inline constexpr auto BRIGHT_BLUE   = "\033[94m";
  inline constexpr auto BRIGHT_CYAN   = "\033[96m";

  // Cursor
  inline constexpr auto CLEAR_LINE = "\033[K";
  inline constexpr auto CLEAR      = "\033[2J\033[H";
  inline constexpr auto CURSOR_UP  = "\033[A";
  inline constexpr auto CURSOR_HIDE = "\033[?25l";
  inline constexpr auto CURSOR_SHOW = "\033[?25h";

  // ── Colorize a value based on threshold ──
  // Usage: std::print("{}", color_pct(75.0));
  [[nodiscard]] inline std::string color_pct(double pct) {
    if (pct > 90.0) return RED;
    if (pct > 75.0) return YELLOW;
    return GREEN;
  }
} // namespace ansi

} // namespace linux_sysmon::terminal
