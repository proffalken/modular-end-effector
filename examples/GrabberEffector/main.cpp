// GrabberEffector — servo-driven grabber with OTel instrumentation.
//
// Wiring:
//   Servo signal → GPIO 8
//   Serial1 TX (GPIO 4) / RX (GPIO 5) → 4-pin mount connector
//   5V via connector, 12V rail unused (servo runs from 5V here)
//
// Build flags required in platformio.ini:
//   -DMSC_OTEL_ENABLED
//   -DWIFI_SSID="..." -DWIFI_PASS="..."
//   -DOTEL_COLLECTOR_HOST="..." -DOTEL_COLLECTOR_PORT=4318
//   -DOTEL_SERVICE_NAME="grabber" etc.

#include <Arduino.h>
#include <ESP32Servo.h>
#include "ModularSerial.h"

#ifdef MSC_OTEL_ENABLED
#include <WiFi.h>
#include <time.h>
#endif

using namespace MSC;

static constexpr int SERVO_PIN       = 8;
static constexpr int SERVO_OPEN_DEG  = 90;
static constexpr int SERVO_CLOSE_DEG = 0;

Servo servo;
EffectorNode node(Serial1, "grabber", "1.0.0");

static void buildSchema(CommandDef& open, CommandDef& close, CommandDef& setPos) {
    strncpy(open.name,        "open",          sizeof(open.name) - 1);
    strncpy(open.description, "Open grabber",  sizeof(open.description) - 1);

    strncpy(close.name,        "close",         sizeof(close.name) - 1);
    strncpy(close.description, "Close grabber", sizeof(close.description) - 1);

    strncpy(setPos.name,        "set_position",      sizeof(setPos.name) - 1);
    strncpy(setPos.description, "Set angle 0-180",   sizeof(setPos.description) - 1);

    ParamDef angle;
    strncpy(angle.name, "degrees", sizeof(angle.name) - 1);
    angle.type   = ParamType::INT;
    angle.minVal = 0;
    angle.maxVal = 180;
    strncpy(angle.unit, "deg", sizeof(angle.unit) - 1);
    setPos.addParam(angle);
}

void setup() {
    Serial1.begin(115200);
    servo.attach(SERVO_PIN);
    servo.write(SERVO_OPEN_DEG);

#ifdef MSC_OTEL_ENABLED
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    configTime(0, 0, "pool.ntp.org");
    while (time(nullptr) < 1609459200UL) delay(500);
    node.enableOtel("http://" OTEL_COLLECTOR_HOST ":4318", OTEL_SERVICE_NAMESPACE);
#endif

    CommandDef open, close, setPos;
    buildSchema(open, close, setPos);

    node.addCommand(open, [](JsonObjectConst, JsonObject) {
        servo.write(SERVO_OPEN_DEG);
        return true;
    });

    node.addCommand(close, [](JsonObjectConst, JsonObject) {
        servo.write(SERVO_CLOSE_DEG);
        return true;
    });

    node.addCommand(setPos, [](JsonObjectConst params, JsonObject result) {
        int deg = params["degrees"] | -1;
        if (deg < 0 || deg > 180) {
            result["error"] = "degrees must be 0-180";
            return false;
        }
        servo.write(deg);
        result["actual"] = deg;
        return true;
    });

    node.setHeartbeatInterval(2000);
    node.begin();
}

void loop() {
    node.loop();
}
