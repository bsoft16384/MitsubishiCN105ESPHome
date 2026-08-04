import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import (
    climate,
    uart,
    select,
    sensor,
    button,
    switch,
    binary_sensor,
    text_sensor,
    number,
)
from esphome.components.uart import UARTParityOptions

from esphome.const import (
    CONF_ID,
    CONF_UPDATE_INTERVAL,
    CONF_MODE,
    CONF_FAN_MODE,
    CONF_SWING_MODE,
    CONF_UART_ID,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_SECOND,
    ICON_TIMER,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_TEMPERATURE,
    UNIT_HERTZ,
    UNIT_WATT,
    UNIT_KILOWATT_HOURS,
    UNIT_CELSIUS,
    UNIT_HOUR,
    STATE_CLASS_MEASUREMENT,
)
from esphome.helpers import cpp_string_escape

# --- AUTO_LOAD, DEPENDENCIES, and CONF_XXX_SENSOR constants ---
AUTO_LOAD = [
    "climate",
    "sensor",
    "select",
    "binary_sensor",
    "button",
    "switch",
    "text_sensor",
    "uart",
    "uptime",
    "number",
]
DEPENDENCIES = ["uart"]  # Keep uart here too

CONF_SUPPORTS = "supports"
CONF_SUPPORTS_HORIZONTAL_VANE_MODE = "horizontal_vane_mode"
CONF_HORIZONTAL_VANES = "horizontal_vanes"
CONF_VANE_TYPE = "vane_type"

VANE_TYPES = {
    "standard": 0,
    "split_horizontal": 1,
    "split_vertical": 2,
}
CONF_HORIZONTAL_SWING_SELECT = "horizontal_vane_select"
CONF_VERTICAL_SWING_SELECT = "vertical_vane_select"
CONF_COMPRESSOR_FREQUENCY_SENSOR = "compressor_frequency_sensor"
CONF_INPUT_POWER_SENSOR = "input_power_sensor"
CONF_KWH_SENSOR = "kwh_sensor"
CONF_RUNTIME_HOURS_SENSOR = "runtime_hours_sensor"
CONF_OUTSIDE_AIR_TEMPERATURE_SENSOR = "outside_air_temperature_sensor"
CONF_TARGET_HUMIDITY_SENSOR = "target_humidity_sensor"
CONF_ISEE_SENSOR = "isee_sensor"
CONF_FUNCTIONS_SENSOR = "functions_sensor"
CONF_FUNCTIONS_BUTTON = "functions_get_button"
CONF_FUNCTIONS_SET_BUTTON = "functions_set_button"
CONF_FUNCTIONS_SET_CODE = "functions_set_code"
CONF_FUNCTIONS_SET_VALUE = "functions_set_value"
CONF_STAGE_SENSOR = "stage_sensor"
CONF_USE_AS_OPERATING_FALLBACK = "use_stage_for_operating_status"
# STAGE_SENSOR fallback description
CONF_SUB_MODE_SENSOR = "sub_mode_sensor"
CONF_AUTO_SUB_MODE_SENSOR = "auto_sub_mode_sensor"
CONF_ERROR_CODE_SENSOR = "error_code_sensor"
CONF_CURRENT_TEMP_SOURCE_SENSOR = "current_temperature_source_sensor"
CONF_HP_UP_TIME_CONNECTION_SENSOR = "hp_uptime_connection_sensor"
CONF_REMOTE_TEMP_SOURCE = "remote_temperature_source"
CONF_REMOTE_TEMP_SOURCE_SENSOR_ID = "sensor_id"
CONF_REMOTE_TEMP_SOURCE_INFO = "info"
CONF_AIRFLOW_CONTROL_SELECT = "airflow_control_select"
CONF_FAN_STOP_SWITCH = "fan_stop_switch"
CONF_LOW_TEMP_PROTECTION_SWITCH = "low_temp_protection_switch"
CONF_DIAGNOSTIC_SENSOR = "diagnostic_sensor"
CONF_HYSTERESIS = "hysteresis"
CONF_LOW_TEMP_TEMP = "low_temp_temp"
CONF_LOW_TEMP_HYSTERESIS = "low_temp_hysteresis"
CONF_HARDWARE_SETTINGS = "hardware_settings"
CONF_CODE = "code"
CONF_OPTIONS = "options"
CONF_TEMPERATURE_MARGIN = "temperature_margin"
CONF_POWER_UNIT_IS_BTU = "power_unit_is_btu"

DEFAULT_CLIMATE_MODES = ["COOL", "HEAT", "DRY", "FAN_ONLY"]
DEFAULT_FAN_MODES = ["AUTO", "MIDDLE", "QUIET", "LOW", "MEDIUM", "HIGH"]
DEFAULT_SWING_MODES = ["OFF", "VERTICAL", "HORIZONTAL", "BOTH"]

CN105Climate = cg.esphome_ns.class_(
    "CN105Climate", climate.Climate, cg.Component, uart.UARTDevice
)
CONF_REMOTE_TEMP_TIMEOUT = "remote_temperature_timeout"
CONF_REMOTE_TEMP_KEEPALIVE_INTERVAL = "remote_temperature_keepalive_interval"
CONF_DEBOUNCE_DELAY = "debounce_delay"
CONF_CONNECTION_BOOTSTRAP_DELAY = "connection_bootstrap_delay"
CONF_INSTALLER_MODE = "installer_mode"

# Definitions of C++ classes
VaneOrientationSelect = cg.esphome_ns.class_(
    "VaneOrientationSelect", select.Select, cg.Component
)
FunctionsButton = cg.esphome_ns.class_("FunctionsButton", button.Button, cg.Component)
FunctionsNumber = cg.esphome_ns.class_("FunctionsNumber", number.Number, cg.Component)
cn105_ns = cg.esphome_ns.namespace("cn105")
HpUpTimeConnectionSensor = cn105_ns.class_(
    "HpUpTimeConnectionSensor", sensor.Sensor, cg.PollingComponent
)
HardwareSettingSelect = cg.esphome_ns.class_(
    "HardwareSettingSelect", select.Select, cg.Component
)

# Schemas for optional entities
SELECT_SCHEMA = select.select_schema(VaneOrientationSelect).extend(
    {cv.GenerateID(CONF_ID): cv.declare_id(VaneOrientationSelect)}
)
COMPRESSOR_FREQUENCY_SENSOR_SCHEMA = sensor.sensor_schema(
    sensor.Sensor,
    unit_of_measurement=UNIT_HERTZ,
    device_class=DEVICE_CLASS_FREQUENCY,
    state_class=STATE_CLASS_MEASUREMENT,
    accuracy_decimals=1,
).extend(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(sensor.Sensor),
        cv.Optional("force_update", default=False): cv.boolean,
    }
)
INPUT_POWER_SENSOR_SCHEMA = sensor.sensor_schema(
    sensor.Sensor,
    unit_of_measurement=UNIT_WATT,
    device_class=DEVICE_CLASS_POWER,
    state_class=STATE_CLASS_MEASUREMENT,
    accuracy_decimals=0,
).extend(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(sensor.Sensor),
        cv.Optional("force_update", default=False): cv.boolean,
    }
)
KWH_SENSOR_SCHEMA = sensor.sensor_schema(
    sensor.Sensor,
    unit_of_measurement=UNIT_KILOWATT_HOURS,
    device_class=DEVICE_CLASS_ENERGY,
    state_class=STATE_CLASS_TOTAL_INCREASING,
    accuracy_decimals=1,
).extend(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(sensor.Sensor),
        cv.Optional("force_update", default=False): cv.boolean,
    }
)
RUNTIME_HOURS_SENSOR_SCHEMA = sensor.sensor_schema(
    sensor.Sensor,
    unit_of_measurement=UNIT_HOUR,
    device_class=DEVICE_CLASS_DURATION,
    state_class=STATE_CLASS_TOTAL_INCREASING,
    accuracy_decimals=2,
).extend(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(sensor.Sensor),
        cv.Optional("force_update", default=False): cv.boolean,
    }
)
OUTSIDE_AIR_TEMPERATURE_SENSOR_SCHEMA = sensor.sensor_schema(
    sensor.Sensor,
    unit_of_measurement=UNIT_CELSIUS,
    device_class=DEVICE_CLASS_TEMPERATURE,
    state_class=STATE_CLASS_MEASUREMENT,
    accuracy_decimals=1,
).extend(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(sensor.Sensor),
        cv.Optional("force_update", default=False): cv.boolean,
    }
)
ISEE_SENSOR_SCHEMA = binary_sensor.binary_sensor_schema(binary_sensor.BinarySensor).extend(
    {cv.GenerateID(CONF_ID): cv.declare_id(binary_sensor.BinarySensor)}
)
TARGET_HUMIDITY_SENSOR_SCHEMA = sensor.sensor_schema(
    sensor.Sensor,
    unit_of_measurement="%",
    icon="mdi:water-percent",
    state_class=STATE_CLASS_MEASUREMENT,
    accuracy_decimals=0,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
).extend(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(sensor.Sensor),
        cv.Optional("force_update", default=False): cv.boolean,
    }
)
FUNCTIONS_SENSOR_SCHEMA = text_sensor.text_sensor_schema(text_sensor.TextSensor).extend(
    {cv.GenerateID(CONF_ID): cv.declare_id(text_sensor.TextSensor)}
)
FUNCTIONS_BUTTON_SCHEMA = button.button_schema(FunctionsButton).extend(
    {cv.GenerateID(CONF_ID): cv.declare_id(FunctionsButton)}
)
FUNCTIONS_NUMBER_SCHEMA = number.number_schema(FunctionsNumber).extend(
    {cv.GenerateID(CONF_ID): cv.declare_id(FunctionsNumber)}
)
SUB_MODE_SENSOR_SCHEMA = text_sensor.text_sensor_schema(text_sensor.TextSensor).extend(
    {cv.GenerateID(CONF_ID): cv.declare_id(text_sensor.TextSensor)}
)
AUTO_SUB_MODE_SENSOR_SCHEMA = text_sensor.text_sensor_schema(text_sensor.TextSensor).extend(
    {cv.GenerateID(CONF_ID): cv.declare_id(text_sensor.TextSensor)}
)

ERROR_CODE_SENSOR_SCHEMA = text_sensor.text_sensor_schema(text_sensor.TextSensor).extend(
    {cv.GenerateID(CONF_ID): cv.declare_id(text_sensor.TextSensor)}
)

# Diagnostic text sensor reporting which temperature source the unit is using
# ("Remote" when it has adopted the value we feed it, "Internal" otherwise).
CURRENT_TEMP_SOURCE_SENSOR_SCHEMA = text_sensor.text_sensor_schema(
    text_sensor.TextSensor,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    icon="mdi:thermometer-check",
).extend({cv.GenerateID(CONF_ID): cv.declare_id(text_sensor.TextSensor)})

REMOTE_TEMP_SOURCE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_REMOTE_TEMP_SOURCE_SENSOR_ID): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_REMOTE_TEMP_SOURCE_INFO): text_sensor.text_sensor_schema(text_sensor.TextSensor).extend(
            {cv.GenerateID(CONF_ID): cv.declare_id(text_sensor.TextSensor)}
        ),
    }
)


# Schema for STAGE_SENSOR (which is a text_sensor) WITH the new sub-option
STAGE_SENSOR_CONFIG_SCHEMA = text_sensor.text_sensor_schema(text_sensor.TextSensor).extend(
    {
        # The ID for the C++ object is managed by text_sensor.TEXT_SENSOR_SCHEMA (via CONF_ID)
        cv.Optional(CONF_USE_AS_OPERATING_FALLBACK, default=False): cv.boolean,
    }
)

# Schema for HP_UP_TIME_CONNECTION_SENSOR
HP_UP_TIME_CONNECTION_SENSOR_SCHEMA = sensor.sensor_schema(
    HpUpTimeConnectionSensor,
    unit_of_measurement=UNIT_SECOND,
    icon=ICON_TIMER,
    accuracy_decimals=0,
    state_class=STATE_CLASS_TOTAL_INCREASING,
    device_class=DEVICE_CLASS_DURATION,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
).extend(cv.polling_component_schema("60s"))

HARDWARE_SETTING_ITEM_SCHEMA = select.select_schema(HardwareSettingSelect).extend(
    {
        cv.Required(CONF_CODE): cv.int_range(min=101, max=128),
        cv.Required(CONF_OPTIONS): cv.Schema({cv.int_range(min=0, max=100): cv.string}),
    }
)

CONF_HARDWARE_SETTINGS_LIST = "list"

HARDWARE_SETTING_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_UPDATE_INTERVAL, default="24h"): cv.update_interval,
        cv.Required(CONF_HARDWARE_SETTINGS_LIST): cv.ensure_list(
            HARDWARE_SETTING_ITEM_SCHEMA
        ),
    }
)

def validate_modes(value):
    modes = cv.ensure_list(climate.validate_climate_mode)(value)
    for unsupported in ("AUTO", "HEAT_COOL"):
        if unsupported in modes:
            raise cv.Invalid(
                f"{unsupported} mode is not supported by this component. "
                "The unit's own AUTO mode is reported to Home Assistant as FAN_ONLY."
            )
    return modes

CONFIG_SCHEMA = (
    climate.climate_schema(CN105Climate)
    .extend(
        {
            cv.GenerateID(): cv.declare_id(CN105Climate),
            cv.GenerateID(CONF_UART_ID): cv.use_id(uart.UARTComponent),
            cv.Optional(CONF_UPDATE_INTERVAL, default="2s"): cv.update_interval,
            cv.Optional(CONF_HORIZONTAL_SWING_SELECT): SELECT_SCHEMA,
            cv.Optional(CONF_VERTICAL_SWING_SELECT): SELECT_SCHEMA,
            cv.Optional(
                CONF_COMPRESSOR_FREQUENCY_SENSOR
            ): COMPRESSOR_FREQUENCY_SENSOR_SCHEMA,
            cv.Optional(CONF_INPUT_POWER_SENSOR): INPUT_POWER_SENSOR_SCHEMA,
            cv.Optional(CONF_KWH_SENSOR): KWH_SENSOR_SCHEMA,
            cv.Optional(CONF_RUNTIME_HOURS_SENSOR): RUNTIME_HOURS_SENSOR_SCHEMA,
            cv.Optional(
                CONF_OUTSIDE_AIR_TEMPERATURE_SENSOR
            ): OUTSIDE_AIR_TEMPERATURE_SENSOR_SCHEMA,
            cv.Optional(CONF_ISEE_SENSOR): ISEE_SENSOR_SCHEMA,
            cv.Optional(CONF_TARGET_HUMIDITY_SENSOR): TARGET_HUMIDITY_SENSOR_SCHEMA,
            cv.Optional(CONF_FUNCTIONS_SENSOR): FUNCTIONS_SENSOR_SCHEMA,
            cv.Optional(CONF_FUNCTIONS_BUTTON): FUNCTIONS_BUTTON_SCHEMA,
            cv.Optional(CONF_FUNCTIONS_SET_BUTTON): FUNCTIONS_BUTTON_SCHEMA,
            cv.Optional(CONF_FUNCTIONS_SET_CODE): FUNCTIONS_NUMBER_SCHEMA,
            cv.Optional(CONF_FUNCTIONS_SET_VALUE): FUNCTIONS_NUMBER_SCHEMA,
            cv.Optional(
                CONF_STAGE_SENSOR
            ): STAGE_SENSOR_CONFIG_SCHEMA,  # Modified for the new schema
            cv.Optional(CONF_SUB_MODE_SENSOR): SUB_MODE_SENSOR_SCHEMA,
            cv.Optional(CONF_AUTO_SUB_MODE_SENSOR): AUTO_SUB_MODE_SENSOR_SCHEMA,
            cv.Optional(CONF_ERROR_CODE_SENSOR): ERROR_CODE_SENSOR_SCHEMA,
            cv.Optional(CONF_REMOTE_TEMP_SOURCE): REMOTE_TEMP_SOURCE_SCHEMA,
            cv.Optional(CONF_REMOTE_TEMP_TIMEOUT, default="never"): cv.update_interval,
            # Keep-alive interval for remote temperature (like Kumo does every ~20s).
            # Cannot be disabled: values under 20s are clamped up to 20s at runtime,
            # since keep-alive is the safety net against the unit reverting to its
            # internal sensor when the remote temperature is stable.
            cv.Optional(CONF_REMOTE_TEMP_KEEPALIVE_INTERVAL, default="20s"): cv.update_interval,
            cv.Optional(CONF_DEBOUNCE_DELAY, default="100ms"): cv.update_interval,
            cv.Optional(CONF_CONNECTION_BOOTSTRAP_DELAY, default="10s"): cv.update_interval,
            cv.Optional(CONF_INSTALLER_MODE, default=False): cv.boolean,
            cv.Optional(
                CONF_HP_UP_TIME_CONNECTION_SENSOR
            ): HP_UP_TIME_CONNECTION_SENSOR_SCHEMA,
            cv.Optional(CONF_AIRFLOW_CONTROL_SELECT): SELECT_SCHEMA,
            cv.Optional(CONF_HARDWARE_SETTINGS): HARDWARE_SETTING_SCHEMA,
            cv.Optional(
                CONF_CURRENT_TEMP_SOURCE_SENSOR
            ): CURRENT_TEMP_SOURCE_SENSOR_SCHEMA,
            # Tolerance (°C) for deciding the unit has adopted our remote temperature:
            # if its echoed room temp is within this margin of the value we sent, the
            # current-temperature source is reported as "Remote".
            cv.Optional(CONF_TEMPERATURE_MARGIN, default=0.4): cv.positive_float,
            cv.Optional(CONF_POWER_UNIT_IS_BTU, default=False): cv.boolean,
            cv.Optional(CONF_FAN_STOP_SWITCH): cv.use_id(switch.Switch),
            cv.Optional(CONF_LOW_TEMP_PROTECTION_SWITCH): cv.use_id(switch.Switch),
            cv.Optional(CONF_DIAGNOSTIC_SENSOR): cv.use_id(text_sensor.TextSensor),
            cv.Optional(CONF_HYSTERESIS, default=0.5): cv.positive_float,
            cv.Optional(CONF_LOW_TEMP_TEMP, default=8.0): cv.positive_float,
            cv.Optional(CONF_LOW_TEMP_HYSTERESIS, default=4.0): cv.positive_float,
            cv.Optional(CONF_SUPPORTS, default={}): cv.Schema(
                {
                    cv.Optional(
                        CONF_MODE, default=DEFAULT_CLIMATE_MODES
                    ): validate_modes,
                    cv.Optional(
                        CONF_FAN_MODE, default=DEFAULT_FAN_MODES
                    ): cv.ensure_list(climate.validate_climate_fan_mode),
                    cv.Optional(
                        CONF_SWING_MODE, default=DEFAULT_SWING_MODES
                    ): cv.ensure_list(climate.validate_climate_swing_mode),
                    cv.Optional(CONF_SUPPORTS_HORIZONTAL_VANE_MODE): cv.ensure_list(
                        cv.string
                    ),
                    # Deprecated: use vane_type instead. Left without a default so an
                    # explicit vane_type can be told apart from the implicit one.
                    cv.Optional(CONF_HORIZONTAL_VANES, default=1): cv.int_range(
                        min=1, max=2
                    ),
                    cv.Optional(CONF_VANE_TYPE): cv.enum(VANE_TYPES, lower=True),
                }
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

async def to_code(config):
    uart_id_object = config[CONF_UART_ID]
    uart_var = await cg.get_variable(uart_id_object)
    var = cg.new_Pvariable(config[CONF_ID], uart_var)

    cg.add(var.set_installer_mode(config[CONF_INSTALLER_MODE]))
    cg.add(var.set_power_unit_is_btu(config[CONF_POWER_UNIT_IS_BTU]))

    cg.add(uart_var.set_data_bits(8))
    cg.add(uart_var.set_parity(UARTParityOptions.UART_CONFIG_PARITY_EVEN))
    cg.add(uart_var.set_stop_bits(1))

    # Empty list means use all options from WIDEVANE_MAP (C++ is source of truth)
    horizontal_vane_options = []

    supports = config[CONF_SUPPORTS]
    traits = var.config_traits()

    # Configure the supported modes
    supported_modes = supports.get(CONF_MODE, DEFAULT_CLIMATE_MODES)
    for mode_str in supported_modes:
        if mode_str == "OFF":
            continue
        if mode_str in climate.CLIMATE_MODES:
            cg.add(traits.add_supported_mode(climate.CLIMATE_MODES[mode_str]))

    # Configure the horizontal vane options
    horizontal_vane_options = supports.get(CONF_SUPPORTS_HORIZONTAL_VANE_MODE, [])

    # Resolve the vane type once, in Python. vane_type is the current option and wins
    # when set explicitly; horizontal_vanes is the deprecated spelling of the same
    # thing. Emitting both setters unconditionally (as this used to) meant the
    # vane_type default always ran last and silently reset horizontal_vanes: 2.
    if CONF_VANE_TYPE in supports:
        vane_type_val = VANE_TYPES[supports[CONF_VANE_TYPE]]
    elif supports.get(CONF_HORIZONTAL_VANES, 1) > 1:
        vane_type_val = VANE_TYPES["split_horizontal"]
    else:
        vane_type_val = VANE_TYPES["standard"]
    vane_type_enum = cg.RawExpression(
        f"static_cast<esphome::CN105Climate::VaneType>({vane_type_val})"
    )
    cg.add(var.set_vane_type(vane_type_enum))

    for fan_mode_str in supports.get(CONF_FAN_MODE, DEFAULT_FAN_MODES):
        if fan_mode_str in climate.CLIMATE_FAN_MODES:
            cg.add(
                traits.add_supported_fan_mode(
                    climate.CLIMATE_FAN_MODES[fan_mode_str]
                )
            )
    for swing_mode_str in supports.get(CONF_SWING_MODE, DEFAULT_SWING_MODES):
        if swing_mode_str in climate.CLIMATE_SWING_MODES:
            cg.add(
                traits.add_supported_swing_mode(
                    climate.CLIMATE_SWING_MODES[swing_mode_str]
                )
            )

    cg.add(var.set_remote_temp_timeout(config[CONF_REMOTE_TEMP_TIMEOUT]))
    cg.add(var.set_debounce_delay(config[CONF_DEBOUNCE_DELAY]))
    cg.add(
        var.set_remote_temp_keepalive_interval(
            int(config[CONF_REMOTE_TEMP_KEEPALIVE_INTERVAL].total_milliseconds)
        )
    )
    cg.add(
        var.set_connection_bootstrap_delay(
            int(config[CONF_CONNECTION_BOOTSTRAP_DELAY].total_milliseconds)
        )
    )

    # --- Configure optional entities (original style) ---
    if CONF_HORIZONTAL_SWING_SELECT in config:
        conf_item = config[CONF_HORIZONTAL_SWING_SELECT]
        # new_select handles registration. options=[] is important.
        swing_select_var = await select.new_select(conf_item, options=[])
        if horizontal_vane_options:
            options_vector = cg.RawExpression(
                "std::vector<std::string>{"
                + ", ".join(cpp_string_escape(opt) for opt in horizontal_vane_options)
                + "}"
            )
        else:
            options_vector = cg.RawExpression("std::vector<std::string>{}")
        cg.add(var.set_horizontal_vane_select(swing_select_var, options_vector))

    if CONF_VERTICAL_SWING_SELECT in config:
        conf_item = config[CONF_VERTICAL_SWING_SELECT]
        swing_select_var = await select.new_select(conf_item, options=[])
        cg.add(var.set_vertical_vane_select(swing_select_var))

    if CONF_AIRFLOW_CONTROL_SELECT in config:
        conf_item = config[CONF_AIRFLOW_CONTROL_SELECT]
        control_select_var = await select.new_select(conf_item, options=[])
        cg.add(var.set_airflow_control_select(control_select_var))

    # For sensors, text_sensors, etc., use standard .new_... method
    # These functions handle component registration.
    if CONF_COMPRESSOR_FREQUENCY_SENSOR in config:
        conf_item = config[CONF_COMPRESSOR_FREQUENCY_SENSOR]
        sensor_var = await sensor.new_sensor(conf_item)
        cg.add(var.set_compressor_frequency_sensor(sensor_var))

    if CONF_INPUT_POWER_SENSOR in config:
        conf_item = config[CONF_INPUT_POWER_SENSOR]
        sensor_var = await sensor.new_sensor(conf_item)
        cg.add(var.set_input_power_sensor(sensor_var))

    if CONF_KWH_SENSOR in config:
        conf_item = config[CONF_KWH_SENSOR]
        sensor_var = await sensor.new_sensor(conf_item)
        cg.add(var.set_kwh_sensor(sensor_var))

    if CONF_RUNTIME_HOURS_SENSOR in config:
        conf_item = config[CONF_RUNTIME_HOURS_SENSOR]
        sensor_var = await sensor.new_sensor(conf_item)
        cg.add(var.set_runtime_hours_sensor(sensor_var))

    if CONF_OUTSIDE_AIR_TEMPERATURE_SENSOR in config:
        conf_item = config[CONF_OUTSIDE_AIR_TEMPERATURE_SENSOR]
        sensor_var = await sensor.new_sensor(conf_item)
        cg.add(var.set_outside_air_temperature_sensor(sensor_var))

    if CONF_ISEE_SENSOR in config:
        bsensor_var = await binary_sensor.new_binary_sensor(config[CONF_ISEE_SENSOR])
        cg.add(var.set_isee_sensor(bsensor_var))

    if CONF_TARGET_HUMIDITY_SENSOR in config:
        conf_item = config[CONF_TARGET_HUMIDITY_SENSOR]
        sensor_var = await sensor.new_sensor(conf_item)
        cg.add(var.set_target_humidity_sensor(sensor_var))

    if CONF_FUNCTIONS_SENSOR in config:
        tsensor_var = await text_sensor.new_text_sensor(config[CONF_FUNCTIONS_SENSOR])
        cg.add(var.set_functions_sensor(tsensor_var))

    if CONF_FUNCTIONS_BUTTON in config:
        button_var = await button.new_button(config[CONF_FUNCTIONS_BUTTON])
        cg.add(var.set_functions_get_button(button_var))

    if CONF_FUNCTIONS_SET_BUTTON in config:
        button_var = await button.new_button(config[CONF_FUNCTIONS_SET_BUTTON])
        cg.add(var.set_functions_set_button(button_var))

    if CONF_FUNCTIONS_SET_CODE in config:
        conf_item = config[CONF_FUNCTIONS_SET_CODE]
        number_var = await number.new_number(
            conf_item, min_value=100.0, max_value=128.0, step=1.0
        )
        cg.add(var.set_functions_set_code(number_var))

    if CONF_FUNCTIONS_SET_VALUE in config:
        conf_item = config[CONF_FUNCTIONS_SET_VALUE]
        number_var = await number.new_number(
            conf_item, min_value=1.0, max_value=3.0, step=1.0
        )
        cg.add(var.set_functions_set_value(number_var))

    # --- STAGE_SENSOR TREATMENT WITH NEW OPTION ---
    if CONF_STAGE_SENSOR in config:
        conf_stage_dict = config[CONF_STAGE_SENSOR]
        # new_text_sensor handles base creation and registration of the text_sensor
        stage_ts_var = await text_sensor.new_text_sensor(conf_stage_dict)
        cg.add(var.set_stage_sensor(stage_ts_var))

        # Pass the fallback option to C++
        if conf_stage_dict.get(CONF_USE_AS_OPERATING_FALLBACK, False):
            cg.add(var.set_use_stage_for_operating_status(True))
    # --- END OF STAGE_SENSOR TREATMENT ---

    cg.add(var.set_remote_temperature_margin(config[CONF_TEMPERATURE_MARGIN]))

    if CONF_CURRENT_TEMP_SOURCE_SENSOR in config:
        tsensor_var = await text_sensor.new_text_sensor(
            config[CONF_CURRENT_TEMP_SOURCE_SENSOR]
        )
        cg.add(var.set_current_temperature_source_sensor(tsensor_var))

    if CONF_SUB_MODE_SENSOR in config:
        tsensor_var = await text_sensor.new_text_sensor(config[CONF_SUB_MODE_SENSOR])
        cg.add(var.set_sub_mode_sensor(tsensor_var))

    if CONF_AUTO_SUB_MODE_SENSOR in config:
        tsensor_var = await text_sensor.new_text_sensor(
            config[CONF_AUTO_SUB_MODE_SENSOR]
        )
        cg.add(var.set_auto_sub_mode_sensor(tsensor_var))

    if CONF_ERROR_CODE_SENSOR in config:
        tsensor_var = await text_sensor.new_text_sensor(
            config[CONF_ERROR_CODE_SENSOR]
        )
        cg.add(var.set_error_code_sensor(tsensor_var))

    if CONF_REMOTE_TEMP_SOURCE in config:
        rts_config = config[CONF_REMOTE_TEMP_SOURCE]
        source_sensor = await cg.get_variable(rts_config[CONF_REMOTE_TEMP_SOURCE_SENSOR_ID])
        cg.add(var.set_remote_temp_source(source_sensor))
        if CONF_REMOTE_TEMP_SOURCE_INFO in rts_config:
            info_sensor = await text_sensor.new_text_sensor(rts_config[CONF_REMOTE_TEMP_SOURCE_INFO])
            cg.add(var.set_remote_temp_source_info_sensor(info_sensor))

    if CONF_HP_UP_TIME_CONNECTION_SENSOR in config:
        conf = config[CONF_HP_UP_TIME_CONNECTION_SENSOR]
        hp_connection_sensor_ = await sensor.new_sensor(conf)
        cg.add(var.set_hp_uptime_connection_sensor(hp_connection_sensor_))

    if CONF_HARDWARE_SETTINGS in config:
        hw_config = config[CONF_HARDWARE_SETTINGS]

        # Extract and set the update interval
        interval_ms = int(hw_config[CONF_UPDATE_INTERVAL].total_milliseconds)
        cg.add(var.set_hardware_settings_interval(interval_ms))

        # Iterate over the list of hardware settings
        for setting_conf in hw_config[CONF_HARDWARE_SETTINGS_LIST]:
            code = setting_conf[CONF_CODE]
            options_map = setting_conf[CONF_OPTIONS]

            # Build inline initializer list for std::map: {{key1, "val1"}, {key2, "val2"}, ...}
            map_entries = ", ".join(
                f"{{{val}, {cpp_string_escape(label)}}}"
                for val, label in options_map.items()
            )
            map_expr = cg.RawExpression(f"std::map<int, std::string>{{{map_entries}}}")

            setting_var = cg.new_Pvariable(setting_conf[CONF_ID], code, map_expr)

            # Extract options list sorted by key (1, 2, 3...) to ensure consistent order
            options_list = [options_map[k] for k in sorted(options_map.keys())]
            await select.register_select(
                setting_var, setting_conf, options=options_list
            )

            cg.add(var.add_hardware_setting(setting_var))

    if CONF_FAN_STOP_SWITCH in config:
        fan_stop_switch_var = await cg.get_variable(config[CONF_FAN_STOP_SWITCH])
        cg.add(var.set_fan_stop_switch(fan_stop_switch_var))
    if CONF_LOW_TEMP_PROTECTION_SWITCH in config:
        low_temp_protection_switch_var = await cg.get_variable(config[CONF_LOW_TEMP_PROTECTION_SWITCH])
        cg.add(var.set_low_temp_protection_switch(low_temp_protection_switch_var))
    if CONF_DIAGNOSTIC_SENSOR in config:
        diagnostic_sensor_var = await cg.get_variable(config[CONF_DIAGNOSTIC_SENSOR])
        cg.add(var.set_diagnostic_sensor(diagnostic_sensor_var))
    if CONF_HYSTERESIS in config:
        cg.add(var.set_hysteresis(config[CONF_HYSTERESIS]))
    if CONF_LOW_TEMP_TEMP in config:
        cg.add(var.set_low_temp_temp(config[CONF_LOW_TEMP_TEMP]))
    if CONF_LOW_TEMP_HYSTERESIS in config:
        cg.add(var.set_low_temp_hysteresis(config[CONF_LOW_TEMP_HYSTERESIS]))

    await cg.register_component(var, config)
    await climate.register_climate(var, config)
