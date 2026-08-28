#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "driver/i2s_types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "keyboard/audio_io_arbiter.h"

namespace easy_input::online_music {

enum class OnlineMusicPlaybackResult : std::uint8_t {
  Failed,
  Completed,
};

class OnlineMusicStream {
 public:
  esp_err_t begin(TaskHandle_t owner, ai_keyboard::AudioIoArbiter* arbiter);

  bool play(const std::string& url);
  void stop();
  void set_paused(bool paused);
  void set_volume_percent(std::uint8_t volume_percent);

  bool active() const;
  bool sleep_blocked() const;
  bool power_required() const;
  void notify_power_ready();
  bool poll();
  bool take_started();
  bool take_visual(std::uint16_t* rms_milli, std::uint16_t* beat_milli);
  // Returns the terminal result for the current generation. Completed means
  // the decoder reached the HTTP end-of-stream; Failed covers transport,
  // decoder, and cancellation errors. The bool overload below is retained
  // for callers that only need the legacy success bit.
  bool take_result(OnlineMusicPlaybackResult* result);
  bool take_result(bool* succeeded);

 private:
  static void task_entry(void* context);
  bool ensure_worker();
  void run();
  bool start_generation();
  bool open_i2s(std::uint32_t sample_rate, std::uint8_t channels);
  void close_i2s();
  bool enable_i2s_with_silence();
  void disable_i2s();
  esp_err_t write_samples(const std::int16_t* samples,
                          std::size_t sample_count);
  void publish_visual(const std::int16_t* samples,
                      std::size_t sample_count,
                      std::uint32_t* smoothed_rms);
  void finish_generation(std::uint32_t generation, bool succeeded);

  TaskHandle_t owner_task_ = nullptr;
  TaskHandle_t worker_task_ = nullptr;
  ai_keyboard::AudioIoArbiter* arbiter_ = nullptr;
  i2s_chan_handle_t tx_channel_ = nullptr;

  std::string requested_url_;
  SemaphoreHandle_t command_mutex_ = nullptr;
  std::atomic<bool> playback_requested_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> power_ready_{false};
  std::atomic<std::uint8_t> volume_percent_{15};
  std::atomic<std::uint32_t> command_sequence_{0};
  std::atomic<std::uint32_t> worker_generation_{0};
  std::atomic<std::uint32_t> completed_generation_{0};
  std::atomic<OnlineMusicPlaybackResult> completed_result_{
      OnlineMusicPlaybackResult::Failed};
  std::atomic<std::uint16_t> visual_rms_{0};
  std::atomic<std::uint16_t> visual_beat_{0};
  std::atomic<bool> visual_pending_{false};
  std::atomic<bool> started_pending_{false};
  std::atomic<bool> result_pending_{false};

  std::uint32_t next_generation_ = 0;
  std::uint32_t arbiter_generation_ = 0;
  bool initialized_ = false;
};

}  // namespace easy_input::online_music
