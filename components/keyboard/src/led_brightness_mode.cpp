#include "keyboard/led_brightness_mode.h"

namespace ai_keyboard {
namespace {

LedBrightnessAction action(LedBrightnessActionKind kind,
                           std::uint8_t brightness_percent) {
  return {kind, brightness_percent};
}

}  // namespace

LedBrightnessAction LedBrightnessMode::press(std::uint32_t now_ms) {
  if (active_) {
    last_activity_ms_ = now_ms;
    return {};
  }

  LedBrightnessAction result;
  if (click_pending_ &&
      static_cast<std::uint32_t>(now_ms - first_release_ms_) <=
          kLedBrightnessDoubleClickMs) {
    second_press_armed_ = true;
  } else {
    if (click_pending_) {
      result = action(LedBrightnessActionKind::DispatchClick,
                      preview_percent_);
    }
    click_pending_ = false;
    second_press_armed_ = false;
  }
  return result;
}

LedBrightnessAction LedBrightnessMode::short_release(
    std::uint32_t now_ms,
    std::uint8_t current_percent) {
  if (active_) {
    active_ = false;
    click_pending_ = false;
    second_press_armed_ = false;
    return action(LedBrightnessActionKind::Saved, preview_percent_);
  }

  if (second_press_armed_) {
    click_pending_ = false;
    second_press_armed_ = false;
    active_ = true;
    original_percent_ = clamp_ws2812_brightness_percent(current_percent);
    preview_percent_ = original_percent_;
    last_activity_ms_ = now_ms;
    return action(LedBrightnessActionKind::Entered, preview_percent_);
  }

  click_pending_ = true;
  first_release_ms_ = now_ms;
  preview_percent_ = clamp_ws2812_brightness_percent(current_percent);
  return {};
}

LedBrightnessAction LedBrightnessMode::rotate(int detent_delta,
                                              std::uint32_t now_ms) {
  if (!active_ || detent_delta == 0) {
    return {};
  }
  const int adjusted = static_cast<int>(preview_percent_) +
                       detent_delta * kLedBrightnessStepPercent;
  preview_percent_ = clamp_ws2812_brightness_percent(adjusted);
  last_activity_ms_ = now_ms;
  return action(LedBrightnessActionKind::Changed, preview_percent_);
}

LedBrightnessAction LedBrightnessMode::update(std::uint32_t now_ms) {
  if (active_ &&
      static_cast<std::uint32_t>(now_ms - last_activity_ms_) >=
          kLedBrightnessModeTimeoutMs) {
    active_ = false;
    return action(LedBrightnessActionKind::Cancelled, original_percent_);
  }
  if (click_pending_ && !second_press_armed_ &&
      static_cast<std::uint32_t>(now_ms - first_release_ms_) >=
          kLedBrightnessDoubleClickMs) {
    click_pending_ = false;
    return action(LedBrightnessActionKind::DispatchClick,
                  preview_percent_);
  }
  return {};
}

void LedBrightnessMode::cancel_click_sequence() {
  click_pending_ = false;
  second_press_armed_ = false;
}

void LedBrightnessMode::reset() {
  click_pending_ = false;
  second_press_armed_ = false;
  active_ = false;
  last_activity_ms_ = 0;
}

bool LedBrightnessMode::active() const {
  return active_;
}

bool LedBrightnessMode::interaction_pending() const {
  return active_ || click_pending_ || second_press_armed_;
}

bool LedBrightnessMode::next_deadline_ms(std::uint32_t* deadline_ms) const {
  if (deadline_ms == nullptr) {
    return false;
  }
  if (active_) {
    *deadline_ms = last_activity_ms_ + kLedBrightnessModeTimeoutMs;
    return true;
  }
  if (click_pending_ && !second_press_armed_) {
    *deadline_ms = first_release_ms_ + kLedBrightnessDoubleClickMs;
    return true;
  }
  return false;
}

std::uint8_t LedBrightnessMode::preview_percent() const {
  return preview_percent_;
}

}  // namespace ai_keyboard
