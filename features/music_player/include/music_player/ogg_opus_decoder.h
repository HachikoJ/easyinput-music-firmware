#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace easy_input::music_player {

enum class OggOpusDecodeStatus : std::uint8_t {
  Frame,
  End,
  Failed,
};

struct OggOpusPcmFrame {
  const std::int16_t* samples = nullptr;
  std::size_t sample_count = 0U;
};

class OggOpusDecoder {
 public:
  OggOpusDecoder() = default;
  ~OggOpusDecoder();

  OggOpusDecoder(const OggOpusDecoder&) = delete;
  OggOpusDecoder& operator=(const OggOpusDecoder&) = delete;

  [[nodiscard]] esp_err_t open(const std::uint8_t* encoded,
                               std::size_t encoded_size);
  [[nodiscard]] esp_err_t reset();
  [[nodiscard]] OggOpusDecodeStatus decode_next(OggOpusPcmFrame* frame);
  void close();

  [[nodiscard]] bool ready() const;
  [[nodiscard]] std::int32_t last_error_code() const;

 private:
  [[nodiscard]] bool validate_container() const;
  [[nodiscard]] bool validate_format();

  const std::uint8_t* encoded_ = nullptr;
  std::size_t encoded_size_ = 0U;
  std::size_t encoded_offset_ = 0U;
  void* decoder_ = nullptr;
  std::uint8_t* pcm_buffer_ = nullptr;
  std::size_t pcm_buffer_size_ = 0U;
  bool ready_ = false;
  bool format_validated_ = false;
  bool owns_opus_registration_ = false;
  bool owns_ogg_registration_ = false;
  std::int32_t last_error_code_ = 0;
};

}  // namespace easy_input::music_player
