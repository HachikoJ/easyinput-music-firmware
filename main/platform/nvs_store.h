#pragma once

#include <cstdint>
#include <string>

#include "esp_err.h"
#include "keyboard/keymap.h"
#include "keyboard/wifi_profile.h"

namespace easy_input {

esp_err_t initialize_nvs_storage();

class NvsConfigStore {
 public:
  struct OnlineMusicCredentials {
    std::string api_key;
    std::string workspace_id;
  };

  using WifiProfile = ai_keyboard::WifiProfile;
  using WifiProfileList = ai_keyboard::WifiProfileList;

  bool load_config(std::string* json, esp_err_t* out_err = nullptr) const;
  bool save_config(const std::string& json, esp_err_t* out_err = nullptr) const;
  bool load_battery_full_anchor_mv(std::int32_t* measured_mv,
                                   esp_err_t* out_err = nullptr) const;
  bool save_battery_full_anchor_mv(std::int32_t measured_mv,
                                   esp_err_t* out_err = nullptr) const;
  bool load_gatt_schema_revision(std::uint8_t* revision,
                                 esp_err_t* out_err = nullptr) const;
  bool save_gatt_schema_revision(std::uint8_t revision,
                                 esp_err_t* out_err = nullptr) const;
  bool load_led_brightness(std::uint8_t* brightness_percent,
                           esp_err_t* out_err = nullptr) const;
  bool save_led_brightness(std::uint8_t brightness_percent,
                           esp_err_t* out_err = nullptr) const;
  bool load_host_platform(ai_keyboard::HostPlatform* platform,
                          esp_err_t* out_err = nullptr) const;
  bool save_host_platform(ai_keyboard::HostPlatform platform,
                          esp_err_t* out_err = nullptr) const;
  bool save_config_and_host_platform(const std::string& json,
                                     ai_keyboard::HostPlatform platform,
                                     esp_err_t* out_err = nullptr) const;
  bool load_wifi_profiles(WifiProfileList* profiles,
                          esp_err_t* out_err = nullptr) const;
  bool save_wifi_profile(const WifiProfile& profile,
                         esp_err_t* out_err = nullptr) const;
  bool load_online_music_credentials(
      OnlineMusicCredentials* credentials,
      esp_err_t* out_err = nullptr) const;
  bool save_online_music_credentials(
      const OnlineMusicCredentials& credentials,
      esp_err_t* out_err = nullptr) const;
};

}  // namespace easy_input
