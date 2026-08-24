#include "platform/music_player.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "keyboard/board_pins.h"

namespace easy_input {
namespace {

constexpr const char* kTag = "music_player";
constexpr std::uint32_t kSampleRate = 48000;
constexpr std::size_t kFrameSamples = 960;
constexpr std::size_t kDmaDescriptorCount = 8;
constexpr std::uint32_t kWriteTimeoutMs = 60;

TickType_t delay_ticks(std::uint32_t milliseconds) {
  const auto value = pdMS_TO_TICKS(milliseconds);
  return value == 0 ? 1 : value;
}

}  // namespace

esp_err_t MusicPlayer::begin(TaskHandle_t owner,
                             ai_keyboard::AudioIoArbiter* arbiter) {
  if (initialized_) {
    return ESP_OK;
  }
  if (owner == nullptr || arbiter == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  const esp_err_t library_result = library_.begin();
  if (library_result != ESP_OK || library_.track_count() != 2U) {
    ESP_LOGE(kTag,
             "music library unavailable result=%s tracks=%u",
             esp_err_to_name(library_result),
             static_cast<unsigned>(library_.track_count()));
    library_.close();
    return library_result == ESP_OK ? ESP_ERR_INVALID_RESPONSE : library_result;
  }

  owner_task_ = owner;
  arbiter_ = arbiter;
  const auto task_result = xTaskCreatePinnedToCore(
      &MusicPlayer::task_entry,
      "music_player",
      20U * 1024U,
      this,
      tskIDLE_PRIORITY + 1,
      &worker_task_,
      1);
  if (task_result != pdPASS) {
    library_.close();
    owner_task_ = nullptr;
    arbiter_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  initialized_ = true;
  ESP_LOGI(kTag,
           "offline player ready tracks=%u rate=%lu volume=%u%%",
           static_cast<unsigned>(library_.track_count()),
           static_cast<unsigned long>(kSampleRate),
           static_cast<unsigned>(volume_percent_.load(std::memory_order_relaxed)));
  return ESP_OK;
}

bool MusicPlayer::play_or_next() {
  if (!initialized_ || library_.track_count() == 0U) {
    return false;
  }
  if (last_selected_track_ == static_cast<std::size_t>(-1)) {
    last_selected_track_ = 0U;
  } else {
    last_selected_track_ = (last_selected_track_ + 1U) % library_.track_count();
  }
  requested_track_.store(static_cast<std::uint8_t>(last_selected_track_),
                         std::memory_order_release);
  paused_.store(false, std::memory_order_release);
  stop_requested_.store(false, std::memory_order_release);
  playback_requested_.store(true, std::memory_order_release);
  command_sequence_.fetch_add(1U, std::memory_order_acq_rel);
  if (worker_task_ != nullptr) {
    xTaskNotifyGive(worker_task_);
  }
  if (owner_task_ != nullptr) {
    xTaskNotifyGive(owner_task_);
  }
  return true;
}

void MusicPlayer::stop() {
  playback_requested_.store(false, std::memory_order_release);
  paused_.store(false, std::memory_order_release);
  stop_requested_.store(true, std::memory_order_release);
  command_sequence_.fetch_add(1U, std::memory_order_acq_rel);
  visual_rms_.store(0, std::memory_order_relaxed);
  visual_beat_.store(0, std::memory_order_relaxed);
  visual_pending_.store(true, std::memory_order_release);
  if (worker_task_ != nullptr) {
    xTaskNotifyGive(worker_task_);
  }
  if (owner_task_ != nullptr) {
    xTaskNotifyGive(owner_task_);
  }
}

void MusicPlayer::set_paused(bool paused) {
  if (!initialized_ || !playback_requested_.load(std::memory_order_acquire)) {
    return;
  }
  paused_.store(paused, std::memory_order_release);
  if (worker_task_ != nullptr) {
    xTaskNotifyGive(worker_task_);
  }
}

void MusicPlayer::set_volume_percent(std::uint8_t volume_percent) {
  volume_percent_.store(std::min<std::uint8_t>(volume_percent, 100),
                        std::memory_order_relaxed);
}

bool MusicPlayer::active() const {
  return arbiter_generation_ != 0U;
}

bool MusicPlayer::sleep_blocked() const {
  return active() || playback_requested_.load(std::memory_order_acquire);
}

bool MusicPlayer::power_required() const {
  return active();
}

void MusicPlayer::notify_power_ready() {
  if (!active()) {
    return;
  }
  if (!power_ready_.exchange(true, std::memory_order_acq_rel) &&
      worker_task_ != nullptr) {
    xTaskNotifyGive(worker_task_);
  }
}

bool MusicPlayer::poll() {
  bool changed = false;
  const auto completed =
      completed_generation_.exchange(0, std::memory_order_acq_rel);
  if (completed != 0U && completed == arbiter_generation_) {
    if (arbiter_ != nullptr && !arbiter_->finish_speaker(completed)) {
      ESP_LOGE(kTag,
               "speaker ownership release failed generation=%lu",
               static_cast<unsigned long>(completed));
    }
    arbiter_generation_ = 0U;
    power_ready_.store(false, std::memory_order_release);
    changed = true;
  }
  if (arbiter_generation_ == 0U &&
      playback_requested_.load(std::memory_order_acquire) &&
      !stop_requested_.load(std::memory_order_acquire)) {
    changed = start_generation() || changed;
  }
  return changed;
}

bool MusicPlayer::take_visual(std::uint16_t* rms_milli,
                              std::uint16_t* beat_milli) {
  if (!visual_pending_.exchange(false, std::memory_order_acq_rel)) {
    return false;
  }
  if (rms_milli != nullptr) {
    *rms_milli = visual_rms_.load(std::memory_order_relaxed);
  }
  if (beat_milli != nullptr) {
    *beat_milli = visual_beat_.load(std::memory_order_relaxed);
  }
  return true;
}

bool MusicPlayer::start_generation() {
  if (arbiter_ == nullptr || arbiter_generation_ != 0U) {
    return false;
  }
  ++next_generation_;
  if (next_generation_ == 0U) {
    ++next_generation_;
  }
  if (!arbiter_->try_begin_speaker(next_generation_)) {
    return false;
  }
  arbiter_generation_ = next_generation_;
  power_ready_.store(false, std::memory_order_release);
  worker_generation_.store(arbiter_generation_, std::memory_order_release);
  if (worker_task_ != nullptr) {
    xTaskNotifyGive(worker_task_);
  }
  ESP_LOGI(kTag,
           "speaker reservation accepted generation=%lu track=%u",
           static_cast<unsigned long>(arbiter_generation_),
           static_cast<unsigned>(requested_track_.load()));
  return true;
}

void MusicPlayer::task_entry(void* context) {
  static_cast<MusicPlayer*>(context)->run();
}

void MusicPlayer::run() {
  std::array<std::int16_t, kFrameSamples> output{};
  std::uint32_t active_generation = 0U;
  std::uint32_t observed_command = 0U;
  std::uint32_t smoothed_rms = 0U;
  bool clock_enabled = false;
  bool stream_open = false;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, delay_ticks(10));

    if (active_generation == 0U) {
      const auto requested_generation =
          worker_generation_.load(std::memory_order_acquire);
      if (requested_generation == 0U ||
          !power_ready_.load(std::memory_order_acquire)) {
        continue;
      }
      active_generation = requested_generation;
      observed_command = 0U;
      smoothed_rms = 0U;
    }

    const bool microphone_preempted =
        arbiter_ != nullptr && arbiter_->microphone_requested();
    if (stop_requested_.load(std::memory_order_acquire) ||
        microphone_preempted) {
      const auto interrupted_command =
          command_sequence_.load(std::memory_order_acquire);
      stop_requested_.store(false, std::memory_order_release);
      disable_i2s();
      clock_enabled = false;
      close_i2s();
      decoder_.close();
      stream_open = false;
      visual_rms_.store(0, std::memory_order_relaxed);
      visual_beat_.store(0, std::memory_order_relaxed);
      visual_pending_.store(true, std::memory_order_release);
      const auto completed = active_generation;
      active_generation = 0U;
      worker_generation_.store(0, std::memory_order_release);
      if (command_sequence_.load(std::memory_order_acquire) ==
              interrupted_command ||
          !playback_requested_.load(std::memory_order_acquire)) {
        playback_requested_.store(false, std::memory_order_release);
      }
      finish_generation(completed);
      continue;
    }

    const auto command = command_sequence_.load(std::memory_order_acquire);
    if (command != observed_command) {
      disable_i2s();
      clock_enabled = false;
      decoder_.close();
      stream_open = false;

      music_storage::TrackView track{};
      const auto track_index = requested_track_.load(std::memory_order_acquire);
      if (!library_.track(track_index, &track) ||
          decoder_.open(track.data, track.size) != ESP_OK || !open_i2s()) {
        ESP_LOGE(kTag,
                 "track open failed index=%u bytes=%u decoder_error=%ld",
                 static_cast<unsigned>(track_index),
                 static_cast<unsigned>(track.size),
                 static_cast<long>(decoder_.last_error_code()));
        if (command_sequence_.load(std::memory_order_acquire) == command ||
            !playback_requested_.load(std::memory_order_acquire)) {
          playback_requested_.store(false, std::memory_order_release);
        }
        close_i2s();
        decoder_.close();
        const auto completed = active_generation;
        active_generation = 0U;
        worker_generation_.store(0, std::memory_order_release);
        finish_generation(completed);
        continue;
      }
      observed_command = command;
      smoothed_rms = 0U;
      stream_open = true;
      ESP_LOGI(kTag,
               "track ready index=%u encoded_bytes=%u",
               static_cast<unsigned>(track_index),
               static_cast<unsigned>(track.size));
    }

    if (!stream_open) {
      continue;
    }

    if (paused_.load(std::memory_order_acquire)) {
      if (clock_enabled) {
        output.fill(0);
        (void)write_samples(output.data(), output.size());
        disable_i2s();
        clock_enabled = false;
      }
      visual_rms_.store(0, std::memory_order_relaxed);
      visual_beat_.store(0, std::memory_order_relaxed);
      visual_pending_.store(true, std::memory_order_release);
      continue;
    }

    if (!clock_enabled) {
      if (!enable_i2s_with_silence()) {
        if (command_sequence_.load(std::memory_order_acquire) ==
                observed_command ||
            !playback_requested_.load(std::memory_order_acquire)) {
          playback_requested_.store(false, std::memory_order_release);
        }
        close_i2s();
        decoder_.close();
        stream_open = false;
        const auto completed = active_generation;
        active_generation = 0U;
        worker_generation_.store(0, std::memory_order_release);
        finish_generation(completed);
        continue;
      }
      clock_enabled = true;
    }

    music_player::OggOpusPcmFrame decoded{};
    const auto decode_status = decoder_.decode_next(&decoded);
    if (decode_status == music_player::OggOpusDecodeStatus::Frame &&
        decoded.samples != nullptr && decoded.sample_count != 0U) {
      esp_err_t write_result = ESP_OK;
      for (std::size_t offset = 0U; offset < decoded.sample_count;) {
        if (command_sequence_.load(std::memory_order_acquire) !=
                observed_command ||
            stop_requested_.load(std::memory_order_acquire) ||
            paused_.load(std::memory_order_acquire)) {
          break;
        }
        const auto chunk =
            std::min<std::size_t>(output.size(), decoded.sample_count - offset);
        const auto volume_percent =
            volume_percent_.load(std::memory_order_relaxed);
        for (std::size_t index = 0U; index < chunk; ++index) {
          output[index] = static_cast<std::int16_t>(
              static_cast<std::int32_t>(decoded.samples[offset + index]) *
              volume_percent / 100);
        }
        publish_visual(output.data(), chunk, &smoothed_rms);
        write_result = write_samples(output.data(), chunk);
        if (write_result != ESP_OK) {
          break;
        }
        offset += chunk;
      }
      if (write_result == ESP_OK) {
        continue;
      }
      ESP_LOGE(kTag, "I2S write failed: %s", esp_err_to_name(write_result));
    } else if (decode_status != music_player::OggOpusDecodeStatus::End) {
      ESP_LOGE(kTag,
               "Ogg Opus decode failed error=%ld samples=%u",
               static_cast<long>(decoder_.last_error_code()),
               static_cast<unsigned>(decoded.sample_count));
    }

    const auto latest_command =
        command_sequence_.load(std::memory_order_acquire);
    if (latest_command != observed_command &&
        playback_requested_.load(std::memory_order_acquire)) {
      continue;
    }
    const auto terminal_command =
        command_sequence_.load(std::memory_order_acquire);
    if (terminal_command == observed_command ||
        !playback_requested_.load(std::memory_order_acquire)) {
      playback_requested_.store(false, std::memory_order_release);
    }
    output.fill(0);
    (void)write_samples(output.data(), output.size());
    disable_i2s();
    clock_enabled = false;
    close_i2s();
    decoder_.close();
    stream_open = false;
    visual_rms_.store(0, std::memory_order_relaxed);
    visual_beat_.store(0, std::memory_order_relaxed);
    visual_pending_.store(true, std::memory_order_release);
    const auto completed = active_generation;
    active_generation = 0U;
    worker_generation_.store(0, std::memory_order_release);
    finish_generation(completed);
  }
}

bool MusicPlayer::open_i2s() {
  if (tx_channel_ != nullptr) {
    return true;
  }
  i2s_chan_config_t channel =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  channel.dma_desc_num = kDmaDescriptorCount;
  channel.dma_frame_num = kFrameSamples / 2U;
  const esp_err_t channel_result =
      i2s_new_channel(&channel, &tx_channel_, nullptr);
  if (channel_result != ESP_OK) {
    tx_channel_ = nullptr;
    ESP_LOGE(kTag, "I2S channel create failed: %s",
             esp_err_to_name(channel_result));
    return false;
  }

  i2s_std_config_t config = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
          I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = static_cast<gpio_num_t>(ai_keyboard::kSpkI2sBclkPin),
          .ws = static_cast<gpio_num_t>(ai_keyboard::kSpkI2sWsPin),
          .dout = static_cast<gpio_num_t>(ai_keyboard::kSpkI2sDataOutPin),
          .din = I2S_GPIO_UNUSED,
          .invert_flags = {},
      },
  };
  config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  const esp_err_t init_result =
      i2s_channel_init_std_mode(tx_channel_, &config);
  if (init_result != ESP_OK) {
    i2s_del_channel(tx_channel_);
    tx_channel_ = nullptr;
    ESP_LOGE(kTag, "I2S mode init failed: %s", esp_err_to_name(init_result));
    return false;
  }
  return true;
}

void MusicPlayer::close_i2s() {
  if (tx_channel_ == nullptr) {
    return;
  }
  (void)i2s_channel_disable(tx_channel_);
  (void)i2s_del_channel(tx_channel_);
  tx_channel_ = nullptr;
}

bool MusicPlayer::enable_i2s_with_silence() {
  if (tx_channel_ == nullptr) {
    return false;
  }
  std::array<std::int16_t, kFrameSamples / 2U> silence{};
  std::size_t loaded = 0U;
  const esp_err_t preload_result = i2s_channel_preload_data(
      tx_channel_, silence.data(), sizeof(silence), &loaded);
  if (preload_result != ESP_OK || loaded != sizeof(silence)) {
    ESP_LOGE(kTag,
             "I2S silence preload failed result=%s loaded=%u",
             esp_err_to_name(preload_result),
             static_cast<unsigned>(loaded));
    return false;
  }
  const esp_err_t enable_result = i2s_channel_enable(tx_channel_);
  if (enable_result != ESP_OK && enable_result != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "I2S enable failed: %s", esp_err_to_name(enable_result));
    return false;
  }
  return true;
}

void MusicPlayer::disable_i2s() {
  if (tx_channel_ == nullptr) {
    return;
  }
  const esp_err_t result = i2s_channel_disable(tx_channel_);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "I2S disable failed: %s", esp_err_to_name(result));
  }
}

esp_err_t MusicPlayer::write_samples(const std::int16_t* samples,
                                     std::size_t sample_count) {
  if (tx_channel_ == nullptr || samples == nullptr || sample_count == 0U) {
    return ESP_ERR_INVALID_ARG;
  }
  const auto* data = reinterpret_cast<const std::uint8_t*>(samples);
  const auto bytes = sample_count * sizeof(samples[0]);
  std::size_t offset = 0U;
  while (offset < bytes) {
    std::size_t written = 0U;
    const esp_err_t result = i2s_channel_write(
        tx_channel_, data + offset, bytes - offset, &written, kWriteTimeoutMs);
    if (result != ESP_OK) {
      return result;
    }
    if (written == 0U) {
      return ESP_ERR_TIMEOUT;
    }
    offset += written;
  }
  return ESP_OK;
}

void MusicPlayer::publish_visual(const std::int16_t* samples,
                                 std::size_t sample_count,
                                 std::uint32_t* smoothed_rms) {
  if (samples == nullptr || sample_count == 0U || smoothed_rms == nullptr) {
    return;
  }
  std::uint64_t sum_squares = 0U;
  for (std::size_t index = 0; index < sample_count; ++index) {
    const auto sample = static_cast<std::int32_t>(samples[index]);
    sum_squares += static_cast<std::uint64_t>(sample * sample);
  }
  const auto rms = static_cast<std::uint32_t>(
      std::sqrt(static_cast<double>(sum_squares) /
                static_cast<double>(sample_count)));
  const auto prior = *smoothed_rms;
  *smoothed_rms = prior == 0U ? rms : (prior * 7U + rms) / 8U;
  const auto beat = rms > prior ? std::min<std::uint32_t>(32767U, (rms - prior) * 5U)
                                : 0U;
  visual_rms_.store(static_cast<std::uint16_t>(
                        std::min<std::uint32_t>(
                            1000U, *smoothed_rms * 1000U / 32767U)),
                    std::memory_order_relaxed);
  visual_beat_.store(static_cast<std::uint16_t>(
                         std::min<std::uint32_t>(1000U, beat * 1000U / 32767U)),
                     std::memory_order_relaxed);
  visual_pending_.store(true, std::memory_order_release);
  if (owner_task_ != nullptr) {
    xTaskNotifyGive(owner_task_);
  }
}

void MusicPlayer::finish_generation(std::uint32_t generation) {
  completed_generation_.store(generation, std::memory_order_release);
  if (owner_task_ != nullptr) {
    xTaskNotifyGive(owner_task_);
  }
}

}  // namespace easy_input
