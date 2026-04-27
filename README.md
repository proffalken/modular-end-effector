# ModularSerialConnector

A PlatformIO library implementing a hot-plug serial protocol for modular robot-arm end effectors.

When an effector is mounted and powered via the 4-pin connector, it advertises its name and full command schema over UART. The mount controller relays this to the primary controller (Pi or similar), which then knows exactly what is attached and how to talk to it — with no hardcoded device knowledge anywhere in the system.

Optional [OpenTelemetry](https://opentelemetry.io/) support provides distributed traces across the Pi → Mount → Effector chain over WiFi, independent of the serial control path.

[![CI](https://github.com/proffalken/modular-end-effector/actions/workflows/ci.yml/badge.svg)](https://github.com/proffalken/modular-end-effector/actions/workflows/ci.yml)

---

## Architecture

```
Primary Controller (Pi etc.)
        │  UART
        │
 ┌──────▼────────┐
 │  Mount MCU    │  ← ESP32-C3, Serial0 to Pi, Serial1 to effector connector
 └──────┬────────┘
        │  4-pin magnetic connector
        │  GND / TX / RX / 5V
        │
 ┌──────▼────────┐
 │ Effector MCU  │  ← ESP32-C3, powered by 5V on connector
 └──────┬────────┘
        │
   [Actuator / Sensor]
        │
   ══════════════  2-pin connector: 12V for steppers etc.
```

### Connection sequence

1. Effector is mounted → 5V powers up its MCU
2. Effector broadcasts an **advertisement** packet containing its name, version, and full command schema
3. Mount receives it, sends a **connect event** and the advertisement to the primary controller
4. Primary controller now knows what is attached and what commands it accepts
5. Primary sends **commands** → Mount **ACKs** immediately, then forwards to effector
6. Effector executes and sends a **response** → Mount forwards to primary
7. Effector sends periodic **heartbeats**; if they stop, the Mount declares disconnect

### Wire format

Each message is two newline-terminated lines:

```
{"type":"command","id":"a1b2","command":"set_position","params":{"position":50}}
3F8A21C4
```

The second line is the CRC32 of the JSON string (8 uppercase hex digits). Any frame that fails the CRC check is silently discarded.

---

## Installation

Add to your `platformio.ini`:

```ini
lib_deps =
    https://github.com/proffalken/modular-end-effector.git#main
```

For OTel support also add:

```ini
lib_deps =
    https://github.com/proffalken/modular-end-effector.git#main
    https://github.com/proffalken/otel-embedded-cpp.git#main
```

---

## Quick start — Effector

```cpp
#include <Arduino.h>
#include "ModularSerial.h"

using namespace MSC;

// Serial1 is connected to the 4-pin mount connector
EffectorNode node(Serial1, "grabber", "1.0.0");

void setup() {
    Serial1.begin(115200);

    // Describe the command
    CommandDef setPos;
    strncpy(setPos.name,        "set_position", sizeof(setPos.name) - 1);
    strncpy(setPos.description, "Set angle",    sizeof(setPos.description) - 1);

    ParamDef angle;
    strncpy(angle.name, "degrees", sizeof(angle.name) - 1);
    angle.type   = ParamType::INT;
    angle.minVal = 0;
    angle.maxVal = 180;
    strncpy(angle.unit, "deg", sizeof(angle.unit) - 1);
    setPos.addParam(angle);

    // Register the command with a handler
    node.addCommand(setPos, [](JsonObjectConst params, JsonObject result) {
        int deg = params["degrees"] | 0;
        servo.write(deg);
        result["actual"] = deg;
        return true; // false = error response
    });

    node.setHeartbeatInterval(2000); // ms, default 2000
    node.begin();                    // sends initial advertisement
}

void loop() {
    node.loop(); // reads serial, dispatches commands, sends heartbeats
}
```

### Supported parameter types

| `ParamType`  | JSON type  | Extra fields          |
|--------------|------------|-----------------------|
| `INT`        | number     | `min`, `max`, `unit`  |
| `FLOAT`      | number     | `min`, `max`, `unit`  |
| `BOOL`       | boolean    | —                     |
| `STRING`     | string     | —                     |
| `ENUM`       | string     | `values` array        |

For `ENUM`, set `enumValues` as a comma-separated string:

```cpp
ParamDef mode;
strncpy(mode.name, "mode", sizeof(mode.name) - 1);
mode.type = ParamType::ENUM;
strncpy(mode.enumValues, "on,off,blink", sizeof(mode.enumValues) - 1);
```

---

## Quick start — Mount

```cpp
#include <Arduino.h>
#include "ModularSerial.h"

using namespace MSC;

// Serial0 → primary controller (Pi), Serial1 → 4-pin effector connector
MountNode mount(Serial, Serial1);

void setup() {
    Serial.begin(115200);   // to primary controller
    Serial1.begin(115200);  // to effector

    mount.onEffectorConnected([](const Capabilities& caps) {
        // Called when an effector advertises itself.
        // The advertisement is automatically forwarded to the controller;
        // use this callback for any local handling you need.
        (void)caps;
    });

    mount.onEffectorDisconnected([]() {
        // Called when the heartbeat watchdog fires.
        // A disconnect event is automatically sent to the controller.
    });

    mount.setHeartbeatTimeout(6000); // ms — default 6000 (3× effector heartbeat)
    mount.begin();                   // sends a query to the effector on startup
}

void loop() {
    mount.loop(); // bridges both serial ports, manages watchdog
}
```

---

## Packet reference

All packets are JSON, CRC32-framed as described above.

### Effector → Mount → Primary

| Packet | When |
|--------|------|
| `{"type":"advertisement","device":"…","version":"…","commands":{…}}` | On power-up, or in response to a `query` |
| `{"type":"heartbeat"}` | Every `heartbeat_interval` ms |
| `{"type":"response","id":"…","status":"ok","data":{…}}` | After a successful command |
| `{"type":"response","id":"…","status":"error","error":"…"}` | After a failed command |

### Mount → Primary only

| Packet | When |
|--------|------|
| `{"type":"ack","id":"…"}` | Immediately on receiving a valid command from the primary |
| `{"type":"event","event":"connect"}` | Effector advertisement received for first time |
| `{"type":"event","event":"disconnect"}` | Heartbeat watchdog fired |

### Primary → Mount → Effector

| Packet | When |
|--------|------|
| `{"type":"command","id":"…","command":"…","params":{…}}` | To execute a command |
| `{"type":"command",…,"traceparent":"00-…"}` | Same, with W3C trace context |
| `{"type":"query"}` | Ask the effector to re-send its advertisement |

---

## OpenTelemetry

OTel is opt-in. Enable it with the `-DMSC_OTEL_ENABLED` build flag and add `otel-embedded-cpp` to `lib_deps`.

The serial control path is completely unaffected if WiFi drops — OTel data is queued and sent when the connection is available.

### Effector

```cpp
// Call before begin(). WiFi must be connected first.
node.enableOtel("http://192.168.1.10:4318", "robotics");
```

This instruments:
- A span `command.execute.<name>` for every command, linked to the Pi's trace
- Counter `msc.commands.executed` (labels: `command`, `status`)
- Gauge `msc.command.duration_ms`
- Log events for advertisement, query, and CRC errors

### Mount

```cpp
mount.enableOtel("http://192.168.1.10:4318", "robotics");
```

This instruments:
- A span `command.forward` for every in-flight command, child of the Pi's span
- The `traceparent` in the forwarded packet is rewritten to point at the mount's span, so the effector's span becomes its child
- Gauge `msc.mount.forward_duration_ms`
- Log events for connect, disconnect, and CRC errors

The resulting trace in your backend looks like:

```
Pi: command.send  ──────────────────────────────────────────
    Mount: command.forward  ───────────────────────────────
        Effector: command.execute.set_position  ──────────
```

### OTel build flags

```ini
build_flags =
    -DMSC_OTEL_ENABLED
    -DWIFI_SSID='"your-ssid"'
    -DWIFI_PASS='"your-password"'
    -DOTEL_COLLECTOR_HOST='"192.168.1.10"'
    -DOTEL_COLLECTOR_PORT=4318
    -DOTEL_SERVICE_NAME='"grabber"'
    -DOTEL_SERVICE_NAMESPACE='"robotics"'
    -DOTEL_SERVICE_VERSION='"1.0.0"'
    -DOTEL_SERVICE_INSTANCE='"effector-1"'
    -DOTEL_DEPLOY_ENV='"production"'
```

Use an `.env` file to keep credentials out of source control (see `.env.sample`).

---

## Configuration

All limits are compile-time macros that can be overridden in `build_flags`:

| Macro | Default | Description |
|-------|---------|-------------|
| `MSC_MAX_FRAME_SIZE` | `512` | Maximum JSON payload size in bytes |
| `MSC_MAX_COMMANDS` | `16` | Maximum commands per effector |
| `MSC_MAX_PARAMS` | `8` | Maximum parameters per command |
| `MSC_MAX_INFLIGHT` | `8` | Maximum simultaneous in-flight commands on the mount |

---

## Running the tests

```bash
# Unit tests (Frame + Schema) — runs on Linux, no hardware needed
pio test -e native

# Protocol tests — requires a connected ESP32-C3
cp .env.sample .env   # fill in your WiFi + OTel details
export $(grep -v '^#' .env | xargs)
pio test -e esp32-c3
```

---

## Licence

MIT
