#include "online_music/music_catalog.h"

#include <array>
#include <cctype>
#include <string_view>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include <cJSON.h>

namespace easy_input::online_music {
namespace {

constexpr const char* kApiBase = "https://music-api.gdstudio.xyz/api.php";
constexpr std::size_t kResponseCapacity = 4096U;
constexpr int kRequestTimeoutMs = 10000;

bool append_encoded(std::string_view input, std::string* output) {
  if (output == nullptr || input.empty()) {
    return false;
  }
  constexpr char kHex[] = "0123456789ABCDEF";
  for (const unsigned char ch : input) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      output->push_back(static_cast<char>(ch));
    } else {
      output->push_back('%');
      output->push_back(kHex[(ch >> 4U) & 0x0fU]);
      output->push_back(kHex[ch & 0x0fU]);
    }
  }
  return true;
}

bool json_string_field(std::string_view json,
                       const char* field,
                       std::string* value) {
  if (value == nullptr) {
    return false;
  }
  std::string owned(json);
  cJSON* root = cJSON_ParseWithLength(owned.data(), owned.size());
  if (root == nullptr) {
    return false;
  }
  const cJSON* object = root;
  if (cJSON_IsArray(root)) {
    object = cJSON_GetArrayItem(root, 0);
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, field);
  const bool valid = cJSON_IsString(item) && item->valuestring != nullptr &&
                     item->valuestring[0] != '\0';
  if (valid) {
    value->assign(item->valuestring);
  }
  cJSON_Delete(root);
  if (!valid) {
    return false;
  }
  return true;
}

bool get_json(const std::string& url, std::string* response) {
  if (response == nullptr || url.size() > 768U) {
    return false;
  }
  esp_http_client_config_t config{};
  config.url = url.c_str();
  config.timeout_ms = kRequestTimeoutMs;
  config.disable_auto_redirect = false;
  config.max_redirection_count = 2;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    return false;
  }
  if (esp_http_client_open(client, 0) != ESP_OK ||
      esp_http_client_fetch_headers(client) < 0 ||
      esp_http_client_get_status_code(client) != 200) {
    esp_http_client_cleanup(client);
    return false;
  }
  const int64_t length = esp_http_client_get_content_length(client);
  if (length > static_cast<int64_t>(kResponseCapacity - 1U)) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }
  std::array<char, kResponseCapacity> buffer{};
  std::size_t total = 0U;
  bool complete = false;
  while (total < buffer.size() - 1U) {
    const int read = esp_http_client_read(
        client, buffer.data() + total,
        static_cast<int>(buffer.size() - 1U - total));
    if (read <= 0) {
      complete = read == 0 && esp_http_client_is_complete_data_received(client);
      break;
    }
    total += static_cast<std::size_t>(read);
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  if (!complete || total == 0U || total >= buffer.size()) {
    return false;
  }
  response->assign(buffer.data(), total);
  return true;
}

bool valid_stream_url(const std::string& url) {
  return url.size() <= 512U && url.rfind("https://", 0U) == 0U &&
         url.find_first_of("\r\n") == std::string::npos;
}

}  // namespace

bool MusicCatalog::search(const std::string& title, std::string* song_id) const {
  return search(title, 1U, song_id);
}

bool MusicCatalog::search(const std::string& title,
                          std::uint32_t page,
                          std::string* song_id) const {
  if (song_id == nullptr || title.empty() || title.size() > 120U ||
      page == 0U || page > 100U) {
    return false;
  }
  std::string search_url = std::string(kApiBase) +
                           "?types=search&source=netease&count=1&pages=" +
                           std::to_string(page) + "&name=";
  if (!append_encoded(title, &search_url)) {
    return false;
  }
  std::string search_response;
  if (!get_json(search_url, &search_response) ||
      !json_string_field(search_response, "url_id", song_id)) {
    return false;
  }
  return true;
}

bool MusicCatalog::resolve_url(const std::string& song_id,
                               std::string* stream_url) const {
  if (stream_url == nullptr || song_id.empty() || song_id.size() > 120U) {
    return false;
  }
  std::string stream_response;
  const std::string stream_request = std::string(kApiBase) +
                                     "?types=url&source=netease&id=" + song_id +
                                     "&br=128";
  std::string url;
  if (!get_json(stream_request, &stream_response) ||
      !json_string_field(stream_response, "url", &url) ||
      !valid_stream_url(url)) {
    return false;
  }
  *stream_url = std::move(url);
  return true;
}

bool MusicCatalog::resolve(const std::string& title,
                           std::string* stream_url) const {
  std::string song_id;
  return search(title, &song_id) && resolve_url(song_id, stream_url);
}

}  // namespace easy_input::online_music
