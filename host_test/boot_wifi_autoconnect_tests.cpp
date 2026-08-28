#include <cassert>

#include "keyboard/boot_wifi_autoconnect.h"

int main() {
  using ai_keyboard::BootWifiAutoconnectInputs;
  using ai_keyboard::BootWifiAutoconnectState;
  using ai_keyboard::evaluate_boot_wifi_autoconnect;

  auto pending = evaluate_boot_wifi_autoconnect({});
  assert(pending.next_state == BootWifiAutoconnectState::TimedOut);
  assert(!pending.attempt_connect);
  assert(pending.suppress_automatic_connect);

  BootWifiAutoconnectInputs missing_password{};
  missing_password.state = BootWifiAutoconnectState::Active;
  missing_password.since_last_attempt_ms =
      ai_keyboard::kBootWifiAutoconnectRetryMs;
  auto missing = evaluate_boot_wifi_autoconnect(missing_password);
  assert(missing.next_state == BootWifiAutoconnectState::TimedOut);
  assert(!missing.attempt_connect);
  assert(missing.stop_wifi);

  BootWifiAutoconnectInputs retry{};
  retry.state = BootWifiAutoconnectState::Active;
  retry.credentials_configured = true;
  retry.since_last_attempt_ms =
      ai_keyboard::kBootWifiAutoconnectRetryMs - 1U;
  retry.elapsed_ms = 50U * 1000U;
  auto before_retry = evaluate_boot_wifi_autoconnect(retry);
  assert(before_retry.next_state == BootWifiAutoconnectState::Active);
  assert(!before_retry.attempt_connect);
  assert(!before_retry.stop_wifi);

  retry.since_last_attempt_ms =
      ai_keyboard::kBootWifiAutoconnectRetryMs;
  auto retry_due = evaluate_boot_wifi_autoconnect(retry);
  assert(retry_due.next_state == BootWifiAutoconnectState::Active);
  assert(retry_due.attempt_connect);
  assert(!retry_due.stop_wifi);

  retry.elapsed_ms = ai_keyboard::kBootWifiAutoconnectWindowMs;
  auto timed_out = evaluate_boot_wifi_autoconnect(retry);
  assert(timed_out.next_state == BootWifiAutoconnectState::TimedOut);
  assert(!timed_out.attempt_connect);
  assert(timed_out.stop_wifi);
  assert(timed_out.suppress_automatic_connect);

  BootWifiAutoconnectInputs connected{};
  connected.state = BootWifiAutoconnectState::Active;
  connected.credentials_configured = true;
  connected.connected = true;
  auto success = evaluate_boot_wifi_autoconnect(connected);
  assert(success.next_state == BootWifiAutoconnectState::Connected);
  assert(!success.attempt_connect);
  assert(!success.stop_wifi);

  BootWifiAutoconnectInputs expired{};
  expired.state = BootWifiAutoconnectState::TimedOut;
  expired.credentials_configured = true;
  expired.since_last_attempt_ms =
      ai_keyboard::kBootWifiAutoconnectRetryMs;
  auto suppressed = evaluate_boot_wifi_autoconnect(expired);
  assert(!suppressed.attempt_connect);
  assert(suppressed.suppress_automatic_connect);

  expired.explicit_reconnect = true;
  auto explicit_retry = evaluate_boot_wifi_autoconnect(expired);
  assert(explicit_retry.next_state == BootWifiAutoconnectState::Bypassed);
  assert(explicit_retry.attempt_connect);
  assert(!explicit_retry.suppress_automatic_connect);

  return 0;
}
