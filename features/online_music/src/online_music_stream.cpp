#include "online_music/online_music_stream.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "decoder/impl/esp_mp3_dec.h"
#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "esp_audio_simple_dec.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "keyboard/board_pins.h"

namespace easy_input::online_music {
namespace {

constexpr const char* kTag = "online_music";
constexpr std::size_t kHttpReadBytes = 16384U;
// Keep encoded bytes in PSRAM so short network stalls do not immediately
// starve the decoder/I2S path. The buffer is linear and compacted only when
// the consumed prefix leaves insufficient contiguous write space.
constexpr std::size_t kEncodedPrebufferBytes = 512U * 1024U;
constexpr std::size_t kEncodedPrebufferStartBytes = 96U * 1024U;
constexpr std::size_t kEncodedPrebufferRefillBytes = 64U * 1024U;
constexpr std::size_t kInitialPcmBytes = 11520U;
constexpr std::size_t kMaximumPcmBytes = 18432U;
constexpr std::size_t kOutputSamples = 960U;
constexpr std::size_t kDmaDescriptorCount = 8U;
constexpr std::uint32_t kWriteTimeoutMs = 60U;
constexpr std::uint32_t kHttpTimeoutMs = 12000U;
// A blocking esp_http_client_read() returns -ESP_ERR_HTTP_EAGAIN when the
// transport wait expires before any bytes arrive. That is a transient stall,
// not an end-of-stream; allow a bounded number of such gaps before failing.
constexpr std::uint32_t kMaxHttpReadTimeoutRetries = 2U;

TickType_t delay_ticks(std::uint32_t milliseconds) {
  const auto value = pdMS_TO_TICKS(milliseconds);
  return value == 0 ? 1 : value;
}

bool registration_succeeded(esp_audio_err_t result, bool* owns_registration) {
  if (result == ESP_AUDIO_ERR_OK) {
    *owns_registration = true;
    return true;
  }
  if (result == ESP_AUDIO_ERR_ALREADY_EXIST) {
    *owns_registration = false;
    return true;
  }
  return false;
}

}  // namespace

esp_err_t OnlineMusicStream::begin(TaskHandle_t owner,
                                   ai_keyboard::AudioIoArbiter* arbiter) {
  if (initialized_) {
    return ESP_OK;
  }
  if (owner == nullptr || arbiter == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  owner_task_ = owner;
  arbiter_ = arbiter;
  command_mutex_ = xSemaphoreCreateMutex();
  if (command_mutex_ == nullptr) {
    owner_task_ = nullptr;
    arbiter_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  initialized_ = true;
  return ESP_OK;
}

bool OnlineMusicStream::play(const std::string& url) {
  if (!initialized_ || url.empty() || url.size() > 512U) {
    return false;
  }
  if (!ensure_worker()) {
    return false;
  }
  xSemaphoreTake(command_mutex_, portMAX_DELAY);
  requested_url_ = url;
  xSemaphoreGive(command_mutex_);
  paused_.store(false, std::memory_order_release);
  stop_requested_.store(false, std::memory_order_release);
  started_pending_.store(false, std::memory_order_release);
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

bool OnlineMusicStream::ensure_worker() {
  if (worker_task_ != nullptr) {
    return true;
  }
  return xTaskCreatePinnedToCoreWithCaps(
             &OnlineMusicStream::task_entry, "online_music", 24U * 1024U,
             this, tskIDLE_PRIORITY + 1, &worker_task_, 1,
             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS;
}

void OnlineMusicStream::stop() {
  playback_requested_.store(false, std::memory_order_release);
  paused_.store(false, std::memory_order_release);
  stop_requested_.store(true, std::memory_order_release);
  command_sequence_.fetch_add(1U, std::memory_order_acq_rel);
  if (worker_task_ != nullptr) {
    xTaskNotifyGive(worker_task_);
  }
  if (owner_task_ != nullptr) {
    xTaskNotifyGive(owner_task_);
  }
}

void OnlineMusicStream::set_paused(bool paused) {
  paused_.store(paused, std::memory_order_release);
  if (worker_task_ != nullptr) {
    xTaskNotifyGive(worker_task_);
  }
}

void OnlineMusicStream::set_volume_percent(std::uint8_t volume_percent) {
  volume_percent_.store(std::min<std::uint8_t>(volume_percent, 100U),
                        std::memory_order_relaxed);
}

bool OnlineMusicStream::active() const { return arbiter_generation_ != 0U; }

bool OnlineMusicStream::sleep_blocked() const {
  return active() || playback_requested_.load(std::memory_order_acquire);
}

bool OnlineMusicStream::power_required() const { return active(); }

void OnlineMusicStream::notify_power_ready() {
  if (active() && !power_ready_.exchange(true, std::memory_order_acq_rel) &&
      worker_task_ != nullptr) {
    xTaskNotifyGive(worker_task_);
  }
}

bool OnlineMusicStream::poll() {
  bool changed = false;
  const auto completed = completed_generation_.exchange(0U, std::memory_order_acq_rel);
  if (completed != 0U && completed == arbiter_generation_) {
    if (arbiter_ != nullptr) {
      static_cast<void>(arbiter_->finish_speaker(completed));
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

bool OnlineMusicStream::take_started() {
  return started_pending_.exchange(false, std::memory_order_acq_rel);
}

bool OnlineMusicStream::take_visual(std::uint16_t* rms_milli,
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

bool OnlineMusicStream::take_result(bool* succeeded) {
  if (succeeded == nullptr || !result_pending_.exchange(false, std::memory_order_acq_rel)) {
    return false;
  }
  *succeeded = completed_result_.load(std::memory_order_acquire) ==
               OnlineMusicPlaybackResult::Completed;
  return true;
}

bool OnlineMusicStream::take_result(OnlineMusicPlaybackResult* result) {
  if (result == nullptr ||
      !result_pending_.exchange(false, std::memory_order_acq_rel)) {
    return false;
  }
  *result = completed_result_.load(std::memory_order_acquire);
  return true;
}

bool OnlineMusicStream::start_generation() {
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
  return true;
}

void OnlineMusicStream::task_entry(void* context) {
  static_cast<OnlineMusicStream*>(context)->run();
}

void OnlineMusicStream::run() {
  std::array<std::int16_t, kOutputSamples> output{};
  std::uint32_t active_generation = 0U;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, delay_ticks(10));
    if (active_generation == 0U) {
      const auto generation = worker_generation_.load(std::memory_order_acquire);
      if (generation == 0U || !power_ready_.load(std::memory_order_acquire)) {
        continue;
      }
      active_generation = generation;
    }

    const auto command = command_sequence_.load(std::memory_order_acquire);
    std::string url;
    xSemaphoreTake(command_mutex_, portMAX_DELAY);
    url = requested_url_;
    xSemaphoreGive(command_mutex_);
    bool succeeded = false;
    bool decode_failed = false;
    bool playback_started = false;
    bool clock_enabled = false;
    std::uint32_t smoothed_rms = 0U;
    bool owns_mp3_registration = false;
    void* decoder = nullptr;
    std::uint8_t* pcm = nullptr;
    std::size_t pcm_size = 0U;
    std::uint8_t* encoded_prebuffer = nullptr;
    std::size_t encoded_offset = 0U;
    std::size_t encoded_size = 0U;
    esp_http_client_handle_t client = nullptr;

    const auto cleanup = [&]() {
      if (client != nullptr) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        client = nullptr;
      }
      if (clock_enabled) {
        disable_i2s();
      }
      close_i2s();
      if (decoder != nullptr) {
        esp_audio_simple_dec_close(
            static_cast<esp_audio_simple_dec_handle_t>(decoder));
      }
      if (owns_mp3_registration) {
        esp_audio_dec_unregister(ESP_AUDIO_TYPE_MP3);
      }
      heap_caps_free(pcm);
      heap_caps_free(encoded_prebuffer);
    };

    const auto stopped = [&]() {
      return stop_requested_.load(std::memory_order_acquire) ||
             !playback_requested_.load(std::memory_order_acquire) ||
             command_sequence_.load(std::memory_order_acquire) != command ||
             (arbiter_ != nullptr && arbiter_->microphone_requested());
    };

    if (!stopped() && registration_succeeded(esp_mp3_dec_register(), &owns_mp3_registration)) {
      esp_audio_simple_dec_cfg_t decoder_config{};
      decoder_config.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
      decoder_config.use_frame_dec = false;
      auto decoder_handle = static_cast<esp_audio_simple_dec_handle_t>(nullptr);
      if (esp_audio_simple_dec_open(&decoder_config, &decoder_handle) ==
          ESP_AUDIO_ERR_OK) {
        decoder = decoder_handle;
        pcm = static_cast<std::uint8_t*>(
            heap_caps_malloc(kInitialPcmBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        pcm_size = pcm == nullptr ? 0U : kInitialPcmBytes;
      }
    }
    if (decoder == nullptr || pcm == nullptr || stopped()) {
      if (!stopped()) {
        ESP_LOGW(kTag, "playback failed stage=decoder_alloc");
      }
      cleanup();
      finish_generation(active_generation, false);
      active_generation = 0U;
      continue;
    }

    esp_http_client_config_t http_config{};
    http_config.url = url.c_str();
    http_config.timeout_ms = static_cast<int>(kHttpTimeoutMs);
    http_config.disable_auto_redirect = false;
    http_config.max_redirection_count = 3;
    http_config.keep_alive_enable = true;
    http_config.crt_bundle_attach = esp_crt_bundle_attach;
    client = esp_http_client_init(&http_config);
    if (client == nullptr || esp_http_client_open(client, 0) != ESP_OK ||
        esp_http_client_fetch_headers(client) < 0 ||
        esp_http_client_get_status_code(client) != 200) {
      ESP_LOGW(kTag, "playback failed stage=stream_http");
      cleanup();
      finish_generation(active_generation, false);
      active_generation = 0U;
      continue;
    }

    encoded_prebuffer = static_cast<std::uint8_t*>(heap_caps_malloc(
        kEncodedPrebufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (encoded_prebuffer == nullptr) {
      ESP_LOGW(kTag, "playback failed stage=stream_buffer_alloc");
      cleanup();
      finish_generation(active_generation, false);
      active_generation = 0U;
      continue;
    }

    bool decoder_format_valid = false;
    bool stream_ended = false;
    bool stream_read_failed = false;
    bool decoder_needs_more_input = false;
    std::uint32_t http_read_timeout_retries = 0U;
    while (!stopped() && (!stream_ended || encoded_size != 0U)) {
      const bool should_refill =
          !stream_ended &&
          ((!decoder_format_valid && encoded_size < kEncodedPrebufferStartBytes) ||
           encoded_size < kEncodedPrebufferRefillBytes || decoder_needs_more_input);
      if (should_refill) {
        decoder_needs_more_input = false;
      }
      if (should_refill) {
        if (encoded_offset != 0U && encoded_size != 0U) {
          std::memmove(encoded_prebuffer,
                       encoded_prebuffer + encoded_offset,
                       encoded_size);
          encoded_offset = 0U;
        } else if (encoded_size == 0U) {
          encoded_offset = 0U;
        }
        const auto available = kEncodedPrebufferBytes - encoded_size;
        const auto request = std::min<std::size_t>(available, kHttpReadBytes);
        const int read = esp_http_client_read(
            client, reinterpret_cast<char*>(encoded_prebuffer + encoded_size),
            static_cast<int>(request));
        if (read <= 0) {
          if (read == -ESP_ERR_HTTP_EAGAIN &&
              http_read_timeout_retries < kMaxHttpReadTimeoutRetries) {
            ++http_read_timeout_retries;
            ESP_LOGW(kTag,
                     "HTTP stream read timeout; retry=%lu/%lu",
                     static_cast<unsigned long>(http_read_timeout_retries),
                     static_cast<unsigned long>(kMaxHttpReadTimeoutRetries));
            continue;
          }
          stream_ended =
              read == 0 && esp_http_client_is_complete_data_received(client);
          stream_read_failed = !stream_ended;
        } else {
          encoded_size += static_cast<std::size_t>(read);
          http_read_timeout_retries = 0U;
        }
        if (!stream_ended && encoded_size < kEncodedPrebufferStartBytes) {
          continue;
        }
      }
      if (stream_read_failed || encoded_size == 0U) {
        break;
      }

      // Feed only the currently contiguous buffered bytes. Any unconsumed
      // suffix remains in PSRAM and is compacted before the next network read.
      esp_audio_simple_dec_raw_t raw{};
      raw.buffer = encoded_prebuffer + encoded_offset;
      raw.len = static_cast<std::uint32_t>(encoded_size);
      raw.eos = stream_ended;
      while (raw.len != 0U && !stopped()) {
        esp_audio_simple_dec_out_t frame{};
        frame.buffer = pcm;
        frame.len = static_cast<std::uint32_t>(pcm_size);
        const auto decode_result = esp_audio_simple_dec_process(
            static_cast<esp_audio_simple_dec_handle_t>(decoder), &raw, &frame);
        if (decode_result == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH &&
            frame.needed_size > pcm_size && frame.needed_size <= kMaximumPcmBytes) {
          auto* replacement = static_cast<std::uint8_t*>(heap_caps_realloc(
              pcm, frame.needed_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
          if (replacement == nullptr) {
            decode_failed = true;
            raw.len = 0U;
            break;
          }
          pcm = replacement;
          pcm_size = frame.needed_size;
          continue;
        }
        if (decode_result != ESP_AUDIO_ERR_OK || raw.consumed > raw.len) {
          decode_failed = true;
          raw.len = 0U;
          break;
        }
        encoded_offset += raw.consumed;
        encoded_size -= raw.consumed;
        raw.buffer = encoded_prebuffer + encoded_offset;
        raw.len = static_cast<std::uint32_t>(encoded_size);
        if (frame.decoded_size == 0U) {
          if (raw.consumed == 0U) {
            if (stream_ended) {
              decode_failed = true;
            } else {
              decoder_needs_more_input = true;
            }
            raw.len = 0U;
          }
          continue;
        }
        esp_audio_simple_dec_info_t info{};
        if (esp_audio_simple_dec_get_info(
                static_cast<esp_audio_simple_dec_handle_t>(decoder), &info) !=
                ESP_AUDIO_ERR_OK ||
            info.bits_per_sample != 16U || info.channel == 0U ||
            info.channel > 2U || info.sample_rate == 0U) {
          decode_failed = true;
          raw.len = 0U;
          break;
        }
        if (!decoder_format_valid) {
          if (!open_i2s(info.sample_rate, info.channel) ||
              !enable_i2s_with_silence()) {
            decode_failed = true;
            raw.len = 0U;
            break;
          }
          decoder_format_valid = true;
          clock_enabled = true;
          ESP_LOGI(kTag,
                   "stream format rate=%lu channels=%u",
                   static_cast<unsigned long>(info.sample_rate),
                   static_cast<unsigned>(info.channel));
        }
        const auto* samples = reinterpret_cast<const std::int16_t*>(pcm);
        const std::size_t decoded_values =
            frame.decoded_size / sizeof(std::int16_t);
        const std::size_t sample_count = info.channel == 2U
                                             ? decoded_values / 2U
                                             : decoded_values;
        for (std::size_t offset = 0U; offset < sample_count && !stopped();) {
          const auto count = std::min<std::size_t>(output.size(), sample_count - offset);
          const auto volume = volume_percent_.load(std::memory_order_relaxed);
          for (std::size_t index = 0U; index < count; ++index) {
            std::int32_t value = samples[info.channel == 2U
                                             ? (offset + index) * 2U
                                             : offset + index];
            if (info.channel == 2U) {
              value = (value + samples[(offset + index) * 2U + 1U]) / 2;
            }
            output[index] = static_cast<std::int16_t>(value * volume / 100);
          }
          while (paused_.load(std::memory_order_acquire) && !stopped()) {
            vTaskDelay(delay_ticks(10));
          }
          if (stopped()) {
            break;
          }
          publish_visual(output.data(), count, &smoothed_rms);
          if (write_samples(output.data(), count) != ESP_OK) {
            decode_failed = true;
            raw.len = 0U;
            break;
          }
          if (!playback_started) {
            playback_started = true;
            started_pending_.store(true, std::memory_order_release);
            if (owner_task_ != nullptr) {
              xTaskNotifyGive(owner_task_);
            }
          }
          offset += count;
        }
      }
    }
    succeeded = stream_ended && !stream_read_failed && decoder_format_valid &&
                !decode_failed && !stopped();
    if (!succeeded && !stopped()) {
      ESP_LOGW(kTag, "playback failed stage=%s",
               decode_failed ? "stream_decode" : "stream_read");
    }
    cleanup();
    finish_generation(active_generation, succeeded);
    active_generation = 0U;
  }
}

bool OnlineMusicStream::open_i2s(std::uint32_t sample_rate,
                                 std::uint8_t channels) {
  if (tx_channel_ != nullptr || (channels != 1U && channels != 2U)) {
    return tx_channel_ != nullptr;
  }
  i2s_chan_config_t channel =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  channel.dma_desc_num = kDmaDescriptorCount;
  channel.dma_frame_num = kOutputSamples / 2U;
  if (i2s_new_channel(&channel, &tx_channel_, nullptr) != ESP_OK) {
    tx_channel_ = nullptr;
    return false;
  }
  i2s_std_config_t config = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
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
  if (i2s_channel_init_std_mode(tx_channel_, &config) != ESP_OK) {
    close_i2s();
    return false;
  }
  return true;
}

void OnlineMusicStream::close_i2s() {
  if (tx_channel_ != nullptr) {
    static_cast<void>(i2s_channel_disable(tx_channel_));
    static_cast<void>(i2s_del_channel(tx_channel_));
    tx_channel_ = nullptr;
  }
}

bool OnlineMusicStream::enable_i2s_with_silence() {
  std::array<std::int16_t, kOutputSamples / 2U> silence{};
  std::size_t loaded = 0U;
  if (tx_channel_ == nullptr ||
      i2s_channel_preload_data(tx_channel_, silence.data(), sizeof(silence),
                               &loaded) != ESP_OK ||
      loaded != sizeof(silence)) {
    return false;
  }
  const auto result = i2s_channel_enable(tx_channel_);
  return result == ESP_OK || result == ESP_ERR_INVALID_STATE;
}

void OnlineMusicStream::disable_i2s() {
  if (tx_channel_ != nullptr) {
    static_cast<void>(i2s_channel_disable(tx_channel_));
  }
}

esp_err_t OnlineMusicStream::write_samples(const std::int16_t* samples,
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
        tx_channel_, data + offset, bytes - offset, &written,
        kWriteTimeoutMs);
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

void OnlineMusicStream::publish_visual(const std::int16_t* samples,
                                       std::size_t sample_count,
                                       std::uint32_t* smoothed_rms) {
  if (samples == nullptr || sample_count == 0U || smoothed_rms == nullptr) {
    return;
  }
  std::uint64_t squares = 0U;
  for (std::size_t index = 0U; index < sample_count; ++index) {
    const auto value = static_cast<std::int32_t>(samples[index]);
    squares += static_cast<std::uint64_t>(value * value);
  }
  const auto rms = static_cast<std::uint32_t>(std::sqrt(
      static_cast<double>(squares / sample_count)) * 1000.0 / 32767.0);
  *smoothed_rms = (*smoothed_rms * 3U + rms) / 4U;
  visual_rms_.store(static_cast<std::uint16_t>(std::min<std::uint32_t>(*smoothed_rms, 1000U)),
                    std::memory_order_relaxed);
  visual_beat_.store(static_cast<std::uint16_t>(rms > *smoothed_rms ?
      std::min<std::uint32_t>(rms - *smoothed_rms, 1000U) : 0U), std::memory_order_relaxed);
  visual_pending_.store(true, std::memory_order_release);
  if (owner_task_ != nullptr) {
    xTaskNotifyGive(owner_task_);
  }
}

void OnlineMusicStream::finish_generation(std::uint32_t generation,
                                          bool succeeded) {
  playback_requested_.store(false, std::memory_order_release);
  stop_requested_.store(false, std::memory_order_release);
  worker_generation_.store(0U, std::memory_order_release);
  visual_rms_.store(0U, std::memory_order_relaxed);
  visual_beat_.store(0U, std::memory_order_relaxed);
  visual_pending_.store(true, std::memory_order_release);
  completed_result_.store(
      succeeded ? OnlineMusicPlaybackResult::Completed
                : OnlineMusicPlaybackResult::Failed,
      std::memory_order_release);
  result_pending_.store(true, std::memory_order_release);
  completed_generation_.store(generation, std::memory_order_release);
  if (owner_task_ != nullptr) {
    xTaskNotifyGive(owner_task_);
  }
}

}  // namespace easy_input::online_music
