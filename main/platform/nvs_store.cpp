#include "platform/nvs_store.h"

#include <cstdio>
#include <utility>
#include <vector>

#include "esp_log.h"
#include "keyboard/board_pins.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace easy_input {
namespace {

constexpr const char* kTag = "nvs_store";
constexpr const char* kPrefsNamespace = "ai_keyboard";
constexpr const char* kPrefsConfigKey = "config";
constexpr const char* kPrefsConfigKeyV2 = "config_v2";
constexpr const char* kPrefsBatteryFullKey = "bat_full_v1";
// bat_full_v2 may have been learned while SEN_CHRG polarity was inverted.
constexpr const char* kPrefsBatteryFullKeyV3 = "bat_full_v3";
constexpr const char* kPrefsGattSchemaRevisionKey = "gatt_rev_v1";
constexpr const char* kPrefsHostPlatformKey = "host_os_v1";
constexpr const char* kPrefsLedBrightnessKey = "led_brt_v1";
constexpr const char* kPrefsOnlineMusicApiKey = "om_asr_key_v1";
constexpr const char* kPrefsOnlineMusicWorkspaceKey = "om_asr_ws_v1";
constexpr const char* kPrefsWifiProfileSsidPrefix = "wifi_p_ssid_";
constexpr const char* kPrefsWifiProfilePasswordPrefix = "wifi_p_pwd_";

const char* prefs_config_key() {
#if defined(EASY_INPUT_BOARD_V2)
  return kPrefsConfigKeyV2;
#else
  return kPrefsConfigKey;
#endif
}

const char* prefs_battery_full_key() {
#if defined(EASY_INPUT_BOARD_V2)
  return kPrefsBatteryFullKeyV3;
#else
  return kPrefsBatteryFullKey;
#endif
}

void set_error(esp_err_t* out_err, esp_err_t err) {
  if (out_err != nullptr) {
    *out_err = err;
  }
}

esp_err_t read_string(nvs_handle_t handle,
                      const char* key,
                      std::string* value) {
  std::size_t required_len = 0U;
  esp_err_t err = nvs_get_str(handle, key, nullptr, &required_len);
  if (err != ESP_OK) return err;
  if (required_len <= 1U) return ESP_ERR_INVALID_SIZE;
  std::vector<char> buffer(required_len);
  err = nvs_get_str(handle, key, buffer.data(), &required_len);
  if (err == ESP_OK) value->assign(buffer.data());
  return err;
}

}  // namespace

namespace {

void wifi_profile_key(char* buffer,
                      std::size_t capacity,
                      const char* prefix,
                      std::size_t index) {
  std::snprintf(buffer, capacity, "%s%u", prefix,
                static_cast<unsigned>(index));
}

}  // namespace

esp_err_t initialize_nvs_storage() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(kTag, "NVS init returned %s, erasing and retrying", esp_err_to_name(err));
    const esp_err_t erase_err = nvs_flash_erase();
    if (erase_err != ESP_OK) {
      ESP_LOGE(kTag, "NVS erase failed: %s", esp_err_to_name(erase_err));
      return erase_err;
    }
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "NVS init failed: %s", esp_err_to_name(err));
  }
  return err;
}

bool NvsConfigStore::load_config(std::string* json, esp_err_t* out_err) const {
  if (json == nullptr) {
    set_error(out_err, ESP_ERR_INVALID_ARG);
    return false;
  }

  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    set_error(out_err, err);
    return false;
  }

  std::size_t required_len = 0;
  const char* key = prefs_config_key();
  err = nvs_get_str(handle, key, nullptr, &required_len);
  if (err != ESP_OK) {
    nvs_close(handle);
    set_error(out_err, err);
    return false;
  }
  if (required_len == 0) {
    nvs_close(handle);
    set_error(out_err, ESP_ERR_INVALID_SIZE);
    return false;
  }

  std::vector<char> buffer(required_len);
  err = nvs_get_str(handle, key, buffer.data(), &required_len);
  nvs_close(handle);
  if (err != ESP_OK) {
    set_error(out_err, err);
    return false;
  }

  json->assign(buffer.data());
  ESP_LOGI(kTag,
           "loaded %s config key='%s' bytes=%u",
           ai_keyboard::kBoardName,
           key,
           static_cast<unsigned>(json->size()));
  set_error(out_err, ESP_OK);
  return !json->empty();
}

bool NvsConfigStore::save_config(const std::string& json, esp_err_t* out_err) const {
  if (json.empty()) {
    set_error(out_err, ESP_ERR_INVALID_ARG);
    return false;
  }

  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    set_error(out_err, err);
    return false;
  }

  const char* key = prefs_config_key();
  err = nvs_set_str(handle, key, json.c_str());
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);

  if (err == ESP_OK) {
    ESP_LOGI(kTag,
             "saved %s config key='%s' bytes=%u",
             ai_keyboard::kBoardName,
             key,
             static_cast<unsigned>(json.size()));
  }
  set_error(out_err, err);
  return err == ESP_OK;
}

bool NvsConfigStore::load_battery_full_anchor_mv(std::int32_t* measured_mv,
                                                 esp_err_t* out_err) const {
  if (measured_mv == nullptr) {
    set_error(out_err, ESP_ERR_INVALID_ARG);
    return false;
  }

  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    set_error(out_err, err);
    return false;
  }

  err = nvs_get_i32(handle, prefs_battery_full_key(), measured_mv);
  nvs_close(handle);
  if (err == ESP_OK) {
    ESP_LOGI(kTag,
             "loaded %s battery full anchor key='%s' measured_mv=%ld",
             ai_keyboard::kBoardName,
             prefs_battery_full_key(),
             static_cast<long>(*measured_mv));
  }
  set_error(out_err, err);
  return err == ESP_OK;
}

bool NvsConfigStore::save_battery_full_anchor_mv(std::int32_t measured_mv,
                                                 esp_err_t* out_err) const {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    set_error(out_err, err);
    return false;
  }

  err = nvs_set_i32(handle, prefs_battery_full_key(), measured_mv);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  if (err == ESP_OK) {
    ESP_LOGI(kTag,
             "saved %s battery full anchor key='%s' measured_mv=%ld",
             ai_keyboard::kBoardName,
             prefs_battery_full_key(),
             static_cast<long>(measured_mv));
  }
  set_error(out_err, err);
  return err == ESP_OK;
}

bool NvsConfigStore::load_gatt_schema_revision(std::uint8_t* revision,
                                               esp_err_t* out_err) const {
  if (revision == nullptr) {
    set_error(out_err, ESP_ERR_INVALID_ARG);
    return false;
  }

  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    set_error(out_err, err);
    return false;
  }

  err = nvs_get_u8(handle, kPrefsGattSchemaRevisionKey, revision);
  nvs_close(handle);
  if (err == ESP_OK) {
    ESP_LOGI(kTag,
             "loaded %s GATT schema revision=%u",
             ai_keyboard::kBoardName,
             static_cast<unsigned>(*revision));
  }
  set_error(out_err, err);
  return err == ESP_OK;
}

bool NvsConfigStore::save_gatt_schema_revision(std::uint8_t revision,
                                               esp_err_t* out_err) const {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    set_error(out_err, err);
    return false;
  }

  err = nvs_set_u8(handle, kPrefsGattSchemaRevisionKey, revision);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  if (err == ESP_OK) {
    ESP_LOGI(kTag,
             "saved %s GATT schema revision=%u",
             ai_keyboard::kBoardName,
             static_cast<unsigned>(revision));
  }
  set_error(out_err, err);
  return err == ESP_OK;
}

bool NvsConfigStore::load_led_brightness(std::uint8_t* brightness_percent,
                                         esp_err_t* out_err) const {
  if (brightness_percent == nullptr) {
    set_error(out_err, ESP_ERR_INVALID_ARG);
    return false;
  }
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READONLY, &handle);
  if (err == ESP_OK) {
    err = nvs_get_u8(handle, kPrefsLedBrightnessKey, brightness_percent);
  }
  if (handle != 0) {
    nvs_close(handle);
  }
  if (err == ESP_OK && *brightness_percent > 100) {
    err = ESP_ERR_INVALID_STATE;
  }
  set_error(out_err, err);
  return err == ESP_OK;
}

bool NvsConfigStore::save_led_brightness(std::uint8_t brightness_percent,
                                         esp_err_t* out_err) const {
  if (brightness_percent > 100) {
    set_error(out_err, ESP_ERR_INVALID_ARG);
    return false;
  }
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = nvs_set_u8(handle, kPrefsLedBrightnessKey, brightness_percent);
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  if (handle != 0) {
    nvs_close(handle);
  }
  set_error(out_err, err);
  return err == ESP_OK;
}

bool NvsConfigStore::load_host_platform(ai_keyboard::HostPlatform* platform,
                                        esp_err_t* out_err) const {
  if (platform == nullptr) { set_error(out_err, ESP_ERR_INVALID_ARG); return false; }
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READONLY, &handle);
  std::uint8_t stored = 0;
  if (err == ESP_OK) err = nvs_get_u8(handle, kPrefsHostPlatformKey, &stored);
  if (handle != 0) nvs_close(handle);
  if (err == ESP_OK && stored <= 1) {
    *platform = stored == 1 ? ai_keyboard::HostPlatform::Windows
                            : ai_keyboard::HostPlatform::MacOS;
    set_error(out_err, ESP_OK);
    return true;
  }
  if (err == ESP_OK) err = ESP_ERR_INVALID_STATE;
  set_error(out_err, err);
  return false;
}

bool NvsConfigStore::save_host_platform(ai_keyboard::HostPlatform platform,
                                        esp_err_t* out_err) const {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = nvs_set_u8(handle, kPrefsHostPlatformKey,
                     platform == ai_keyboard::HostPlatform::Windows ? 1 : 0);
  }
  if (err == ESP_OK) err = nvs_commit(handle);
  if (handle != 0) nvs_close(handle);
  set_error(out_err, err);
  return err == ESP_OK;
}

bool NvsConfigStore::save_config_and_host_platform(
    const std::string& json,
    ai_keyboard::HostPlatform platform,
    esp_err_t* out_err) const {
  if (json.empty()) {
    set_error(out_err, ESP_ERR_INVALID_ARG);
    return false;
  }

  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = nvs_set_str(handle, prefs_config_key(), json.c_str());
  }
  if (err == ESP_OK) {
    err = nvs_set_u8(handle,
                     kPrefsHostPlatformKey,
                     platform == ai_keyboard::HostPlatform::Windows ? 1 : 0);
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  if (handle != 0) {
    nvs_close(handle);
  }

  if (err == ESP_OK) {
    ESP_LOGI(kTag,
             "saved %s config+platform key='%s' bytes=%u platform=%s",
             ai_keyboard::kBoardName,
             prefs_config_key(),
             static_cast<unsigned>(json.size()),
             ai_keyboard::host_platform_name(platform));
  }
  set_error(out_err, err);
  return err == ESP_OK;
}

bool NvsConfigStore::load_wifi_profiles(WifiProfileList* profiles,
                                        esp_err_t* out_err) const {
  if (profiles == nullptr) {
    set_error(out_err, ESP_ERR_INVALID_ARG);
    return false;
  }
  *profiles = {};
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    set_error(out_err, err);
    return false;
  }
  bool any = false;
  for (std::size_t index = 0; index < profiles->size(); ++index) {
    char ssid_key[16] = {};
    char password_key[16] = {};
    wifi_profile_key(ssid_key, sizeof(ssid_key), kPrefsWifiProfileSsidPrefix, index);
    wifi_profile_key(password_key, sizeof(password_key), kPrefsWifiProfilePasswordPrefix, index);
    std::string ssid;
    if (read_string(handle, ssid_key, &ssid) != ESP_OK) {
      continue;
    }
    std::string password;
    const esp_err_t password_err = read_string(handle, password_key, &password);
    if (password_err != ESP_OK && password_err != ESP_ERR_NVS_NOT_FOUND) {
      continue;
    }
    (*profiles)[index] = {std::move(ssid), std::move(password)};
    any = true;
  }
  nvs_close(handle);
  set_error(out_err, any ? ESP_OK : ESP_ERR_NVS_NOT_FOUND);
  return any;
}

bool NvsConfigStore::save_wifi_profile(const WifiProfile& profile,
                                       esp_err_t* out_err) const {
  if (!profile.configured()) {
    set_error(out_err, ESP_ERR_INVALID_ARG);
    return false;
  }
  WifiProfileList profiles{};
  esp_err_t load_err = ESP_OK;
  load_wifi_profiles(&profiles, &load_err);
  if (profiles[0].ssid == profile.ssid &&
      profiles[0].password == profile.password) {
    set_error(out_err, ESP_OK);
    return true;
  }
  WifiProfileList reordered{};
  reordered[0] = profile;
  std::size_t next = 1U;
  for (const auto& existing : profiles) {
    if (!existing.configured() || existing.ssid == profile.ssid) {
      continue;
    }
    if (next >= reordered.size()) {
      break;
    }
    reordered[next++] = existing;
  }
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    for (std::size_t index = 0; index < reordered.size(); ++index) {
      char ssid_key[16] = {};
      char password_key[16] = {};
      wifi_profile_key(ssid_key, sizeof(ssid_key), kPrefsWifiProfileSsidPrefix, index);
      wifi_profile_key(password_key, sizeof(password_key), kPrefsWifiProfilePasswordPrefix, index);
      if (reordered[index].configured()) {
        err = nvs_set_str(handle, ssid_key, reordered[index].ssid.c_str());
        if (err == ESP_OK) {
          err = nvs_set_str(handle, password_key, reordered[index].password.c_str());
        }
      } else {
        err = nvs_erase_key(handle, ssid_key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        if (err == ESP_OK) {
          err = nvs_erase_key(handle, password_key);
          if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        }
      }
      if (err != ESP_OK) break;
    }
  }
  if (err == ESP_OK) err = nvs_commit(handle);
  if (handle != 0) nvs_close(handle);
  set_error(out_err, err);
  return err == ESP_OK;
}

bool NvsConfigStore::load_online_music_credentials(
    OnlineMusicCredentials* credentials,
    esp_err_t* out_err) const {
  if (credentials == nullptr) {
    set_error(out_err, ESP_ERR_INVALID_ARG);
    return false;
  }
  *credentials = {};
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READONLY, &handle);
  if (err == ESP_OK) {
    err = read_string(handle, kPrefsOnlineMusicApiKey, &credentials->api_key);
  }
  if (err == ESP_OK) {
    err = read_string(handle, kPrefsOnlineMusicWorkspaceKey,
                      &credentials->workspace_id);
  }
  if (handle != 0) nvs_close(handle);
  if (err != ESP_OK) *credentials = {};
  set_error(out_err, err);
  return err == ESP_OK;
}

bool NvsConfigStore::save_online_music_credentials(
    const OnlineMusicCredentials& credentials,
    esp_err_t* out_err) const {
  if (credentials.api_key.empty() || credentials.workspace_id.empty()) {
    set_error(out_err, ESP_ERR_INVALID_ARG);
    return false;
  }
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kPrefsNamespace, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = nvs_set_str(handle, kPrefsOnlineMusicApiKey,
                      credentials.api_key.c_str());
  }
  if (err == ESP_OK) {
    err = nvs_set_str(handle, kPrefsOnlineMusicWorkspaceKey,
                      credentials.workspace_id.c_str());
  }
  if (err == ESP_OK) err = nvs_commit(handle);
  if (handle != 0) nvs_close(handle);
  if (err == ESP_OK) {
    ESP_LOGI(kTag, "saved online music credentials configured=1");
  }
  set_error(out_err, err);
  return err == ESP_OK;
}

}  // namespace easy_input
