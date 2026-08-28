#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "keyboard/agent_status.h"
#include "keyboard/board_pins.h"
#include "keyboard/boot_led_sequence.h"
#include "keyboard/input_feedback.h"
#include "keyboard/keymap.h"
#include "keyboard/online_music_progress.h"

namespace easy_input {

struct Rgb {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
};

enum class StatusLedEvent {
  BleConnected,
  BleDisconnected,
  UsbConnected,
  UsbDisconnected,
  ConfigMode,
  PlatformMacOS,
  PlatformWindows,
  SaveFailed,
  OnlineMusicRecognitionFailed,
  OnlineMusicPlaybackFailed,
};

class StatusLedStrip {
 public:
  esp_err_t begin();
  bool ready() const;

  void clear();
  esp_err_t prepare_for_deep_sleep();
  void reserve_cold_boot_sequence();
  void start_cold_boot_sequence(std::uint32_t now_ms);
  bool cold_boot_sequence_active() const;
  void show_raw_color(Rgb color);
  void show_pixel(std::size_t index, Rgb color);
  void set_brightness_percent(std::uint8_t brightness_percent);
  void show_brightness_preview();
  void end_brightness_preview();
  void set_agent_status(const ai_keyboard::AgentStatusCommand& command,
                        std::uint32_t now_ms);
  void show_scroll_event(std::int8_t vertical,
                         std::int8_t horizontal,
                         std::uint32_t now_ms);
  void show_status_event(StatusLedEvent event, std::uint32_t now_ms);
  void show_input_event(ai_keyboard::InputId input,
                        ai_keyboard::InputPhase phase,
                        std::uint32_t now_ms);
  void set_music_visual(bool active, bool paused, std::uint32_t now_ms);
  void set_music_audio_level(std::uint16_t rms_milli,
                             std::uint16_t beat_milli,
                             std::uint32_t now_ms);
  void set_online_music_progress(
      const ai_keyboard::OnlineMusicProgress& progress,
      std::uint32_t now_ms);
  void update(std::uint32_t now_ms);
  bool next_update_deadline_ms(std::uint32_t now_ms,
                               std::uint32_t* deadline_ms) const;

 private:
  void show_feedback(const ai_keyboard::InputActivityFeedback& feedback,
                     std::uint32_t now_ms,
                     bool replay_full_duration_after_boot = false);
  void activate_feedback(const ai_keyboard::InputActivityFeedback& feedback,
                         std::uint32_t now_ms,
                         std::uint32_t effect_until_ms);
  bool update_active_feedback(std::uint32_t now_ms);
  void apply_cold_boot_frame(const ai_keyboard::BootLedFrame& frame,
                             std::uint32_t now_ms);
  void release_cold_boot_visual_ownership(std::uint32_t now_ms);
  bool agent_status_valid(std::uint32_t now_ms) const;
  void render_background_status(std::uint32_t now_ms);
  void render_agent_status();
  void render_online_music_progress(std::uint32_t now_ms);
  void render_idle_status();
  void set_all(Rgb color);
  void render_active_effect();
  void render_solid_effect();
  void render_light_bar_ripple_effect();
  void render_directional_flow_effect();
  void render_confirm_pulse_effect();
  void render_rainbow_marquee_effect();
  esp_err_t flush();

  rmt_channel_handle_t rmt_channel_ = nullptr;
  rmt_encoder_handle_t rmt_copy_encoder_ = nullptr;
  std::array<Rgb, ai_keyboard::kWs2812Count> leds_ = {};
  ai_keyboard::InputActivityFeedback active_feedback_;
  std::uint32_t effect_until_ms_ = 0;
  std::uint32_t last_frame_ms_ = 0;
  std::uint8_t cursor_ = 0;
  ai_keyboard::BootLedSequence cold_boot_sequence_;
  ai_keyboard::BootLedDeferredFeedback deferred_feedback_;
  ai_keyboard::AgentStatusCommand agent_status_;
  std::uint32_t agent_status_expires_ms_ = 0;
  bool agent_status_active_ = false;
  bool agent_status_rendered_ = false;
  bool idle_rendered_ = false;
  std::uint8_t brightness_percent_ = 35;
  bool brightness_preview_active_ = false;
  bool music_visual_active_ = false;
  bool music_visual_paused_ = false;
  std::uint16_t music_rms_milli_ = 0;
  std::uint16_t music_beat_milli_ = 0;
  std::uint32_t music_last_frame_ms_ = 0;
  std::uint8_t music_color_phase_ = 0;
  ai_keyboard::OnlineMusicProgress online_music_progress_;
};

}  // namespace easy_input
