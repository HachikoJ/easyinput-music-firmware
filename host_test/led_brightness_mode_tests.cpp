#include <cassert>

#include "keyboard/led_brightness_mode.h"

namespace {

using ai_keyboard::LedBrightnessActionKind;
using ai_keyboard::LedBrightnessMode;

void single_click_dispatches_after_double_click_window() {
  LedBrightnessMode mode;
  assert(mode.press(10).kind == LedBrightnessActionKind::None);
  assert(mode.short_release(20, 35).kind == LedBrightnessActionKind::None);
  assert(mode.update(369).kind == LedBrightnessActionKind::None);
  assert(mode.update(370).kind == LedBrightnessActionKind::DispatchClick);
}

void second_click_enters_within_three_hundred_fifty_ms() {
  LedBrightnessMode mode;
  mode.press(10);
  mode.short_release(20, 35);
  mode.press(370);
  const auto entered = mode.short_release(390, 35);
  assert(entered.kind == LedBrightnessActionKind::Entered);
  assert(entered.brightness_percent == 35);
  assert(mode.active());
}

void press_after_window_dispatches_first_click_and_starts_a_new_candidate() {
  LedBrightnessMode mode;
  mode.press(10);
  mode.short_release(20, 35);
  assert(mode.press(371).kind == LedBrightnessActionKind::DispatchClick);
  assert(mode.short_release(380, 35).kind == LedBrightnessActionKind::None);
  assert(mode.update(730).kind == LedBrightnessActionKind::DispatchClick);
}

void rotation_changes_five_percent_per_detent_and_clamps() {
  LedBrightnessMode mode;
  mode.press(0);
  mode.short_release(10, 95);
  mode.press(100);
  mode.short_release(110, 95);

  auto changed = mode.rotate(2, 200);
  assert(changed.kind == LedBrightnessActionKind::Changed);
  assert(changed.brightness_percent == 100);
  changed = mode.rotate(-30, 300);
  assert(changed.brightness_percent == 0);
}

void click_saves_and_exits() {
  LedBrightnessMode mode;
  mode.press(0);
  mode.short_release(10, 35);
  mode.press(20);
  mode.short_release(30, 35);
  mode.rotate(1, 40);

  mode.press(50);
  const auto saved = mode.short_release(60, 35);
  assert(saved.kind == LedBrightnessActionKind::Saved);
  assert(saved.brightness_percent == 40);
  assert(!mode.active());
}

void inactivity_cancels_and_restores_original_brightness() {
  LedBrightnessMode mode;
  mode.press(0);
  mode.short_release(10, 65);
  mode.press(20);
  mode.short_release(30, 65);
  mode.rotate(1, 100);

  assert(mode.update(15099).kind == LedBrightnessActionKind::None);
  const auto cancelled = mode.update(15100);
  assert(cancelled.kind == LedBrightnessActionKind::Cancelled);
  assert(cancelled.brightness_percent == 65);
  assert(!mode.active());
}

void long_press_path_can_clear_a_double_click_candidate() {
  LedBrightnessMode mode;
  mode.press(0);
  mode.short_release(10, 35);
  mode.press(20);
  mode.cancel_click_sequence();
  assert(!mode.interaction_pending());
}

}  // namespace

int main() {
  single_click_dispatches_after_double_click_window();
  second_click_enters_within_three_hundred_fifty_ms();
  press_after_window_dispatches_first_click_and_starts_a_new_candidate();
  rotation_changes_five_percent_per_detent_and_clamps();
  click_saves_and_exits();
  inactivity_cancels_and_restores_original_brightness();
  long_press_path_can_clear_a_double_click_candidate();
  return 0;
}
