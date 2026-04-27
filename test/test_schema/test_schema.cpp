#include <unity.h>
#include "MSC/Schema.h"
#include <string.h>

using namespace MSC;

void setUp() {}
void tearDown() {}

// ── Helpers ───────────────────────────────────────────────────────────────────

static Capabilities makeGrabber() {
    Capabilities caps;
    strncpy(caps.deviceName, "grabber", sizeof(caps.deviceName) - 1);
    strncpy(caps.version,    "1.0.0",   sizeof(caps.version) - 1);

    CommandDef open;
    strncpy(open.name, "open", sizeof(open.name) - 1);
    strncpy(open.description, "Open the grabber", sizeof(open.description) - 1);
    caps.addCommand(open);

    CommandDef setPos;
    strncpy(setPos.name, "set_position", sizeof(setPos.name) - 1);
    strncpy(setPos.description, "Set position", sizeof(setPos.description) - 1);

    ParamDef p;
    strncpy(p.name, "position", sizeof(p.name) - 1);
    p.type   = ParamType::INT;
    p.minVal = 0.0f;
    p.maxVal = 100.0f;
    strncpy(p.unit, "percent", sizeof(p.unit) - 1);
    setPos.addParam(p);
    caps.addCommand(setPos);

    return caps;
}

// ── Serialisation ─────────────────────────────────────────────────────────────

void test_toJson_has_advertisement_type() {
    Capabilities caps = makeGrabber();
    char buf[512];
    size_t len = caps.toJson(buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0u, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"advertisement\""));
}

void test_toJson_has_device_and_version() {
    Capabilities caps = makeGrabber();
    char buf[512];
    caps.toJson(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"grabber\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"1.0.0\""));
}

void test_toJson_buffer_too_small_returns_zero() {
    Capabilities caps = makeGrabber();
    char buf[10];
    TEST_ASSERT_EQUAL_UINT(0u, caps.toJson(buf, sizeof(buf)));
}

// ── Deserialisation ───────────────────────────────────────────────────────────

void test_round_trip_device_name_and_version() {
    Capabilities caps = makeGrabber();
    char buf[512];
    caps.toJson(buf, sizeof(buf));

    Capabilities parsed = Capabilities::fromJson(buf);
    TEST_ASSERT_TRUE(parsed.valid());
    TEST_ASSERT_EQUAL_STRING("grabber", parsed.deviceName);
    TEST_ASSERT_EQUAL_STRING("1.0.0",   parsed.version);
}

void test_round_trip_command_count() {
    Capabilities caps = makeGrabber();
    char buf[512];
    caps.toJson(buf, sizeof(buf));

    Capabilities parsed = Capabilities::fromJson(buf);
    TEST_ASSERT_EQUAL_UINT8(2, parsed.commandCount);
}

void test_round_trip_param_type_and_range() {
    Capabilities caps = makeGrabber();
    char buf[512];
    caps.toJson(buf, sizeof(buf));

    Capabilities parsed = Capabilities::fromJson(buf);
    // Find set_position command
    CommandDef* cmd = nullptr;
    for (uint8_t i = 0; i < parsed.commandCount; i++) {
        if (strcmp(parsed.commands[i].name, "set_position") == 0) {
            cmd = &parsed.commands[i];
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_UINT8(1, cmd->paramCount);
    TEST_ASSERT_EQUAL_STRING("position", cmd->params[0].name);
    TEST_ASSERT_EQUAL(ParamType::INT, cmd->params[0].type);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,   cmd->params[0].minVal);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, cmd->params[0].maxVal);
    TEST_ASSERT_EQUAL_STRING("percent", cmd->params[0].unit);
}

void test_from_json_rejects_non_advertisement() {
    const char* json = "{\"type\":\"command\",\"device\":\"grabber\",\"version\":\"1.0.0\"}";
    Capabilities caps = Capabilities::fromJson(json);
    TEST_ASSERT_FALSE(caps.valid());
}

void test_from_json_rejects_malformed_json() {
    Capabilities caps = Capabilities::fromJson("{not valid json");
    TEST_ASSERT_FALSE(caps.valid());
}

// ── Enum params ───────────────────────────────────────────────────────────────

void test_enum_param_round_trip() {
    Capabilities caps;
    strncpy(caps.deviceName, "light", sizeof(caps.deviceName) - 1);
    strncpy(caps.version,    "1.0.0", sizeof(caps.version) - 1);

    CommandDef cmd;
    strncpy(cmd.name, "set_mode", sizeof(cmd.name) - 1);

    ParamDef p;
    strncpy(p.name, "mode", sizeof(p.name) - 1);
    p.type = ParamType::ENUM;
    strncpy(p.enumValues, "on,off,blink", sizeof(p.enumValues) - 1);
    cmd.addParam(p);
    caps.addCommand(cmd);

    char buf[512];
    caps.toJson(buf, sizeof(buf));
    Capabilities parsed = Capabilities::fromJson(buf);

    TEST_ASSERT_TRUE(parsed.valid());
    TEST_ASSERT_EQUAL(ParamType::ENUM, parsed.commands[0].params[0].type);
    // Enum values survive the round trip
    TEST_ASSERT_NOT_NULL(strstr(parsed.commands[0].params[0].enumValues, "on"));
    TEST_ASSERT_NOT_NULL(strstr(parsed.commands[0].params[0].enumValues, "off"));
    TEST_ASSERT_NOT_NULL(strstr(parsed.commands[0].params[0].enumValues, "blink"));
}

// ── Boolean + float params ────────────────────────────────────────────────────

void test_bool_and_float_param_types_serialise() {
    Capabilities caps;
    strncpy(caps.deviceName, "laser", sizeof(caps.deviceName) - 1);
    strncpy(caps.version,    "1.0.0", sizeof(caps.version) - 1);

    CommandDef cmd;
    strncpy(cmd.name, "fire", sizeof(cmd.name) - 1);

    ParamDef pwr;
    strncpy(pwr.name, "power", sizeof(pwr.name) - 1);
    pwr.type   = ParamType::FLOAT;
    pwr.minVal = 0.0f;
    pwr.maxVal = 1.0f;
    strncpy(pwr.unit, "ratio", sizeof(pwr.unit) - 1);
    cmd.addParam(pwr);

    ParamDef safe;
    strncpy(safe.name, "safety_off", sizeof(safe.name) - 1);
    safe.type = ParamType::BOOL;
    cmd.addParam(safe);

    caps.addCommand(cmd);

    char buf[512];
    caps.toJson(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"float\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bool\""));

    Capabilities parsed = Capabilities::fromJson(buf);
    TEST_ASSERT_EQUAL(ParamType::FLOAT, parsed.commands[0].params[0].type);
    TEST_ASSERT_EQUAL(ParamType::BOOL,  parsed.commands[0].params[1].type);
}

// ── Capacity limits ───────────────────────────────────────────────────────────

void test_addCommand_ignores_overflow() {
    Capabilities caps;
    strncpy(caps.deviceName, "busy", sizeof(caps.deviceName) - 1);
    for (int i = 0; i < MSC_MAX_COMMANDS + 5; i++) {
        CommandDef cmd;
        snprintf(cmd.name, sizeof(cmd.name), "cmd%d", i);
        caps.addCommand(cmd);
    }
    TEST_ASSERT_EQUAL_UINT8(MSC_MAX_COMMANDS, caps.commandCount);
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_toJson_has_advertisement_type);
    RUN_TEST(test_toJson_has_device_and_version);
    RUN_TEST(test_toJson_buffer_too_small_returns_zero);
    RUN_TEST(test_round_trip_device_name_and_version);
    RUN_TEST(test_round_trip_command_count);
    RUN_TEST(test_round_trip_param_type_and_range);
    RUN_TEST(test_from_json_rejects_non_advertisement);
    RUN_TEST(test_from_json_rejects_malformed_json);
    RUN_TEST(test_enum_param_round_trip);
    RUN_TEST(test_bool_and_float_param_types_serialise);
    RUN_TEST(test_addCommand_ignores_overflow);
    return UNITY_END();
}
