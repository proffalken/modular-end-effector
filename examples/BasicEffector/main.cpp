// BasicEffector — minimal EffectorNode with no actuator logic.
// Demonstrates: registration, advertisement, command handling, heartbeat.
//
// Wiring (ESP32-C3 Supermini):
//   Serial1 TX (GPIO 4) → mount connector RX
//   Serial1 RX (GPIO 5) → mount connector TX
//   GND and 5V via 4-pin magnetic connector

#include <Arduino.h>
#include "ModularSerial.h"

using namespace MSC;

// Serial1 is the UART connected to the 4-pin mount connector
EffectorNode node(Serial1, "basic-effector", "1.0.0");

void setup() {
    Serial1.begin(115200);

    CommandDef ping;
    strncpy(ping.name,        "ping",          sizeof(ping.name) - 1);
    strncpy(ping.description, "Echo test",     sizeof(ping.description) - 1);

    node.addCommand(ping, [](JsonObjectConst, JsonObject result) {
        result["pong"] = true;
        return true;
    });

    node.setHeartbeatInterval(2000);
    node.begin(); // sends advertisement
}

void loop() {
    node.loop();
}
