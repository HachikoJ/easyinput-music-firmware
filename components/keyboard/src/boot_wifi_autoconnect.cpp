#include "keyboard/boot_wifi_autoconnect.h"

namespace ai_keyboard {

BootWifiAutoconnectDecision evaluate_boot_wifi_autoconnect(
    const BootWifiAutoconnectInputs& inputs) {
  BootWifiAutoconnectDecision decision{};
  decision.next_state = inputs.state;

  if (inputs.explicit_reconnect) {
    decision.next_state = BootWifiAutoconnectState::Bypassed;
    decision.attempt_connect = inputs.credentials_configured;
    return decision;
  }

  if (inputs.connected) {
    decision.next_state = BootWifiAutoconnectState::Connected;
    return decision;
  }

  if (!inputs.credentials_configured) {
    decision.next_state = BootWifiAutoconnectState::TimedOut;
    decision.stop_wifi = inputs.state == BootWifiAutoconnectState::Active;
    decision.suppress_automatic_connect = true;
    return decision;
  }

  if (inputs.state == BootWifiAutoconnectState::Pending) {
    decision.next_state = BootWifiAutoconnectState::Active;
    decision.attempt_connect = true;
    return decision;
  }

  if (inputs.state == BootWifiAutoconnectState::Active &&
      inputs.elapsed_ms >= kBootWifiAutoconnectWindowMs) {
    decision.next_state = BootWifiAutoconnectState::TimedOut;
    decision.stop_wifi = true;
    decision.suppress_automatic_connect = true;
    return decision;
  }

  if (inputs.state == BootWifiAutoconnectState::Active &&
      inputs.since_last_attempt_ms >= kBootWifiAutoconnectRetryMs) {
    decision.attempt_connect = true;
    return decision;
  }

  if (inputs.state == BootWifiAutoconnectState::TimedOut) {
    decision.suppress_automatic_connect = true;
  }
  return decision;
}

}  // namespace ai_keyboard
