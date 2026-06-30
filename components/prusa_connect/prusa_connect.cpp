#include "prusa_connect.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lwip/sockets.h"
#include <Arduino.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace prusa_connect {

static const char *const TAG = "prusa_connect";

static const char *reset_reason_str(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "Power-on";
    case ESP_RST_EXT:       return "External pin";
    case ESP_RST_SW:        return "Software (esp_restart)";
    case ESP_RST_PANIC:     return "Panic / exception";
    case ESP_RST_INT_WDT:   return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "Task watchdog";
    case ESP_RST_WDT:       return "Other watchdog";
    case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
    case ESP_RST_BROWNOUT:  return "Brownout";
    default:                return "Unknown";
  }
}

static const char *const SNAPSHOT_URL = "https://connect.prusa3d.com/c/snapshot";
static const char *const INFO_URL = "https://connect.prusa3d.com/c/info";

// ---------- setup / loop ----------

void PrusaConnectComponent::setup() {
  uint64_t mac = ESP.getEfuseMac();
  char fp[17];
  snprintf(fp, sizeof(fp), "%016llx", mac);
  fingerprint_ = fp;
  ESP_LOGI(TAG, "Device fingerprint: %s", fingerprint_.c_str());

  last_reset_reason_ = reset_reason_str(esp_reset_reason());
  ESP_LOGI(TAG, "Firmware: %s", ESPHOME_PROJECT_VERSION);
  ESP_LOGW(TAG, "Reset reason: %s", last_reset_reason_.c_str());

  psram_buf_ = static_cast<uint8_t *>(heap_caps_malloc(200000, MALLOC_CAP_SPIRAM));
  if (psram_buf_ == nullptr) {
    ESP_LOGE(TAG, "PSRAM alloc failed — uploads disabled");
    return;
  }

  upload_trigger_ = xSemaphoreCreateBinary();
  buf_mutex_ = xSemaphoreCreateMutex();
  xTaskCreate(run_upload_task_, "prusa_upload", 16384, this, 5, &upload_task_handle_);
  ESP_LOGI(TAG, "Upload task started");
}

void PrusaConnectComponent::loop() {
  uint32_t now = millis();

  if (now - last_age_update_ms_ >= 10000) {
    last_age_update_ms_ = now;
    if (upload_age_sensor_ != nullptr && last_upload_ms_ > 0)
      upload_age_sensor_->publish_state((now - last_upload_ms_) / 1000.0f);
  }

  // Watchdog: reboot if camera has produced no uploadable frames for 5 minutes.
  // This recovers from OV2640 driver stalls that stop on_image from firing.
  if (last_upload_ms_ > 0 && (now - last_upload_ms_) >= 300000) {
    ESP_LOGE(TAG, "No upload queued for 5 minutes — camera stalled, rebooting");
    App.reboot();
  }

  // Process result posted by upload task
  int32_t result = upload_result_.exchange(-1, std::memory_order_acq_rel);
  if (result == -1) return;

  // Update upload statistics
  total_attempts_++;
  if (result == 200 || result == 204) {
    successful_uploads_++;
    consecutive_successes_++;
    last_success_ms_ = millis();
  } else {
    failed_uploads_++;
    consecutive_successes_ = 0;
  }

  switch (result) {
    case 200:
    case 204:
      ESP_LOGI(TAG, "Snapshot uploaded OK");
      if (upload_success_sensor_ != nullptr) upload_success_sensor_->publish_state(true);
      if (upload_status_sensor_ != nullptr) upload_status_sensor_->publish_state("OK");
      break;
    case 429:
      ESP_LOGW(TAG, "Rate limited (429) — skipping next cycle");
      skip_next_ = true;
      if (upload_success_sensor_ != nullptr) upload_success_sensor_->publish_state(false);
      if (upload_status_sensor_ != nullptr) upload_status_sensor_->publish_state("Rate limited");
      break;
    case 401:
    case 403:
      ESP_LOGE(TAG, "Auth error (%d) — check prusa_token in secrets.yaml", (int) result);
      if (upload_success_sensor_ != nullptr) upload_success_sensor_->publish_state(false);
      if (upload_status_sensor_ != nullptr) upload_status_sensor_->publish_state("Auth error");
      break;
    case 404:
      ESP_LOGE(TAG, "Camera not registered (404) — register via Prusa Connect web UI");
      if (upload_success_sensor_ != nullptr) upload_success_sensor_->publish_state(false);
      if (upload_status_sensor_ != nullptr) upload_status_sensor_->publish_state("Not registered");
      break;
    case 503:
      ESP_LOGW(TAG, "Server unavailable (503) — will retry next interval");
      if (upload_success_sensor_ != nullptr) upload_success_sensor_->publish_state(false);
      if (upload_status_sensor_ != nullptr) upload_status_sensor_->publish_state("Server unavailable");
      break;
    case 0:
      ESP_LOGW(TAG, "Network error during upload");
      if (upload_success_sensor_ != nullptr) upload_success_sensor_->publish_state(false);
      if (upload_status_sensor_ != nullptr) upload_status_sensor_->publish_state("Network error");
      break;
    default:
      ESP_LOGW(TAG, "Unexpected HTTP response: %d", (int) result);
      if (upload_success_sensor_ != nullptr) upload_success_sensor_->publish_state(false);
      if (upload_status_sensor_ != nullptr) {
        char buf[16];
        snprintf(buf, sizeof(buf), "HTTP %d", (int) result);
        upload_status_sensor_->publish_state(buf);
      }
      break;
  }

  log_stats();
}

// ---------- on_image (main loop) ----------

void PrusaConnectComponent::on_image(const esp32_camera::CameraImageData &image) {
  if (!image.data || image.length == 0) return;
  if (psram_buf_ == nullptr) return;

  if ((millis() - last_upload_ms_) < interval_ms_) return;

  if (skip_next_) {
    skip_next_ = false;
    last_upload_ms_ = millis();
    return;
  }

  // Non-blocking: if task is still uploading the previous frame, skip this one.
  // last_upload_ms_ is set only when we successfully queue, so the 30s interval
  // stays anchored to the last queued (not last completed) upload.
  if (xSemaphoreTake(buf_mutex_, 0) != pdTRUE) return;

  memcpy(psram_buf_, image.data, image.length);
  psram_len_ = image.length;
  last_upload_ms_ = millis();

  xSemaphoreGive(buf_mutex_);
  xSemaphoreGive(upload_trigger_);

  ESP_LOGI(TAG, "Snapshot queued: %zu bytes", image.length);
}

// ---------- background upload task ----------

void PrusaConnectComponent::run_upload_task_(void *arg) {
  auto *self = static_cast<PrusaConnectComponent *>(arg);
  for (;;) {
    xSemaphoreTake(self->upload_trigger_, portMAX_DELAY);

    // Snapshot gets priority — clean TLS heap before any other connection.
    xSemaphoreTake(self->buf_mutex_, portMAX_DELAY);
    int result = self->send_snapshot_(self->psram_buf_, self->psram_len_);
    xSemaphoreGive(self->buf_mutex_);

    self->upload_result_.store(result, std::memory_order_release);

    // Register with Prusa Connect only after a successful snapshot so we know the
    // heap can sustain a second TLS connection.  Retried every 30 s on failure.
    if (result > 0 &&
        !self->info_sent_.load(std::memory_order_relaxed) &&
        millis() >= self->info_retry_after_ms_.load(std::memory_order_relaxed)) {
      self->send_info_();
    }
  }
}

// ---------- HTTPS helpers ----------

int PrusaConnectComponent::send_snapshot_(const uint8_t *data, size_t length) {
  ESP_LOGI(TAG, "Upload starting: %zu bytes, internal heap free: %u, largest block: %u",
           length,
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

  esp_http_client_config_t cfg = {};
  cfg.url = SNAPSHOT_URL;
  cfg.method = HTTP_METHOD_PUT;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 10000;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr) {
    ESP_LOGE(TAG, "Failed to init HTTP client");
    return 0;
  }

  const char *tok = (token_text_ != nullptr && !token_text_->state.empty())
                    ? token_text_->state.c_str() : token_;
  esp_http_client_set_header(client, "Token", tok);
  esp_http_client_set_header(client, "Fingerprint", fingerprint_.c_str());
  esp_http_client_set_header(client, "Content-Type", "image/jpg");

  int status = 0;
  esp_err_t err = esp_http_client_open(client, (int) length);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Connection failed: %s", esp_err_to_name(err));
  }
  if (err == ESP_OK) {
    // Disable Nagle's algorithm so each chunk is sent immediately (matches reference firmware).
    // No SO_SNDTIMEO: it corrupts lwIP socket state mid-upload (errno EINPROGRESS on next send).
    // The write_deadline below is the sole guard against a hung connection.
    int sock = esp_http_client_get_socket(client);
    if (sock >= 0) {
      int flag = 1;
      setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    }

    const size_t CHUNK = 2048;
    size_t written = 0;
    TickType_t write_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(30000);

    while (written < length) {
      if (xTaskGetTickCount() > write_deadline) {
        ESP_LOGW(TAG, "Write timed out at %zu/%zu bytes, errno %d (%s)", written, length, errno, strerror(errno));
        err = ESP_FAIL;
        break;
      }
      size_t to_write = length - written;
      if (to_write > CHUNK) to_write = CHUNK;
      int n = esp_http_client_write(client, reinterpret_cast<const char *>(data) + written, (int) to_write);
      if (n < 0) {
        ESP_LOGE(TAG, "Write failed at offset %zu, errno %d (%s)", written, errno, strerror(errno));
        err = ESP_FAIL;
        break;
      }
      written += (size_t) n;
    }
    if (err == ESP_OK) {
      esp_http_client_fetch_headers(client);
      status = esp_http_client_get_status_code(client);
    }
  }
  esp_http_client_cleanup(client);

  ESP_LOGI(TAG, "Upload done (%s), internal heap free: %u, largest block: %u",
           esp_err_to_name(err),
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "HTTP error: %s", esp_err_to_name(err));
    return 0;
  }

  return status;
}

void PrusaConnectComponent::send_info_() {
  char json[1024];
  snprintf(json, sizeof(json),
    "{"
      "\"config\":{"
        "\"name\":\"%s\","
        "\"trigger_scheme\":\"%s\","
        "\"firmware\":\"" ESPHOME_PROJECT_VERSION "\","
        "\"manufacturer\":\"Espressif\","
        "\"model\":\"ESP32-CAM\""
      "},"
      "\"options\":{"
        "\"available_resolutions\":["
          "{\"width\":160,\"height\":120},"
          "{\"width\":320,\"height\":240},"
          "{\"width\":640,\"height\":480},"
          "{\"width\":800,\"height\":600},"
          "{\"width\":1024,\"height\":768},"
          "{\"width\":1280,\"height\":1024},"
          "{\"width\":1600,\"height\":1200}"
        "]"
      "},"
      "\"capabilities\":[\"imaging\",\"resolution\",\"trigger_scheme\"]"
    "}",
    camera_name_, get_trigger_scheme_().c_str());

  esp_http_client_config_t cfg = {};
  cfg.url = INFO_URL;
  cfg.method = HTTP_METHOD_PUT;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 10000;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr) {
    ESP_LOGE(TAG, "Failed to init HTTP client for /c/info");
    return;
  }

  const char *tok = (token_text_ != nullptr && !token_text_->state.empty())
                    ? token_text_->state.c_str() : token_;
  esp_http_client_set_header(client, "Token", tok);
  esp_http_client_set_header(client, "Fingerprint", fingerprint_.c_str());
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, json, strlen(json));

  esp_err_t err = esp_http_client_perform(client);
  int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : 0;
  esp_http_client_cleanup(client);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Camera info sent, status: %d", status);
    info_sent_.store(true, std::memory_order_relaxed);
  } else {
    ESP_LOGW(TAG, "Failed to send camera info: %s — will retry in 30s", esp_err_to_name(err));
    info_retry_after_ms_.store(millis() + 30000, std::memory_order_relaxed);
  }
}

void PrusaConnectComponent::log_stats() const {
  uint32_t uptime = millis() / 1000;
  uint32_t heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  uint32_t heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  if (last_success_ms_ == 0) {
    ESP_LOGI(TAG, "Stats | attempts:%u  ok:%u  consec:%u  fail:%u  last_ok:never  reset:%s  uptime:%us  heap:%u/%u",
             total_attempts_, successful_uploads_, consecutive_successes_, failed_uploads_,
             last_reset_reason_.c_str(), uptime, heap_free, heap_largest);
  } else {
    uint32_t since_ok = (millis() - last_success_ms_) / 1000;
    ESP_LOGI(TAG, "Stats | attempts:%u  ok:%u  consec:%u  fail:%u  last_ok:%us ago  reset:%s  uptime:%us  heap:%u/%u",
             total_attempts_, successful_uploads_, consecutive_successes_, failed_uploads_, since_ok,
             last_reset_reason_.c_str(), uptime, heap_free, heap_largest);
  }
  if (reset_reason_sensor_ != nullptr) reset_reason_sensor_->publish_state(last_reset_reason_);
  if (debug_mode_) {
    if (upload_total_sensor_ != nullptr)       upload_total_sensor_->publish_state(total_attempts_);
    if (upload_fail_sensor_ != nullptr)        upload_fail_sensor_->publish_state(failed_uploads_);
    if (upload_consecutive_sensor_ != nullptr) upload_consecutive_sensor_->publish_state(consecutive_successes_);
    if (heap_largest_sensor_ != nullptr)       heap_largest_sensor_->publish_state(heap_largest);
    if (heap_free_sensor_ != nullptr)          heap_free_sensor_->publish_state(heap_free);
    if (firmware_version_sensor_ != nullptr)   firmware_version_sensor_->publish_state(ESPHOME_PROJECT_VERSION);
  }
}

std::string PrusaConnectComponent::get_trigger_scheme_() const {
  uint32_t secs = interval_ms_ / 1000;
  if (secs <= 10) return "TEN_SEC";
  if (secs <= 30) return "THIRTY_SEC";
  if (secs <= 60) return "SIXTY_SEC";
  return "MANUAL";
}

}  // namespace prusa_connect
}  // namespace esphome
