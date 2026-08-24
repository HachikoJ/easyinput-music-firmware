#pragma once

#include <cstdint>

#include "keyboard/led_brightness.h"

namespace ai_keyboard {

constexpr std::uint32_t kLedBrightnessDoubleClickMs = 350;
constexpr std::uint32_t kLedBrightnessModeTimeoutMs = 15000;
constexpr int kLedBrightnessStepPercent = 5;

enum class LedBrightnessActionKind : std::uint8_t {
  None,
  DispatchClick,
  Entered,
  Changed,
  Saved,
  Cancelled,
};

struct LedBrightnessAction {
  LedBrightnessActionKind kind = LedBrightnessActionKind::None;
  std::uint8_t brightness_percent = kWs2812BrightnessPercent;
};

class LedBrightnessMode {
 public:
  LedBrightnessAction press(std::uint32_t now_ms);
  LedBrightnessAction short_release(std::uint32_t now_ms,
                                    std::uint8_t current_percent);
  LedBrightnessAction rotate(int detent_delta, std::uint32_t now_ms);
  LedBrightnessAction update(std::uint32_t now_ms);

  void cancel_click_sequence();
  // Abort both the pending double-click gesture and an active preview.
  void reset();
  bool active() const;
  bool interaction_pending() const;
  bool next_deadline_ms(std::uint32_t* deadline_ms) const;
  std::uint8_t preview_percent() const;

 private:
  bool click_pending_ = false;
  bool second_press_armed_ = false;
  bool active_ = false;
  std::uint32_t first_release_ms_ = 0;
  std::uint32_t last_activity_ms_ = 0;
  std::uint8_t original_percent_ = kWs2812BrightnessPercent;
  std::uint8_t preview_percent_ = kWs2812BrightnessPercent;
};

}  // namespace ai_keyboard
