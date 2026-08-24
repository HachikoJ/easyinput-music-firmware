#include <cassert>

#include "keyboard/led_brightness.h"

namespace {

void default_brightness_is_thirty_five_percent() {
  static_assert(ai_keyboard::kWs2812BrightnessPercent == 35);
  assert(ai_keyboard::scale_ws2812_channel(200) == 70);
}

void brightness_clamps_to_black_and_full_scale() {
  assert(ai_keyboard::clamp_ws2812_brightness_percent(-1) == 0);
  assert(ai_keyboard::clamp_ws2812_brightness_percent(0) == 0);
  assert(ai_keyboard::scale_ws2812_channel(255, -1) == 0);
  assert(ai_keyboard::scale_ws2812_channel(255, 0) == 0);

  assert(ai_keyboard::clamp_ws2812_brightness_percent(100) == 100);
  assert(ai_keyboard::clamp_ws2812_brightness_percent(101) == 100);
  assert(ai_keyboard::scale_ws2812_channel(255, 100) == 255);
  assert(ai_keyboard::scale_ws2812_channel(255, 101) == 255);
}

void scaling_uses_integer_truncation_at_rounding_boundaries() {
  assert(ai_keyboard::scale_ws2812_channel(1, 35) == 0);
  assert(ai_keyboard::scale_ws2812_channel(3, 35) == 1);
  assert(ai_keyboard::scale_ws2812_channel(255, 35) == 89);
  assert(ai_keyboard::scale_ws2812_channel(255, 99) == 252);
}

void preview_uses_fixed_red_orange_yellow_green_blue_pixels() {
  constexpr auto colors = ai_keyboard::kLedBrightnessPreviewColors;
  static_assert(colors.size() == 5);
  static_assert(colors[0].red == 255 && colors[0].green == 0);
  static_assert(colors[1].red == 255 && colors[1].green == 80);
  static_assert(colors[2].red == 255 && colors[2].green == 210);
  static_assert(colors[3].green == 255 && colors[3].blue == 0);
  static_assert(colors[4].red == 0 && colors[4].blue == 255);
}

}  // namespace

int main() {
  default_brightness_is_thirty_five_percent();
  brightness_clamps_to_black_and_full_scale();
  scaling_uses_integer_truncation_at_rounding_boundaries();
  preview_uses_fixed_red_orange_yellow_green_blue_pixels();
  return 0;
}
