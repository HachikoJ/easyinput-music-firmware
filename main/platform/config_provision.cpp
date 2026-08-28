#include "platform/config_provision.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "esp_flash.h"
#include "keyboard/config_receiver.h"

namespace easy_input {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic{{
    'E', 'I', 'P', 'R', 'O', 'V', '1', 0,
}};
constexpr std::size_t kHeaderBytes = 16U;
constexpr std::size_t kProvisionBytes = 0x1000U;
constexpr std::uint32_t kProvisionAddress = 0xFFF000U;

std::uint16_t read_u16(const std::uint8_t* data) {
  return static_cast<std::uint16_t>(data[0]) |
         (static_cast<std::uint16_t>(data[1]) << 8U);
}

bool address_is_available() {
  std::uint32_t flash_size = 0U;
  return esp_flash_get_size(nullptr, &flash_size) == ESP_OK &&
         flash_size >= kProvisionAddress + kProvisionBytes;
}

}  // namespace

esp_err_t read_config_provision(std::string* json) {
  if (json == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  json->clear();
  if (!address_is_available()) {
    return ESP_ERR_NOT_FOUND;
  }
  std::array<std::uint8_t, kHeaderBytes> header{};
  esp_err_t err = esp_flash_read(
      nullptr, header.data(), kProvisionAddress, header.size());
  if (err != ESP_OK) {
    return err;
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), header.begin())) {
    return ESP_ERR_NOT_FOUND;
  }
  const auto length = read_u16(header.data() + 8U);
  const auto expected_crc = read_u16(header.data() + 10U);
  if (length == 0U || length > ai_keyboard::kConfigMaxJsonLen ||
      kHeaderBytes + length > kProvisionBytes) {
    return ESP_ERR_INVALID_SIZE;
  }
  std::string candidate(length, '\0');
  err = esp_flash_read(nullptr,
                       candidate.data(),
                       kProvisionAddress + kHeaderBytes,
                       candidate.size());
  if (err != ESP_OK) {
    return err;
  }
  const auto actual_crc = ai_keyboard::crc16_ccitt(
      reinterpret_cast<const std::uint8_t*>(candidate.data()),
      candidate.size());
  if (actual_crc != expected_crc) {
    return ESP_ERR_INVALID_CRC;
  }
  *json = std::move(candidate);
  return ESP_OK;
}

esp_err_t consume_config_provision() {
  return !address_is_available()
             ? ESP_ERR_NOT_FOUND
             : esp_flash_erase_region(
                   nullptr, kProvisionAddress, kProvisionBytes);
}

}  // namespace easy_input
