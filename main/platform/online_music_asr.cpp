#include "platform/online_music_asr.h"

#include <array>
#include <cstdio>
#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "online_music/ws_text_assembler.h"
#include "platform/keyboard_audio.h"

namespace easy_input {
namespace {
constexpr std::uint32_t kStack = 16U * 1024U;
constexpr std::size_t kFrameBytes = 640U;

bool split_endpoint(const std::string& endpoint, std::string* host,
                    std::string* path) {
  constexpr char prefix[] = "wss://";
  if (host == nullptr || path == nullptr || endpoint.rfind(prefix, 0) != 0) return false;
  const auto start = sizeof(prefix) - 1U;
  const auto slash = endpoint.find('/', start);
  if (slash == std::string::npos || slash == start) return false;
  *host = endpoint.substr(start, slash - start);
  *path = endpoint.substr(slash);
  return host->size() <= 128U && path->size() <= 256U;
}

bool send_frame(esp_transport_handle_t ws, ws_transport_opcodes_t opcode,
                const char* data, std::size_t size) {
  const auto complete_opcode = static_cast<ws_transport_opcodes_t>(
      static_cast<int>(opcode) |
      static_cast<int>(WS_TRANSPORT_OPCODES_FIN));
  return ws != nullptr && data != nullptr && size > 0U && size <= 32000U &&
         esp_transport_ws_send_raw(ws, complete_opcode, data,
                                    static_cast<int>(size),
                                    2000) == static_cast<int>(size);
}

struct AudioSinkContext {
  esp_transport_handle_t ws = nullptr;
  online_music::OnlineMusicAsr* protocol = nullptr;
};

bool send_audio_chunk(void* context, const std::uint8_t* pcm,
                      std::size_t bytes) {
  auto* sink = static_cast<AudioSinkContext*>(context);
  return sink != nullptr && sink->protocol != nullptr &&
         sink->protocol->append_audio(pcm, bytes) &&
         send_frame(sink->ws, WS_TRANSPORT_OPCODES_BINARY,
                    reinterpret_cast<const char*>(pcm), bytes);
}

std::string make_task_id() {
  std::array<char, 33> task_id{};
  std::snprintf(task_id.data(),
                task_id.size(),
                "%08lx%08lx%08lx%08lx",
                static_cast<unsigned long>(esp_random()),
                static_cast<unsigned long>(esp_random()),
                static_cast<unsigned long>(esp_random()),
                static_cast<unsigned long>(esp_random()));
  return task_id.data();
}
}

const char* online_music_asr_failure_name(OnlineMusicAsrFailure failure) {
  switch (failure) {
    case OnlineMusicAsrFailure::None: return "none";
    case OnlineMusicAsrFailure::NotConfigured: return "not_configured";
    case OnlineMusicAsrFailure::WorkerAllocation: return "worker_allocation";
    case OnlineMusicAsrFailure::ProtocolConfig: return "protocol_config";
    case OnlineMusicAsrFailure::WifiUnavailable: return "wifi_unavailable";
    case OnlineMusicAsrFailure::EndpointInvalid: return "endpoint_invalid";
    case OnlineMusicAsrFailure::TransportInit: return "transport_init";
    case OnlineMusicAsrFailure::Connect: return "connect";
    case OnlineMusicAsrFailure::TaskRequest: return "task_request";
    case OnlineMusicAsrFailure::TaskStartTimeout: return "task_start_timeout";
    case OnlineMusicAsrFailure::ServerTaskFailed: return "server_task_failed";
    case OnlineMusicAsrFailure::WebSocketRead: return "websocket_read";
    case OnlineMusicAsrFailure::MicrophoneStart: return "microphone_start";
    case OnlineMusicAsrFailure::MicrophoneRead: return "microphone_read";
    case OnlineMusicAsrFailure::AudioSend: return "audio_send";
    case OnlineMusicAsrFailure::AudioFlush: return "audio_flush";
    case OnlineMusicAsrFailure::FinishRequest: return "finish_request";
    case OnlineMusicAsrFailure::ResultTimeout: return "result_timeout";
    case OnlineMusicAsrFailure::EmptyTranscript: return "empty_transcript";
    case OnlineMusicAsrFailure::Cancelled: return "cancelled";
  }
  return "unknown";
}

esp_err_t OnlineMusicAsr::begin(TaskHandle_t owner, KeyboardAudioLink* audio) {
  if (initialized_ || owner == nullptr || audio == nullptr) {
    return initialized_ ? ESP_OK : ESP_ERR_INVALID_ARG;
  }
  owner_task_ = owner;
  audio_ = audio;
  mutex_ = xSemaphoreCreateMutex();
  if (mutex_ == nullptr) return ESP_ERR_NO_MEM;
  esp_log_level_set("transport_ws", ESP_LOG_INFO);
  initialized_ = true;
  return ESP_OK;
}

void OnlineMusicAsr::configure(const std::string& api_key,
                               const std::string& workspace_id) {
  if (mutex_ == nullptr) return;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  api_key_ = api_key;
  workspace_id_ = workspace_id;
  credentials_configured_.store(!api_key.empty() && !workspace_id.empty(),
                                std::memory_order_release);
  xSemaphoreGive(mutex_);
}

bool OnlineMusicAsr::start() {
  if (!initialized_ || busy_.load(std::memory_order_acquire)) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool configured = !api_key_.empty() && !workspace_id_.empty();
  xSemaphoreGive(mutex_);
  if (!configured) {
    last_failure_.store(OnlineMusicAsrFailure::NotConfigured,
                        std::memory_order_release);
    ESP_LOGW("online_asr", "start rejected: credentials configured=0");
    return false;
  }
  if (!ensure_worker()) {
    last_failure_.store(OnlineMusicAsrFailure::WorkerAllocation,
                        std::memory_order_release);
    ESP_LOGW("online_asr", "start rejected: worker allocation failed");
    return false;
  }
  capture_started_pending_.store(false, std::memory_order_release);
  stop_requested_.store(false, std::memory_order_release);
  finish_requested_.store(false, std::memory_order_release);
  requested_.store(true, std::memory_order_release);
  last_failure_.store(OnlineMusicAsrFailure::None, std::memory_order_release);
  websocket_http_status_.store(-1, std::memory_order_release);
  xSemaphoreTake(mutex_, portMAX_DELAY);
  server_error_code_.clear();
  xSemaphoreGive(mutex_);
  xTaskNotifyGive(worker_task_);
  return true;
}

bool OnlineMusicAsr::ensure_worker() {
  if (worker_task_ != nullptr) return true;
  return xTaskCreatePinnedToCoreWithCaps(
             &OnlineMusicAsr::task_entry, "online_asr", kStack, this,
             tskIDLE_PRIORITY + 2, &worker_task_, 1,
             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) ==
         pdPASS;
}

void OnlineMusicAsr::finish_input() {
  finish_requested_.store(true, std::memory_order_release);
}

void OnlineMusicAsr::stop() {
  stop_requested_.store(true, std::memory_order_release);
  requested_.store(false, std::memory_order_release);
  if (worker_task_ != nullptr) xTaskNotifyGive(worker_task_);
}

bool OnlineMusicAsr::busy() const { return busy_.load(std::memory_order_acquire); }

OnlineMusicAsrDiagnostics OnlineMusicAsr::diagnostics() const {
  OnlineMusicAsrDiagnostics result{
      credentials_configured_.load(std::memory_order_acquire),
      busy_.load(std::memory_order_acquire),
      last_failure_.load(std::memory_order_acquire),
      websocket_http_status_.load(std::memory_order_acquire),
      {},
  };
  if (mutex_ != nullptr) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    result.server_error_code = server_error_code_;
    xSemaphoreGive(mutex_);
  }
  return result;
}

bool OnlineMusicAsr::take_capture_started() {
  return capture_started_pending_.exchange(false, std::memory_order_acq_rel);
}

bool OnlineMusicAsr::take_result(bool* succeeded, std::string* text) {
  if (succeeded == nullptr || text == nullptr ||
      !result_pending_.exchange(false, std::memory_order_acq_rel)) return false;
  *succeeded = result_success_.load(std::memory_order_acquire);
  xSemaphoreTake(mutex_, portMAX_DELAY);
  *text = result_text_;
  result_text_.clear();
  xSemaphoreGive(mutex_);
  return true;
}

void OnlineMusicAsr::task_entry(void* arg) {
  auto* self = static_cast<OnlineMusicAsr*>(arg);
  if (self != nullptr) self->run();
  vTaskDeleteWithCaps(nullptr);
}

void OnlineMusicAsr::run() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!requested_.exchange(false, std::memory_order_acq_rel) ||
        stop_requested_.load(std::memory_order_acquire)) continue;
    busy_.store(true, std::memory_order_release);
    bool success = false;
    OnlineMusicAsrFailure failure = OnlineMusicAsrFailure::ProtocolConfig;
    std::string transcript, api_key, workspace_id, endpoint, auth, run_frame;
    std::string server_error_code;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    api_key = api_key_;
    workspace_id = workspace_id_;
    xSemaphoreGive(mutex_);

    online_music::OnlineMusicAsrConfig config;
    config.api_key = api_key;
    config.workspace_id = workspace_id;
    online_music::OnlineMusicAsr protocol;
    const bool configured = protocol.configure(config) &&
                            protocol.build_endpoint(config, &endpoint) &&
                            protocol.build_authorization_header(config, &auth);
    esp_transport_handle_t ssl = nullptr;
    esp_transport_handle_t ws = nullptr;
    std::uint32_t generation = ++generation_;
    if (generation == 0U) generation = ++generation_;
    bool mic_started = false;
    if (configured && audio_ != nullptr) {
      failure = OnlineMusicAsrFailure::WifiUnavailable;
    }
    if (configured && audio_ != nullptr && audio_->ensure_internet_ready()) {
      std::string host, path;
      failure = OnlineMusicAsrFailure::EndpointInvalid;
      if (split_endpoint(endpoint, &host, &path)) {
        ssl = esp_transport_ssl_init();
        ws = ssl == nullptr ? nullptr : esp_transport_ws_init(ssl);
        failure = OnlineMusicAsrFailure::TransportInit;
        if (ssl != nullptr && ws != nullptr) {
          esp_transport_ssl_crt_bundle_attach(ssl, esp_crt_bundle_attach);
          esp_transport_ws_set_path(ws, path.c_str());
          esp_transport_ws_set_auth(ws, auth.c_str());
          const std::string task_id = make_task_id();
          failure = OnlineMusicAsrFailure::Connect;
          const bool connected =
              esp_transport_connect(ws, host.c_str(), 443, 12000) == 0;
          websocket_http_status_.store(
              esp_transport_ws_get_upgrade_request_status(ws),
              std::memory_order_release);
          failure = connected ? OnlineMusicAsrFailure::TaskRequest : failure;
          if (connected && protocol.start(task_id, &run_frame) &&
              send_frame(ws, WS_TRANSPORT_OPCODES_TEXT, run_frame.data(), run_frame.size())) {
            failure = OnlineMusicAsrFailure::TaskStartTimeout;
            char buffer[8192]{};
            online_music::WsTextAssembler assembler;
            bool read_fatal = false;
            const auto read_event = [&](std::uint32_t timeout,
                                        online_music::OnlineMusicAsrParsedEvent* event) {
              const int readable =
                  esp_transport_poll_read(ws, static_cast<int>(timeout));
              if (readable < 0) {
                read_fatal = true;
                return false;
              }
              if (readable == 0) return false;
              const int n = esp_transport_read(ws, buffer, sizeof(buffer) - 1,
                                               1000);
              if (n < 0) {
                read_fatal = true;
                return false;
              }
              if (n == 0) return false;
              const auto opcode = esp_transport_ws_get_read_opcode(ws);
              const int payload_len = esp_transport_ws_get_read_payload_len(ws);
              if (payload_len <= 0) {
                read_fatal = true;
                return false;
              }
              std::string message;
              const auto assembled = assembler.append(
                  static_cast<std::uint8_t>(opcode),
                  esp_transport_ws_get_fin_flag(ws),
                  static_cast<std::size_t>(payload_len), buffer,
                  static_cast<std::size_t>(n), &message);
              if (assembled == online_music::WsTextAssemblyResult::ProtocolError ||
                  assembled == online_music::WsTextAssemblyResult::TooLarge) {
                read_fatal = true;
                return false;
              }
              return assembled == online_music::WsTextAssemblyResult::MessageReady &&
                     protocol.ingest_server_event(message, event);
            };
            online_music::OnlineMusicAsrParsedEvent event;
            const TickType_t started_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(5000U);
            bool task_started = false;
            bool server_failed = false;
            while (!read_fatal &&
                   !stop_requested_.load(std::memory_order_acquire) &&
                   xTaskGetTickCount() < started_deadline) {
              if (read_event(100U, &event)) {
                if (event.event == online_music::OnlineMusicAsrEvent::TaskStarted) {
                  task_started = true;
                  break;
                }
                if (event.event == online_music::OnlineMusicAsrEvent::TaskFailed) {
                  failure = OnlineMusicAsrFailure::ServerTaskFailed;
                  server_error_code = event.error_code;
                  server_failed = true;
                  break;
                }
              }
            }
            if (read_fatal) failure = OnlineMusicAsrFailure::WebSocketRead;
            if (task_started && !stop_requested_.load(std::memory_order_acquire) &&
                audio_->start_microphone_capture(generation)) {
              failure = OnlineMusicAsrFailure::AudioSend;
              mic_started = true;
              capture_started_pending_.store(true, std::memory_order_release);
              if (owner_task_ != nullptr) xTaskNotifyGive(owner_task_);
              online_music::PcmChunker chunker;
              AudioSinkContext sink{ws, &protocol};
              bool audio_ok = true;
              std::size_t captured_bytes = 0U;
              const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(10000U);
              while (!stop_requested_.load(std::memory_order_acquire) &&
                     !finish_requested_.load(std::memory_order_acquire) &&
                     xTaskGetTickCount() < deadline && audio_ok &&
                     !server_failed) {
                std::array<std::uint8_t, kFrameBytes> frame{};
                std::size_t bytes = 0U;
                if (audio_->read_microphone_frame(frame.data(), frame.size(), &bytes) != ESP_OK || bytes == 0U) continue;
                captured_bytes += bytes;
                audio_ok = chunker.append(frame.data(), bytes,
                                          send_audio_chunk, &sink);
                if (read_event(0U, &event) &&
                    event.event == online_music::OnlineMusicAsrEvent::TaskFailed) {
                  failure = OnlineMusicAsrFailure::ServerTaskFailed;
                  server_error_code = event.error_code;
                  server_failed = true;
                }
              }
              if (read_fatal) {
                failure = OnlineMusicAsrFailure::WebSocketRead;
                audio_ok = false;
              } else if (captured_bytes == 0U) {
                failure = OnlineMusicAsrFailure::MicrophoneRead;
                audio_ok = false;
              } else if (!audio_ok) {
                failure = OnlineMusicAsrFailure::AudioSend;
              }
              std::string finish_frame;
              if (audio_ok && !server_failed &&
                  !stop_requested_.load(std::memory_order_acquire)) {
                failure = OnlineMusicAsrFailure::AudioFlush;
                audio_ok = chunker.finish(send_audio_chunk, &sink);
              }
              if (audio_ok) failure = OnlineMusicAsrFailure::FinishRequest;
              if (audio_ok && !server_failed &&
                  !stop_requested_.load(std::memory_order_acquire) &&
                  protocol.finish(&finish_frame) &&
                  send_frame(ws, WS_TRANSPORT_OPCODES_TEXT, finish_frame.data(), finish_frame.size())) {
                failure = OnlineMusicAsrFailure::ResultTimeout;
                const TickType_t final_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(5000U);
                bool task_finished = false;
                while (!read_fatal &&
                       !stop_requested_.load(std::memory_order_acquire) &&
                       xTaskGetTickCount() < final_deadline) {
                  if (!read_event(100U, &event)) continue;
                  if (event.event == online_music::OnlineMusicAsrEvent::TaskFinished) {
                    task_finished = true;
                    break;
                  } else if (event.event == online_music::OnlineMusicAsrEvent::TaskFailed) {
                    failure = OnlineMusicAsrFailure::ServerTaskFailed;
                    server_error_code = event.error_code;
                    server_failed = true;
                    break;
                  }
                }
                transcript = protocol.transcript();
                success = task_finished && !transcript.empty();
                if (read_fatal) {
                  failure = OnlineMusicAsrFailure::WebSocketRead;
                } else if (task_finished && transcript.empty()) {
                  failure = OnlineMusicAsrFailure::EmptyTranscript;
                }
              }
            } else if (task_started) {
              failure = OnlineMusicAsrFailure::MicrophoneStart;
            }
          }
        }
      }
    }
    if (mic_started && audio_ != nullptr) audio_->stop_microphone_capture(generation);
    if (ws != nullptr) {
      esp_transport_close(ws);
      esp_transport_destroy(ws);
    }
    if (ssl != nullptr) esp_transport_destroy(ssl);
    protocol.configure({});
    api_key.clear();
    workspace_id.clear();
    auth.clear();
    if (success) {
      failure = OnlineMusicAsrFailure::None;
    } else if (stop_requested_.load(std::memory_order_acquire)) {
      failure = OnlineMusicAsrFailure::Cancelled;
    } else {
      ESP_LOGW("online_asr", "recognition failed stage=%s",
               online_music_asr_failure_name(failure));
    }
    last_failure_.store(failure, std::memory_order_release);
    busy_.store(false, std::memory_order_release);
    result_success_.store(success && !transcript.empty() && !stop_requested_.load(std::memory_order_acquire), std::memory_order_release);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    result_text_ = transcript;
    server_error_code_ = server_error_code;
    xSemaphoreGive(mutex_);
    result_pending_.store(true, std::memory_order_release);
    if (owner_task_ != nullptr) xTaskNotifyGive(owner_task_);
    stop_requested_.store(false, std::memory_order_release);
    finish_requested_.store(false, std::memory_order_release);
  }
}
}  // namespace easy_input
