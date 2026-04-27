// BasicMount — MountNode bridging an effector to a primary controller (Pi etc.).
//
// Wiring (ESP32-C3 Supermini):
//   Serial0 (USB or GPIO 20/21) → primary controller
//   Serial1 TX (GPIO 4) / RX (GPIO 5) → 4-pin mount connector → effector

#include <Arduino.h>
#include "ModularSerial.h"

using namespace MSC;

// Serial0 → primary controller (Pi), Serial1 → effector connector
MountNode mount(Serial, Serial1);

void setup() {
    Serial.begin(115200);   // to primary controller
    Serial1.begin(115200);  // to effector

    mount.onEffectorConnected([](const Capabilities& caps) {
        // Capabilities are automatically forwarded to the controller as an
        // advertisement packet; this callback is for any local handling you need.
        (void)caps;
    });

    mount.onEffectorDisconnected([]() {
        // disconnect event is automatically sent to the controller
    });

    mount.setHeartbeatTimeout(6000); // 3× the effector's 2 s heartbeat
    mount.begin(); // sends query to effector in case it booted first
}

void loop() {
    mount.loop();
}
