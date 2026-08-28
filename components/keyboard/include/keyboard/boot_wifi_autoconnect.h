#pragma once

#include <cstdint>

namespace ai_keyboard {

constexpr std::uint32_t kBootWifiAutoconnectWindowMs = 60U * 1000U;
constexpr std::uint32_t kBootWifiAutoconnectRetryMs = 10U * 1000U;

enum class BootWifiAutoconnectState : std::uint8_t {
  Pending,
  Active,
  TimedOut,
  Connected,
  Bypassed,
};

struct BootWifiAutoconnectInputs {
  BootWifiAutoconnectState state = BootWifiAutoconnectState::Pending;
  bool credentials_configured = false;
  bool connected = false;
  bool explicit_reconnect = false;
  std::uint32_t elapsed_ms = 0U;
  std::uint32_t since_last_attempt_ms = 0U;
};

struct BootWifiAutoconnectDecision {
  BootWifiAutoconnectState next_state =
      BootWifiAutoconnectState::Pending;
  bool attempt_connect = false;
  bool stop_wifi = false;
  bool suppress_automatic_connect = false;
};

BootWifiAutoconnectDecision evaluate_boot_wifi_autoconnect(
    const BootWifiAutoconnectInputs& inputs);

}  // namespace ai_keyboard
