# ESPHome Prusa Connect Component

An ESPHome external component that uploads snapshots from an AI Thinker ESP32-CAM to
[Prusa Connect](https://connect.prusa3d.com) on a configurable interval, giving you
camera monitoring of your Prusa 3D printer without Prusa's own camera hardware.

## Hardware

- **Board:** AI Thinker ESP32-CAM (OV2640 sensor, 8 MB external PSRAM)
- **Framework:** Arduino — required, the component uses `ESP.getEfuseMac()` for the device fingerprint
- Not tested on other ESP32-CAM variants

## How it works

The component registers an `on_image` callback with ESPHome's `esp32_camera` component.
When a frame arrives and the configured interval has elapsed, the JPEG is copied into PSRAM
and a FreeRTOS background task uploads it to Prusa Connect via HTTPS PUT.
The background task keeps the 6–15 second TLS upload off the ESPHome main loop so the
device stays responsive.

## Installation

Reference this repository in your ESPHome YAML:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/scriptengine/esphome-prusa-connect
      ref: main
    components: [prusa_connect]
```

## Minimal configuration

```yaml
esp32:
  board: esp32cam
  framework:
    type: arduino

psram:

esp32_camera:
  # AI Thinker ESP32-CAM pin assignments — adjust for your board
  external_clock:
    pin: GPIO0
    frequency: 20MHz
  i2c_id: camera_i2c
  data_pins: [GPIO5, GPIO18, GPIO19, GPIO21, GPIO36, GPIO39, GPIO34, GPIO35]
  vsync_pin: GPIO25
  href_pin: GPIO23
  pixel_clock_pin: GPIO22
  power_down_pin: GPIO32
  max_framerate: 1fps
  idle_framerate: 1fps
  resolution: UXGA
  on_image:
    then:
      - lambda: id(prusa_cam).on_image(image);

i2c:
  sda: GPIO26
  scl: GPIO27
  id: camera_i2c

prusa_connect:
  id: prusa_cam
  token: !secret prusa_token
  camera_name: My Printer Camera
  interval: 30s
```

## Configuration options

| Option | Required | Default | Description |
|--------|----------|---------|-------------|
| `token` | Yes | — | Prusa Connect camera token (from the Prusa Connect web UI) |
| `camera_name` | No | `ESP32 Camera` | Camera name shown in Prusa Connect |
| `interval` | No | `30s` | Upload interval |
| `debug_mode` | No | `false` | Enables extended sensor reporting (heap, counters) |
| `token_text` | No | — | ID of a `text` entity whose value overrides `token` at runtime |

## Optional sensors

Declare template sensors in your YAML and reference their IDs in `prusa_connect:`.
All are optional.

| Config key | Sensor type | Description |
|------------|-------------|-------------|
| `upload_success_sensor` | `binary_sensor` | `true` after a successful upload |
| `upload_status_sensor` | `text_sensor` | Last result: `OK`, `Rate limited`, `Auth error`, etc. |
| `upload_age_sensor` | `sensor` | Seconds since the last frame was queued |
| `reset_reason_sensor` | `text_sensor` | ESP32 reset reason (Power-on, Watchdog, etc.) |
| `upload_total_sensor` | `sensor` | Total upload attempts *(debug_mode only)* |
| `upload_fail_sensor` | `sensor` | Total failed uploads *(debug_mode only)* |
| `upload_consecutive_sensor` | `sensor` | Consecutive successful uploads *(debug_mode only)* |
| `heap_largest_sensor` | `sensor` | Largest free internal heap block in bytes *(debug_mode only)* |
| `heap_free_sensor` | `sensor` | Free internal heap in bytes *(debug_mode only)* |
| `firmware_version_sensor` | `text_sensor` | ESPHome project version string *(debug_mode only)* |

## Runtime token update

The token can be changed without reflashing. Declare a `text` entity and pass it as
`token_text` — if set and non-empty it takes priority over the `token` config value:

```yaml
text:
  - platform: template
    name: Prusa Token
    id: prusa_token_runtime
    mode: password
    restore_value: true
    initial_value: !secret prusa_token
    set_action:
      - lambda: "return;"

prusa_connect:
  id: prusa_cam
  token: !secret prusa_token
  token_text: prusa_token_runtime
  camera_name: My Printer Camera
  interval: 30s
```

## Prusa Connect setup

1. Log in to [connect.prusa3d.com](https://connect.prusa3d.com)
2. Add a camera — choose **Other camera**
3. Copy the token shown and add it to your `secrets.yaml` as `prusa_token`
4. Flash your ESP32-CAM — on the first successful snapshot the device registers automatically

## Technical notes

- PSRAM buffer is fixed at 200 KB. UXGA at JPEG quality 10 produces ~60 KB — ample headroom.
- `SO_SNDTIMEO` / `SO_RCVTIMEO` are deliberately **not** set. They corrupt the lwIP socket
  state under this ESP-IDF version (errno `EINPROGRESS` on subsequent sends). A 30-second
  write deadline is used instead.
- `TCP_NODELAY` is set — matches the
  [official Prusa ESP32-Cam firmware](https://github.com/prusa3d/Prusa-Firmware-ESP32-Cam).
- The watchdog in `loop()` reboots the device if no image has been queued in 5 minutes,
  recovering OV2640 driver stalls.
- Chunk size is 2048 bytes, matching the reference firmware.

## Version history

| Version | Changes |
|---------|---------|
| 1.0.0 | Initial component — synchronous upload in the main loop |
| 1.1.0 | FreeRTOS background task, PSRAM copy buffer, atomic result passing |
| 1.1.1 | `camera_hmirror` correction for mirrored images |
| 1.1.2 | Fixed `info_sent_` unconditional override; 30 s retry on `send_info_` failure |
| 1.1.3 | SO_SNDTIMEO + SO_RCVTIMEO + 15 s write deadline to fix 80 s upload stalls |
| 1.2.0 | `send_info_()` moved to background task; `info_sent_` / `info_retry_after_ms_` made atomic |
| 1.2.1 | `send_info_()` runs after `send_snapshot_()` succeeds — snapshot gets priority heap access |
| 1.2.2 | errno + strerror logged on write failure / timeout |
| 1.3.0 | Removed SO_SNDTIMEO / SO_RCVTIMEO (caused errno 119); added TCP_NODELAY; chunk 4096→2048; deadline 15 s→30 s |

## Licence

MIT
