import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor, text_sensor, text
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32_camera"]

prusa_connect_ns = cg.esphome_ns.namespace("prusa_connect")
PrusaConnectComponent = prusa_connect_ns.class_("PrusaConnectComponent", cg.Component)

CONF_TOKEN = "token"
CONF_CAMERA_NAME = "camera_name"
CONF_INTERVAL = "interval"
CONF_DEBUG_MODE = "debug_mode"
CONF_UPLOAD_SUCCESS_SENSOR = "upload_success_sensor"
CONF_UPLOAD_STATUS_SENSOR = "upload_status_sensor"
CONF_UPLOAD_AGE_SENSOR = "upload_age_sensor"
CONF_RESET_REASON_SENSOR = "reset_reason_sensor"
CONF_UPLOAD_TOTAL_SENSOR = "upload_total_sensor"
CONF_UPLOAD_FAIL_SENSOR = "upload_fail_sensor"
CONF_UPLOAD_CONSECUTIVE_SENSOR = "upload_consecutive_sensor"
CONF_HEAP_LARGEST_SENSOR = "heap_largest_sensor"
CONF_HEAP_FREE_SENSOR = "heap_free_sensor"
CONF_FIRMWARE_VERSION_SENSOR = "firmware_version_sensor"
CONF_TOKEN_TEXT = "token_text"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PrusaConnectComponent),
        cv.Required(CONF_TOKEN): cv.string,
        cv.Optional(CONF_CAMERA_NAME, default="ESP32 Camera"): cv.string,
        cv.Optional(CONF_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_DEBUG_MODE, default=False): cv.boolean,
        cv.Optional(CONF_UPLOAD_SUCCESS_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_UPLOAD_STATUS_SENSOR): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_UPLOAD_AGE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_RESET_REASON_SENSOR): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_UPLOAD_TOTAL_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_UPLOAD_FAIL_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_UPLOAD_CONSECUTIVE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_HEAP_LARGEST_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_HEAP_FREE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_FIRMWARE_VERSION_SENSOR): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_TOKEN_TEXT): cv.use_id(text.Text),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_token(config[CONF_TOKEN]))
    cg.add(var.set_camera_name(config[CONF_CAMERA_NAME]))
    cg.add(var.set_interval_ms(config[CONF_INTERVAL]))
    cg.add(var.set_debug_mode(config[CONF_DEBUG_MODE]))

    if CONF_UPLOAD_SUCCESS_SENSOR in config:
        sens = await cg.get_variable(config[CONF_UPLOAD_SUCCESS_SENSOR])
        cg.add(var.set_upload_success_sensor(sens))

    if CONF_UPLOAD_STATUS_SENSOR in config:
        sens = await cg.get_variable(config[CONF_UPLOAD_STATUS_SENSOR])
        cg.add(var.set_upload_status_sensor(sens))

    if CONF_UPLOAD_AGE_SENSOR in config:
        sens = await cg.get_variable(config[CONF_UPLOAD_AGE_SENSOR])
        cg.add(var.set_upload_age_sensor(sens))

    if CONF_RESET_REASON_SENSOR in config:
        sens = await cg.get_variable(config[CONF_RESET_REASON_SENSOR])
        cg.add(var.set_reset_reason_sensor(sens))

    if CONF_UPLOAD_TOTAL_SENSOR in config:
        sens = await cg.get_variable(config[CONF_UPLOAD_TOTAL_SENSOR])
        cg.add(var.set_upload_total_sensor(sens))

    if CONF_UPLOAD_FAIL_SENSOR in config:
        sens = await cg.get_variable(config[CONF_UPLOAD_FAIL_SENSOR])
        cg.add(var.set_upload_fail_sensor(sens))

    if CONF_UPLOAD_CONSECUTIVE_SENSOR in config:
        sens = await cg.get_variable(config[CONF_UPLOAD_CONSECUTIVE_SENSOR])
        cg.add(var.set_upload_consecutive_sensor(sens))

    if CONF_HEAP_LARGEST_SENSOR in config:
        sens = await cg.get_variable(config[CONF_HEAP_LARGEST_SENSOR])
        cg.add(var.set_heap_largest_sensor(sens))

    if CONF_HEAP_FREE_SENSOR in config:
        sens = await cg.get_variable(config[CONF_HEAP_FREE_SENSOR])
        cg.add(var.set_heap_free_sensor(sens))

    if CONF_FIRMWARE_VERSION_SENSOR in config:
        sens = await cg.get_variable(config[CONF_FIRMWARE_VERSION_SENSOR])
        cg.add(var.set_firmware_version_sensor(sens))

    if CONF_TOKEN_TEXT in config:
        t = await cg.get_variable(config[CONF_TOKEN_TEXT])
        cg.add(var.set_token_text(t))
