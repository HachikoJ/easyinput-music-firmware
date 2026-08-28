#include "online_music/online_music_asr.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>

#include <cJSON.h>

namespace easy_input::online_music {
namespace {

constexpr char kBeijingSuffix[] = ".cn-beijing.maas.aliyuncs.com/api-ws/v1/inference";
constexpr char kSingaporeSuffix[] = ".ap-southeast-1.maas.aliyuncs.com/api-ws/v1/inference";

bool valid_token(const std::string& value, std::size_t max_bytes) {
  if (value.empty() || value.size() > max_bytes) {
    return false;
  }
  for (const unsigned char ch : value) {
    if (!(std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.')) {
      return false;
    }
  }
  return true;
}

bool valid_task_id(const std::string& value) {
  if (value.empty() || value.size() > OnlineMusicAsr::kMaxTaskIdBytes) {
    return false;
  }
  for (const unsigned char ch : value) {
    if (!(std::isalnum(ch) || ch == '-' || ch == '_')) {
      return false;
    }
  }
  return true;
}

bool json_string(const cJSON* object, const char* key, std::string* value,
                 std::size_t max_bytes) {
  if (object == nullptr || key == nullptr || value == nullptr) {
    return false;
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsString(item) || item->valuestring == nullptr ||
      std::strlen(item->valuestring) > max_bytes) {
    return false;
  }
  value->assign(item->valuestring);
  return true;
}

bool json_bool(const cJSON* object, const char* key, bool* value) {
  if (object == nullptr || key == nullptr || value == nullptr) {
    return false;
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsBool(item)) {
    return false;
  }
  *value = cJSON_IsTrue(item);
  return true;
}

bool print_json(cJSON* root, std::string* output) {
  if (root == nullptr || output == nullptr) {
    cJSON_Delete(root);
    return false;
  }
  char* text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (text == nullptr) {
    return false;
  }
  output->assign(text);
  std::free(text);
  return true;
}

}  // namespace

bool PcmChunker::append(const std::uint8_t* pcm, std::size_t bytes,
                        PcmChunkSink sink, void* context) {
  if (pcm == nullptr || bytes == 0U || (bytes % 2U) != 0U || sink == nullptr) {
    return false;
  }
  std::size_t offset = 0U;
  while (offset < bytes) {
    const auto count = std::min(bytes - offset, chunk_.size() - used_);
    std::memcpy(chunk_.data() + used_, pcm + offset, count);
    used_ += count;
    offset += count;
    if (used_ == chunk_.size()) {
      if (!sink(context, chunk_.data(), used_)) {
        return false;
      }
      used_ = 0U;
    }
  }
  return true;
}

bool PcmChunker::finish(PcmChunkSink sink, void* context) {
  if (used_ == 0U) {
    return true;
  }
  if (sink == nullptr || !sink(context, chunk_.data(), used_)) {
    return false;
  }
  used_ = 0U;
  return true;
}

void PcmChunker::reset() { used_ = 0U; }

bool OnlineMusicAsr::build_endpoint(const OnlineMusicAsrConfig& config,
                                    std::string* endpoint) {
  if (endpoint == nullptr || !valid_token(config.workspace_id,
                                          kMaxWorkspaceIdBytes)) {
    return false;
  }
  endpoint->assign("wss://");
  endpoint->append(config.workspace_id);
  endpoint->append(config.region == AsrRegion::Singapore ? kSingaporeSuffix
                                                           : kBeijingSuffix);
  return true;
}

bool OnlineMusicAsr::build_authorization_header(
    const OnlineMusicAsrConfig& config, std::string* header_value) {
  if (header_value == nullptr || config.api_key.empty() ||
      config.api_key.size() > kMaxApiKeyBytes ||
      config.api_key.find_first_of("\r\n") != std::string::npos) {
    return false;
  }
  header_value->assign("Bearer ");
  header_value->append(config.api_key);
  return true;
}

bool OnlineMusicAsr::build_run_task(const OnlineMusicAsrConfig& config,
                                    const std::string& task_id,
                                    std::string* frame) {
  if (frame == nullptr || !valid_task_id(task_id) ||
      !valid_token(config.model, 128U) || config.language.size() > 16U) {
    return false;
  }
  cJSON* root = cJSON_CreateObject();
  cJSON* header = cJSON_CreateObject();
  cJSON* payload = cJSON_CreateObject();
  cJSON* parameters = cJSON_CreateObject();
  cJSON* input = cJSON_CreateObject();
  if (root == nullptr || header == nullptr || payload == nullptr ||
      parameters == nullptr || input == nullptr) {
    cJSON_Delete(root);
    cJSON_Delete(header);
    cJSON_Delete(payload);
    cJSON_Delete(parameters);
    cJSON_Delete(input);
    return false;
  }
  cJSON_AddStringToObject(header, "action", "run-task");
  cJSON_AddStringToObject(header, "task_id", task_id.c_str());
  cJSON_AddStringToObject(header, "streaming", "duplex");
  cJSON_AddStringToObject(payload, "task_group", "audio");
  cJSON_AddStringToObject(payload, "task", "asr");
  cJSON_AddStringToObject(payload, "function", "recognition");
  cJSON_AddStringToObject(payload, "model", config.model.c_str());
  cJSON_AddStringToObject(parameters, "format", "pcm");
  cJSON_AddNumberToObject(parameters, "sample_rate", 16000);
  if (!config.language.empty()) {
    cJSON* hints = cJSON_CreateArray();
    if (hints == nullptr) {
      cJSON_Delete(root);
      cJSON_Delete(header);
      cJSON_Delete(payload);
      cJSON_Delete(parameters);
      cJSON_Delete(input);
      return false;
    }
    cJSON_AddItemToArray(hints, cJSON_CreateString(config.language.c_str()));
    cJSON_AddItemToObject(parameters, "language_hints", hints);
  }
  cJSON_AddItemToObject(payload, "parameters", parameters);
  cJSON_AddItemToObject(payload, "input", input);
  cJSON_AddItemToObject(root, "header", header);
  cJSON_AddItemToObject(root, "payload", payload);
  return print_json(root, frame);
}

bool OnlineMusicAsr::build_finish_task(const std::string& task_id,
                                       std::string* frame) {
  if (frame == nullptr || !valid_task_id(task_id)) {
    return false;
  }
  cJSON* root = cJSON_CreateObject();
  cJSON* header = cJSON_CreateObject();
  cJSON* payload = cJSON_CreateObject();
  cJSON* input = cJSON_CreateObject();
  if (root == nullptr || header == nullptr || payload == nullptr ||
      input == nullptr) {
    cJSON_Delete(root);
    cJSON_Delete(header);
    cJSON_Delete(payload);
    cJSON_Delete(input);
    return false;
  }
  cJSON_AddStringToObject(header, "action", "finish-task");
  cJSON_AddStringToObject(header, "task_id", task_id.c_str());
  cJSON_AddStringToObject(header, "streaming", "duplex");
  cJSON_AddItemToObject(payload, "input", input);
  cJSON_AddItemToObject(root, "header", header);
  cJSON_AddItemToObject(root, "payload", payload);
  return print_json(root, frame);
}

bool OnlineMusicAsr::parse_server_event(
    const std::string& frame, OnlineMusicAsrParsedEvent* parsed) {
  if (parsed == nullptr || frame.empty() || frame.size() > 8192U) {
    return false;
  }
  *parsed = {};
  cJSON* root = cJSON_ParseWithLength(frame.data(), frame.size());
  if (root == nullptr) {
    return false;
  }
  const cJSON* header = cJSON_GetObjectItemCaseSensitive(root, "header");
  std::string event_name;
  const bool header_ok = cJSON_IsObject(header) &&
                         json_string(header, "event", &event_name, 64U) &&
                         json_string(header, "task_id", &parsed->task_id,
                                     kMaxTaskIdBytes);
  if (!header_ok) {
    cJSON_Delete(root);
    return false;
  }
  if (event_name == "task-started") {
    parsed->event = OnlineMusicAsrEvent::TaskStarted;
  } else if (event_name == "task-finished") {
    parsed->event = OnlineMusicAsrEvent::TaskFinished;
  } else if (event_name == "task-failed") {
    parsed->event = OnlineMusicAsrEvent::TaskFailed;
    json_string(header, "error_code", &parsed->error_code, 128U);
    json_string(header, "error_message", &parsed->error_message, 256U);
  } else if (event_name == "result-generated") {
    parsed->event = OnlineMusicAsrEvent::ResultGenerated;
    const cJSON* payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    const cJSON* output = cJSON_GetObjectItemCaseSensitive(payload, "output");
    const cJSON* sentence = cJSON_GetObjectItemCaseSensitive(output, "sentence");
    if (!cJSON_IsObject(sentence)) {
      cJSON_Delete(root);
      return false;
    }
    json_bool(sentence, "sentence_begin", &parsed->sentence_begin);
    json_bool(sentence, "sentence_end", &parsed->sentence_end);
    json_bool(sentence, "heartbeat", &parsed->heartbeat);
    const cJSON* text = cJSON_GetObjectItemCaseSensitive(sentence, "text");
    if (cJSON_IsString(text) && text->valuestring != nullptr &&
        std::strlen(text->valuestring) <= kMaxTranscriptBytes) {
      parsed->transcript.assign(text->valuestring);
    } else if (text != nullptr || !parsed->heartbeat) {
      cJSON_Delete(root);
      return false;
    }
  } else {
    cJSON_Delete(root);
    return false;
  }
  cJSON_Delete(root);
  return true;
}

bool OnlineMusicAsr::configure(const OnlineMusicAsrConfig& config) {
  std::string endpoint;
  std::string auth;
  if (!build_endpoint(config, &endpoint) ||
      !build_authorization_header(config, &auth) ||
      !valid_token(config.model, 128U) || config.language.size() > 16U) {
    configured_ = false;
    task_active_ = false;
    task_finishing_ = false;
    config_ = {};
    task_id_.clear();
    transcript_.clear();
    transcript_final_ = false;
    return false;
  }
  config_ = config;
  configured_ = true;
  task_active_ = false;
  task_finishing_ = false;
  task_id_.clear();
  transcript_.clear();
  transcript_final_ = false;
  return true;
}

bool OnlineMusicAsr::start(const std::string& task_id, std::string* run_frame) {
  if (!configured_ || task_active_ || run_frame == nullptr ||
      !build_run_task(config_, task_id, run_frame)) {
    return false;
  }
  task_id_ = task_id;
  task_active_ = true;
  task_finishing_ = false;
  transcript_.clear();
  transcript_final_ = false;
  return true;
}

bool OnlineMusicAsr::append_audio(const std::uint8_t* pcm,
                                  std::size_t bytes) const {
  // The caller sends this as a binary WS frame after task-started. Keeping the
  // check here prevents accidental empty/oversized frames at the API boundary.
  return configured_ && task_active_ && pcm != nullptr && bytes > 0U &&
         bytes <= 32000U && (bytes % 2U) == 0U;
}

bool OnlineMusicAsr::finish(std::string* finish_frame) {
  if (!configured_ || !task_active_ || finish_frame == nullptr ||
      !build_finish_task(task_id_, finish_frame)) {
    return false;
  }
  task_active_ = false;
  task_finishing_ = true;
  return true;
}

bool OnlineMusicAsr::ingest_server_event(
    const std::string& frame, OnlineMusicAsrParsedEvent* parsed) {
  if (!configured_ || parsed == nullptr || (!task_active_ && !task_finishing_) ||
      !parse_server_event(frame, parsed) ||
      parsed->task_id != task_id_) {
    return false;
  }
  if (parsed->event == OnlineMusicAsrEvent::TaskFailed ||
      parsed->event == OnlineMusicAsrEvent::TaskFinished) {
    task_active_ = false;
    task_finishing_ = false;
  } else if (parsed->event == OnlineMusicAsrEvent::ResultGenerated &&
             !parsed->heartbeat && !parsed->transcript.empty()) {
    if (parsed->sentence_end) {
      transcript_ = parsed->transcript;
      transcript_final_ = true;
    } else if (!transcript_final_) {
      transcript_ = parsed->transcript;
    }
  }
  return true;
}

}  // namespace easy_input::online_music
