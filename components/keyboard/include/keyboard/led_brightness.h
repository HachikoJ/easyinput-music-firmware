#pragma once

#include <array>
#include <cstdint>

namespace ai_keyboard {

// The single board-wide WS2812 brightness setting. Keep this as a percentage
// so it can be adjusted without changing the LED state or animation logic.
constexpr std::uint8_t kWs2812BrightnessPercent = 35;

struct LedBrightnessPreviewColor {
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
};

constexpr std::array<LedBrightnessPreviewColor, 5>
    kLedBrightnessPreviewColors{{
        {255, 0, 0},
        {255, 80, 0},
        {255, 210, 0},
        {0, 255, 0},
        {0, 0, 255},
    }};

constexpr std::uint8_t clamp_ws2812_brightness_percent(int percent) {
  if (percent <= 0) {
    return 0;
  }
  if (percent >= 100) {
    return 100;
  }
  return static_cast<std::uint8_t>(percent);
}

constexpr std::uint8_t scale_ws2812_channel(
    std::uint8_t channel,
    int brightness_percent = kWs2812BrightnessPercent) {
  const auto percent = clamp_ws2812_brightness_percent(brightness_percent);
  return static_cast<std::uint8_t>(
      (static_cast<unsigned>(channel) * percent) / 100U);
}

}  // namespace ai_keyboard
