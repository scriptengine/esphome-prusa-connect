# ESPHome Prusa Connect Camera — AI Thinker ESP32-CAM

This project creates the software to upload images from an ESP32-CAM (AI Thinker ESP32-CAM) to [Prusa Connect](https://connect.prusa3d.com).
Prusa Connect has a camera feature that presents the last picture from cameras associated with your Prusa Printer.
I use ESP32-CAM rather than the Prusa Buddy camera as I have a case that tucks tightly into the RIGHT hand front corner of the frame so I can
have a left and right camera view and two different views of my print, not just one. Additionally, the ESP32s can be purchased for £10 and the cameras easily replaced, so if the heat or the fumes of the chamber damage them it's not the most costly of problems.

This software's unique point is it uses [ESPHome](https://esphome.io/) which:
- Allows it to be controlled by and for data to be sent to [Home Assistant](https://www.home-assistant.io/)
- (If you use Home Assistant or ESPHome) you can modify the ESP32 hardware and add for example a temperature or VOC sensor
- (Minor) You can load the ESP32 from ESPHome rather than fiddle with ESP software flasher

The original software, which works just fine and is recommended as it supports other hardware, is:
https://github.com/prusa3d/Prusa-Firmware-ESP32-Cam

I ran the Prusa-Firmware-ESP32-Cam on multiple ESP32-CAM for over a year. It's great. I created this version as I want to add sensors to the ESP32
devices inside the Prusa CoreOne.

I have created 3D printable camera cases for the ESP32s on Printables:
<link required>

---

## Hardware

- **Board:** AI Thinker ESP32-CAM (OV2640 sensor, 8 MB external PSRAM)
- **Framework:** Arduino — required; the component uses `ESP.getEfuseMac()` for the device fingerprint
- Not tested on other ESP32-CAM variants

---

## Limitations

There is no official support. If you are not familiar with ESPHome and ESP32 devices, stick with the original Prusa-Firmware-ESP32-Cam.
This software only works for the ESP32-CAM, but if you know ESPHome you will know how to change the YAML to work with your own devices.

---

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

---

## Prusa Connect Setup

1. Log in to [connect.prusa3d.com](https://connect.prusa3d.com)
2. Add a camera — choose **Other camera**
3. Copy the token shown and add it to your `secrets.yaml` as `prusa_token`
4. Flash your ESP32-CAM — on the first successful snapshot the device registers automatically

---

## Required Settings Before First Flash

### 1. `secrets.yaml`

Create `secrets.yaml` in the same directory as the device YAML. This file should not be committed to version control. It must contain at minimum:

```yaml
wifi_ssid: "YourNetworkName"
wifi_password: "YourWiFiPassword"
prusa_token: "your-prusa-connect-camera-token"
```

### 2. API Encryption Key

Each device YAML contains a unique encryption key for the Home Assistant native API:

```yaml
api:
  encryption:
    key: "base64encodedkey="
```

Generate a fresh key for each device using the ESPHome dashboard (create a new device and copy the generated key). **Do not reuse keys between devices.**

### 3. Device Identity

In the device YAML, set:

| Field | Where | Example |
|-------|-------|---------|
| `esphome.name` | Top-level | `esp32cam-kitchen` |
| `esphome.friendly_name` | Top-level | `esp32cam-kitchen` |
| `esp32_camera.name` | Camera block | `Kitchen Camera` |
| `prusa_connect.camera_name` | prusa_connect block | `Kitchen Camera` |

The two camera name fields must match. The name appears in Prusa Connect.

### 4. Camera Orientation Defaults

The device YAML contains two globals that set the default camera orientation on first flash:

```yaml
globals:
  - id: hmirror_state
    type: bool
    restore_value: true
    initial_value: 'true'   # true = mirrored, false = normal
  - id: vflip_state
    type: bool
    restore_value: true
    initial_value: 'false'  # true = flipped, false = normal
```

Set these to match your physical mounting before flashing. They can be changed at runtime via the web interface without reflashing.

---

## Minimal Configuration

```yaml
esp32:
  board: esp32cam
  framework:
    type: arduino

psram:

esp32_camera:
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

---

## Configuration Reference

### `prusa_connect:` options

| Option | Required | Default | Description |
|--------|----------|---------|-------------|
| `token` | Yes | — | Prusa Connect camera token |
| `camera_name` | No | `ESP32 Camera` | Camera name shown in Prusa Connect |
| `interval` | No | `30s` | Upload interval |
| `debug_mode` | No | `false` | Enables extended sensor reporting (heap, counters) |
| `token_text` | No | — | ID of a `text` entity whose value overrides `token` at runtime |

### Optional sensors

Declare template sensors in your YAML and reference their IDs in `prusa_connect:`. All are optional.

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

### Runtime token update

The token can be changed without reflashing. Declare a `text` entity and pass it as `token_text`:

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

If `token_text` is set and non-empty it takes priority over the compiled-in `token` value.

---

## Usage

Once configured the cameras should just run.
To configure them you will need to add them to Prusa Connect (see [Prusa Connect Setup](#prusa-connect-setup) above).
They can be configured:
- In the YAML
- Via the web interface (see below)
- Via Home Assistant

Once the ESP32 is on the network there is a web interface at `http://[device-name].local/`
Additionally you can see the current picture at `http://[device-name].local:8080/`

For settings like camera rotation, brightness etc it's easier to use the web interface to fiddle around until you get the best picture.

### NVS persistence across flashes

Camera orientation (H-Mirror, V-Flip), Prusa Token, and other runtime settings are stored in ESP32 NVS (non-volatile flash storage). NVS is **preserved across OTA (wireless) firmware updates** but is **wiped by a full USB erase-and-flash**. After a full erase, all settings return to the compiled-in defaults (`initial_value` / `initial_option` fields in the YAML).

---

## Web Interface (port 80)

Browse to `http://[device-name].local/` to access the ESPHome web interface. Three groups of controls are shown.

### Settings

| Control | Description |
|---------|-------------|
| Prusa Token | Runtime override — stored in device flash, survives reboots and OTA updates. Set this if you need to change the token without reflashing. |
| Toggle H-Mirror | Flips the image horizontally — each press toggles the current state |
| Toggle V-Flip | Flips the image vertically — each press toggles the current state |
| Camera Brightness | −2 to +2 (default 0) |
| Camera Contrast | −2 to +2 (default 0) |
| Camera JPEG Quality | 4 (highest quality, largest file) to 63 (lowest quality) — default 10 |
| Camera Resolution | QVGA → UXGA — UXGA is the default and maximum supported |
| Camera Saturation | −2 to +2 (default 0) |
| Camera White Balance | auto / sunny / cloudy / office / home |
| Flash LED | Toggles the onboard illumination LED |
| Restart Device | Reboots the ESP32 immediately |

### Exposure and Gain Controls

| Control | Description |
|---------|-------------|
| Automatic Exposure Control (recommended: on) | Lets the sensor choose exposure automatically. Turn off to set exposure manually. |
| Automatic Gain Control (recommended: on) | Lets the sensor choose gain automatically. Turn off to apply AGC Gain Limit. |
| Manual exposure time | 0–1200 — only takes effect when AEC is off |
| AGC Gain Limit | Maximum sensor gain ceiling: 2× to 128× — only takes effect when AGC is off |

### Debug Logging

Controls the extended telemetry section (see [Debug Logging](#debug-logging) below).

---

## Live Video Stream

The camera streams MJPEG at:

```
http://[device-name].local:8080/
```

Use this to verify orientation and exposure in real time before and after adjusting settings. The stream runs at 1 fps independently of the Prusa Connect upload interval.

---

## Debug Logging

The **Debug Logging (off | on)** switch in the web interface (and as a switch entity in Home Assistant) enables extended upload and health telemetry.

When on, the following are published as Home Assistant sensor entities after each upload and every 30 seconds:

| Sensor | Description |
|--------|-------------|
| Upload Total | Cumulative upload attempts since last boot |
| Upload Fail | Cumulative failed uploads since last boot |
| Upload Consecutive | Consecutive successful uploads — resets to 0 on any failure |
| Heap Largest Block | Largest contiguous free block in internal heap (bytes) |
| Heap Free | Total free internal heap (bytes) |
| Firmware Version | Currently running version string |

The following are always published regardless of the debug switch:

| Sensor | Description |
|--------|-------------|
| Last Upload Success | Binary — was the last upload successful |
| Upload Status | Text — OK / Rate limited / Auth error / Network error / etc. |
| Seconds Since Last Upload | Seconds elapsed since the last frame was queued |
| WiFi Signal | RSSI in dBm, updated every 60 s |
| Reset Reason | Why the device last restarted |

**A summary statistics line is also written to the serial/API log every 30 seconds**, regardless of the debug switch:

```
Stats | attempts:47  ok:46  consec:28  fail:1  last_ok:0s ago  reset:Power-on  uptime:1423s  heap:130420/77824
```

To enable debug logging: flip the **Debug Logging (off | on)** switch to on in the web interface or via Home Assistant. The setting is stored in device flash and survives reboots.

---

## Known Issues and Things to Watch

### Heap size

The TLS handshake to `connect.prusa3d.com` allocates from internal heap (not PSRAM). Watch the **Heap Largest Block** sensor:

- Above ~70 KB — healthy, uploads will succeed
- 65–70 KB — marginal, occasional TLS failures likely
- Below 65 KB — TLS handshakes begin to fail consistently

Heap is stable during normal operation. If it is shrinking over successive uploads, suspect heap fragmentation. The device will automatically reboot after 5 minutes with no successful uploads (see camera stall watchdog below).

### Upload failures — errno 119

Occasionally a write to the Prusa Connect server stalls mid-upload, typically at 60–70 KB into a 72 KB frame:

```
Write timed out at 61440/72782 bytes, errno 119 (EINPROGRESS)
```

This is a known lwIP/TLS interaction on the ESP32 Arduino framework. A failure rate of around 4% is normal and the device recovers on the next cycle automatically. Use the **Upload Consecutive** counter as a health indicator — a sustained run above 20 consecutive successes indicates the device is operating normally.

### Reset reason

The **Reset Reason** entity shows why the device last restarted. Expected values:

| Reason | Meaning |
|--------|---------|
| `Power-on` | Normal power cycle |
| `Software (esp_restart)` | OTA update or Restart Device button |
| `Panic / exception` | Firmware crash — investigate if repeated |
| `Task watchdog` / `Interrupt watchdog` | Severe stall — rare |

Repeated `Panic / exception` resets suggest a stack overflow in the FreeRTOS upload task or critical heap exhaustion immediately before the upload attempt.

### Camera stall watchdog

If the OV2640 driver stalls and stops producing frames, the device reboots automatically after 5 minutes of no upload activity:

```
No upload queued for 5 minutes — camera stalled, rebooting
```

This is a recovery mechanism, not a normal event. If it recurs frequently, check WiFi stability and heap headroom.

---

## Version History

| Version | Description |
|---------|-------------|
| 3.1.5 | Stable version for testing |

---

## Licence

MIT
