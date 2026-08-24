#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "driver/i2s_types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keyboard/audio_io_arbiter.h"
#include "music_player/music_library.h"
#include "music_player/ogg_opus_decoder.h"

namespace easy_input {

class MusicPlayer {
 public:
  esp_err_t begin(TaskHandle_t owner, ai_keyboard::AudioIoArbiter* arbiter);

  bool play_or_next();
  void stop();
  void set_paused(bool paused);
  void set_volume_percent(std::uint8_t volume_percent);

  bool active() const;
  bool sleep_blocked() const;
  bool power_required() const;
  void notify_power_ready();
  bool poll();
  bool take_visual(std::uint16_t* rms_milli, std::uint16_t* beat_milli);

 private:
  static void task_entry(void* context);
  void run();
  bool start_generation();
  bool open_i2s();
  void close_i2s();
  bool enable_i2s_with_silence();
  void disable_i2s();
  esp_err_t write_samples(const std::int16_t* samples,
                          std::size_t sample_count);
  void publish_visual(const std::int16_t* samples,
                      std::size_t sample_count,
                      std::uint32_t* smoothed_rms);
  void finish_generation(std::uint32_t generation);

  TaskHandle_t owner_task_ = nullptr;
  TaskHandle_t worker_task_ = nullptr;
  ai_keyboard::AudioIoArbiter* arbiter_ = nullptr;
  music_storage::MusicLibrary library_;
  music_player::OggOpusDecoder decoder_;
  i2s_chan_handle_t tx_channel_ = nullptr;

  std::atomic<bool> playback_requested_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> power_ready_{false};
  std::atomic<std::uint8_t> volume_percent_{15};
  std::atomic<std::uint8_t> requested_track_{0};
  std::atomic<std::uint32_t> command_sequence_{0};
  std::atomic<std::uint32_t> worker_generation_{0};
  std::atomic<std::uint32_t> completed_generation_{0};
  std::atomic<std::uint16_t> visual_rms_{0};
  std::atomic<std::uint16_t> visual_beat_{0};
  std::atomic<bool> visual_pending_{false};

  std::size_t last_selected_track_ = static_cast<std::size_t>(-1);
  std::uint32_t next_generation_ = 0;
  std::uint32_t arbiter_generation_ = 0;
  bool initialized_ = false;
};

}  // namespace easy_input
