#include "music_player/ogg_opus_decoder.h"

#include <algorithm>
#include <array>

#include "decoder/esp_audio_dec_reg.h"
#include "decoder/impl/esp_opus_dec.h"
#include "esp_audio_simple_dec.h"
#include "esp_heap_caps.h"
#include "simple_dec/impl/esp_ogg_dec.h"

namespace easy_input::music_player {
namespace {

constexpr std::uint32_t kExpectedSampleRate = 48000U;
constexpr std::uint8_t kExpectedChannels = 1U;
constexpr std::uint8_t kExpectedBitsPerSample = 16U;
constexpr std::uint32_t kInitialFrameMs = 20U;
constexpr std::uint32_t kMaximumFrameMs = 120U;
constexpr std::size_t kInitialDecodedFrameBytes =
    kExpectedSampleRate * kInitialFrameMs / 1000U * sizeof(std::int16_t);
constexpr std::size_t kMaximumDecodedFrameBytes =
    kExpectedSampleRate * kMaximumFrameMs / 1000U * sizeof(std::int16_t);
constexpr std::size_t kMaximumParserStepsPerFrame = 64U;
constexpr std::uint32_t kInternalHeapCaps =
    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
static_assert(kInitialDecodedFrameBytes == 1920U);
static_assert(kMaximumDecodedFrameBytes == 11520U);

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

OggOpusDecoder::~OggOpusDecoder() {
  close();
}

esp_err_t OggOpusDecoder::open(const std::uint8_t* encoded,
                               std::size_t encoded_size) {
  close();
  encoded_ = encoded;
  encoded_size_ = encoded_size;
  if (!validate_container()) {
    close();
    last_error_code_ = ESP_ERR_INVALID_RESPONSE;
    return ESP_ERR_INVALID_RESPONSE;
  }

  pcm_buffer_ = static_cast<std::uint8_t*>(
      heap_caps_malloc(kInitialDecodedFrameBytes, kInternalHeapCaps));
  if (pcm_buffer_ == nullptr) {
    close();
    last_error_code_ = ESP_ERR_NO_MEM;
    return ESP_ERR_NO_MEM;
  }
  pcm_buffer_size_ = kInitialDecodedFrameBytes;
  const auto opus_result = esp_opus_dec_register();
  if (!registration_succeeded(opus_result, &owns_opus_registration_)) {
    close();
    last_error_code_ = static_cast<std::int32_t>(opus_result);
    return ESP_FAIL;
  }
  const auto ogg_result = esp_ogg_dec_register();
  if (!registration_succeeded(ogg_result, &owns_ogg_registration_)) {
    close();
    last_error_code_ = static_cast<std::int32_t>(ogg_result);
    return ESP_FAIL;
  }

  esp_audio_simple_dec_cfg_t config{};
  config.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_OGG;
  config.use_frame_dec = false;
  esp_audio_simple_dec_handle_t decoder = nullptr;
  const auto open_result = esp_audio_simple_dec_open(&config, &decoder);
  if (open_result != ESP_AUDIO_ERR_OK || decoder == nullptr) {
    close();
    last_error_code_ = static_cast<std::int32_t>(open_result);
    return open_result == ESP_AUDIO_ERR_MEM_LACK ? ESP_ERR_NO_MEM : ESP_FAIL;
  }

  decoder_ = decoder;
  encoded_offset_ = 0U;
  format_validated_ = false;
  ready_ = true;
  last_error_code_ = 0;
  return ESP_OK;
}

esp_err_t OggOpusDecoder::reset() {
  if (!ready_ || decoder_ == nullptr || pcm_buffer_ == nullptr) {
    last_error_code_ = ESP_ERR_INVALID_STATE;
    return ESP_ERR_INVALID_STATE;
  }
  const auto result = esp_audio_simple_dec_reset(
      static_cast<esp_audio_simple_dec_handle_t>(decoder_));
  if (result != ESP_AUDIO_ERR_OK) {
    last_error_code_ = static_cast<std::int32_t>(result);
    return ESP_FAIL;
  }
  encoded_offset_ = 0U;
  format_validated_ = false;
  last_error_code_ = 0;
  return ESP_OK;
}

OggOpusDecodeStatus OggOpusDecoder::decode_next(OggOpusPcmFrame* frame) {
  if (frame == nullptr || !ready_ || decoder_ == nullptr ||
      pcm_buffer_ == nullptr) {
    last_error_code_ = frame == nullptr ? ESP_ERR_INVALID_ARG
                                        : ESP_ERR_INVALID_STATE;
    return OggOpusDecodeStatus::Failed;
  }
  *frame = {};

  for (std::size_t step = 0U; step < kMaximumParserStepsPerFrame; ++step) {
    if (encoded_offset_ >= encoded_size_) {
      last_error_code_ = 0;
      return OggOpusDecodeStatus::End;
    }
    const auto remaining = encoded_size_ - encoded_offset_;
    esp_audio_simple_dec_raw_t input{};
    input.buffer = const_cast<std::uint8_t*>(encoded_ + encoded_offset_);
    input.len = static_cast<std::uint32_t>(remaining);
    input.eos = true;

    esp_audio_simple_dec_out_t output{};
    output.buffer = pcm_buffer_;
    output.len = static_cast<std::uint32_t>(pcm_buffer_size_);
    const auto result = esp_audio_simple_dec_process(
        static_cast<esp_audio_simple_dec_handle_t>(decoder_), &input, &output);
    if (result == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
      if (output.needed_size <= pcm_buffer_size_ ||
          output.needed_size > kMaximumDecodedFrameBytes) {
        last_error_code_ = ESP_ERR_INVALID_SIZE;
        return OggOpusDecodeStatus::Failed;
      }
      auto* replacement = static_cast<std::uint8_t*>(
          heap_caps_malloc(output.needed_size, kInternalHeapCaps));
      if (replacement == nullptr) {
        last_error_code_ = ESP_ERR_NO_MEM;
        return OggOpusDecodeStatus::Failed;
      }
      heap_caps_free(pcm_buffer_);
      pcm_buffer_ = replacement;
      pcm_buffer_size_ = output.needed_size;
      continue;
    }
    if (result != ESP_AUDIO_ERR_OK || input.consumed > remaining) {
      last_error_code_ = result == ESP_AUDIO_ERR_OK
                             ? ESP_ERR_INVALID_RESPONSE
                             : static_cast<std::int32_t>(result);
      return OggOpusDecodeStatus::Failed;
    }
    encoded_offset_ += input.consumed;

    if (output.decoded_size != 0U) {
      if ((output.decoded_size % sizeof(std::int16_t)) != 0U ||
          output.decoded_size > pcm_buffer_size_ ||
          !validate_format()) {
        if (last_error_code_ == 0) {
          last_error_code_ = ESP_ERR_INVALID_RESPONSE;
        }
        return OggOpusDecodeStatus::Failed;
      }
      frame->samples = reinterpret_cast<const std::int16_t*>(pcm_buffer_);
      frame->sample_count = output.decoded_size / sizeof(std::int16_t);
      last_error_code_ = 0;
      return OggOpusDecodeStatus::Frame;
    }
    if (input.consumed == 0U) {
      last_error_code_ = ESP_ERR_INVALID_RESPONSE;
      return OggOpusDecodeStatus::Failed;
    }
  }

  last_error_code_ = ESP_ERR_INVALID_RESPONSE;
  return OggOpusDecodeStatus::Failed;
}

void OggOpusDecoder::close() {
  ready_ = false;
  format_validated_ = false;
  encoded_offset_ = 0U;
  if (decoder_ != nullptr) {
    esp_audio_simple_dec_close(
        static_cast<esp_audio_simple_dec_handle_t>(decoder_));
    decoder_ = nullptr;
  }
  if (owns_ogg_registration_) {
    esp_ogg_dec_unregister();
    owns_ogg_registration_ = false;
  }
  if (owns_opus_registration_) {
    esp_audio_dec_unregister(ESP_AUDIO_TYPE_OPUS);
    owns_opus_registration_ = false;
  }
  if (pcm_buffer_ != nullptr) {
    heap_caps_free(pcm_buffer_);
    pcm_buffer_ = nullptr;
  }
  pcm_buffer_size_ = 0U;
  encoded_ = nullptr;
  encoded_size_ = 0U;
}

bool OggOpusDecoder::ready() const {
  return ready_;
}

std::int32_t OggOpusDecoder::last_error_code() const {
  return last_error_code_;
}

bool OggOpusDecoder::validate_container() const {
  constexpr std::array<std::uint8_t, 4> kOggMagic{{'O', 'g', 'g', 'S'}};
  constexpr std::array<std::uint8_t, 8> kOpusHead{{
      'O', 'p', 'u', 's', 'H', 'e', 'a', 'd',
  }};
  if (encoded_ == nullptr ||
      encoded_size_ < kOggMagic.size() + kOpusHead.size() ||
      !std::equal(kOggMagic.begin(), kOggMagic.end(), encoded_)) {
    return false;
  }
  const auto search_bytes = std::min<std::size_t>(encoded_size_, 65536U);
  return std::search(encoded_,
                     encoded_ + search_bytes,
                     kOpusHead.begin(),
                     kOpusHead.end()) != encoded_ + search_bytes;
}

bool OggOpusDecoder::validate_format() {
  if (format_validated_) {
    return true;
  }
  esp_audio_simple_dec_info_t info{};
  const auto result = esp_audio_simple_dec_get_info(
      static_cast<esp_audio_simple_dec_handle_t>(decoder_), &info);
  if (result != ESP_AUDIO_ERR_OK) {
    last_error_code_ = static_cast<std::int32_t>(result);
    return false;
  }
  if (info.sample_rate != kExpectedSampleRate ||
      info.channel != kExpectedChannels ||
      info.bits_per_sample != kExpectedBitsPerSample) {
    last_error_code_ = ESP_ERR_INVALID_RESPONSE;
    return false;
  }
  format_validated_ = true;
  last_error_code_ = 0;
  return true;
}

}  // namespace easy_input::music_player
