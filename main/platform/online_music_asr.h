#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "online_music/online_music_asr.h"
namespace easy_input {
class KeyboardAudioLink;

enum class OnlineMusicAsrFailure : std::uint8_t {
  None,
  NotConfigured,
  WorkerAllocation,
  ProtocolConfig,
  WifiUnavailable,
  EndpointInvalid,
  TransportInit,
  Connect,
  TaskRequest,
  TaskStartTimeout,
  ServerTaskFailed,
  WebSocketRead,
  MicrophoneStart,
  MicrophoneRead,
  AudioSend,
  AudioFlush,
  FinishRequest,
  ResultTimeout,
  EmptyTranscript,
  Cancelled,
};

struct OnlineMusicAsrDiagnostics {
  bool credentials_configured = false;
  bool busy = false;
  OnlineMusicAsrFailure last_failure = OnlineMusicAsrFailure::None;
  int websocket_http_status = -1;
  std::string server_error_code;
};

const char* online_music_asr_failure_name(OnlineMusicAsrFailure failure);

class OnlineMusicAsr {
 public:
  esp_err_t begin(TaskHandle_t owner, KeyboardAudioLink* audio);
  void configure(const std::string& api_key, const std::string& workspace_id);
  bool start();
  void finish_input();
  void stop();
  bool busy() const;
  OnlineMusicAsrDiagnostics diagnostics() const;
  bool take_capture_started();
  bool take_result(bool* succeeded, std::string* text);
 private:
  static void task_entry(void* arg);
  bool ensure_worker();
  void run();
  TaskHandle_t owner_task_ = nullptr;
  TaskHandle_t worker_task_ = nullptr;
  KeyboardAudioLink* audio_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  std::string api_key_, workspace_id_, result_text_, server_error_code_;
  std::atomic<bool> requested_{false}, finish_requested_{false}, stop_requested_{false}, busy_{false};
  std::atomic<bool> capture_started_pending_{false};
  std::atomic<bool> result_pending_{false}, result_success_{false};
  std::atomic<bool> credentials_configured_{false};
  std::atomic<OnlineMusicAsrFailure> last_failure_{OnlineMusicAsrFailure::None};
  std::atomic<int> websocket_http_status_{-1};
  std::uint32_t generation_ = 0U;
  online_music::OnlineMusicAsr protocol_;
  bool initialized_ = false;
};
}  // namespace easy_input
