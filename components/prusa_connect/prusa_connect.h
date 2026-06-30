#pragma once

#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/text/text.h"
#include "esphome/components/esp32_camera/esp32_camera.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <atomic>
#include <string>

namespace esphome {
namespace prusa_connect {

class PrusaConnectComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void on_image(const esp32_camera::CameraImageData &image);
  void log_stats() const;

  void set_token(const char *token) { token_ = token; }
  void set_camera_name(const char *name) { camera_name_ = name; }
  void set_interval_ms(uint32_t ms) { interval_ms_ = ms; }
  void set_debug_mode(bool debug) { debug_mode_ = debug; }
  void set_upload_success_sensor(binary_sensor::BinarySensor *s) { upload_success_sensor_ = s; }
  void set_upload_status_sensor(text_sensor::TextSensor *s) { upload_status_sensor_ = s; }
  void set_upload_age_sensor(sensor::Sensor *s) { upload_age_sensor_ = s; }
  void set_reset_reason_sensor(text_sensor::TextSensor *s) { reset_reason_sensor_ = s; }
  void set_upload_total_sensor(sensor::Sensor *s) { upload_total_sensor_ = s; }
  void set_upload_fail_sensor(sensor::Sensor *s) { upload_fail_sensor_ = s; }
  void set_upload_consecutive_sensor(sensor::Sensor *s) { upload_consecutive_sensor_ = s; }
  void set_heap_largest_sensor(sensor::Sensor *s) { heap_largest_sensor_ = s; }
  void set_heap_free_sensor(sensor::Sensor *s) { heap_free_sensor_ = s; }
  void set_firmware_version_sensor(text_sensor::TextSensor *s) { firmware_version_sensor_ = s; }
  void set_token_text(text::Text *t) { token_text_ = t; }

 protected:
  int send_snapshot_(const uint8_t *data, size_t length);
  void send_info_();
  std::string get_trigger_scheme_() const;
  static void run_upload_task_(void *arg);

  const char *token_{};
  const char *camera_name_{"ESP32 Camera"};
  uint32_t interval_ms_{30000};
  bool debug_mode_{false};
  std::string fingerprint_;

  uint32_t last_upload_ms_{0};
  uint32_t last_age_update_ms_{0};
  bool skip_next_{false};

  // Upload statistics — updated in loop() only, no atomics required
  uint32_t total_attempts_{0};
  uint32_t successful_uploads_{0};
  uint32_t consecutive_successes_{0};
  uint32_t failed_uploads_{0};
  uint32_t last_success_ms_{0};
  std::string last_reset_reason_{"Unknown"};

  // Accessed from both camera task (on_image) and upload task — must be atomic.
  std::atomic<bool> info_sent_{false};
  std::atomic<uint32_t> info_retry_after_ms_{0};

  // Background upload task (FreeRTOS)
  TaskHandle_t upload_task_handle_{nullptr};
  SemaphoreHandle_t upload_trigger_{nullptr};  // binary: signals "buffer ready"
  SemaphoreHandle_t buf_mutex_{nullptr};       // guards psram_buf_ / psram_len_
  uint8_t *psram_buf_{nullptr};               // PSRAM copy of camera frame
  size_t psram_len_{0};
  std::atomic<int32_t> upload_result_{-1};    // task → loop: -1 = nothing pending, 0 = net error, >0 = HTTP status

  binary_sensor::BinarySensor *upload_success_sensor_{};
  text_sensor::TextSensor *upload_status_sensor_{};
  sensor::Sensor *upload_age_sensor_{};
  text_sensor::TextSensor *reset_reason_sensor_{};
  sensor::Sensor *upload_total_sensor_{};
  sensor::Sensor *upload_fail_sensor_{};
  sensor::Sensor *upload_consecutive_sensor_{};
  sensor::Sensor *heap_largest_sensor_{};
  sensor::Sensor *heap_free_sensor_{};
  text_sensor::TextSensor *firmware_version_sensor_{};
  text::Text *token_text_{};
};

}  // namespace prusa_connect
}  // namespace esphome
