#pragma once
#ifdef ARDUINO

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include "MSC/Frame.h"
#include "MSC/Schema.h"

#ifdef MSC_OTEL_ENABLED
#include "OtelTracer.h"
#include "OtelLogger.h"
#include "OtelMetrics.h"
#endif

namespace MSC {

// Handler signature: receives parsed params, writes optional result fields.
// Return true for success, false for error.
using CommandHandler = std::function<bool(JsonObjectConst params, JsonObject result)>;

struct RegisteredCommand {
    CommandDef     def;
    CommandHandler handler;
};

class EffectorNode {
public:
    // serial      — the UART connected to the mount (Serial1 on ESP32-C3)
    // deviceName  — human-readable name sent in the advertisement
    // version     — semver string sent in the advertisement
    EffectorNode(Stream& serial, const char* deviceName, const char* version);

    // Register a command the effector can handle.
    void addCommand(const CommandDef& def, CommandHandler handler);

    // How often to send a heartbeat (ms). Default 2000.
    void setHeartbeatInterval(uint32_t ms) { _heartbeatInterval = ms; }

#ifdef MSC_OTEL_ENABLED
    // Call before begin(). WiFi must be connected before begin() is called.
    // collectorUrl  — full base URL, e.g. "http://192.168.1.10:4318"
    // serviceNs     — OTel service.namespace attribute
    void enableOtel(const char* collectorUrl, const char* serviceNs = "msc");
#endif

    // Sends the initial advertisement. Call once in setup(), after WiFi if OTel is enabled.
    void begin();

    // Call from loop(). Reads serial, dispatches commands, sends heartbeats.
    void loop();

private:
    Stream&  _serial;
    char     _deviceName[32];
    char     _version[16];
    uint32_t _heartbeatInterval = 2000;
    uint32_t _lastHeartbeatMs   = 0;

    RegisteredCommand _commands[MSC_MAX_COMMANDS];
    uint8_t           _commandCount = 0;

    FrameReader _reader;

    void _sendAdvertisement();
    void _sendHeartbeat();
    void _processPacket(const char* json);
    void _handleCommand(JsonObjectConst doc);
    void _sendResponse(const char* id, bool ok, JsonObjectConst data, const char* errMsg);
    void _sendFrame(JsonDocument& doc);

#ifdef MSC_OTEL_ENABLED
    bool _otelEnabled = false;
    char _collectorUrl[64] = {};
    char _serviceNs[32]    = {};

    void _initOtel();
#endif
};

} // namespace MSC
#endif // ARDUINO
