#pragma once
#ifdef ARDUINO

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <optional>
#include "MSC/Frame.h"
#include "MSC/Schema.h"

#ifdef MSC_OTEL_ENABLED
#include "OtelTracer.h"
#include "OtelLogger.h"
#include "OtelMetrics.h"
#endif

namespace MSC {

#ifndef MSC_MAX_INFLIGHT
#define MSC_MAX_INFLIGHT 8
#endif

class MountNode {
public:
    // toController — UART facing the primary controller / Pi (e.g. Serial0)
    // toEffector   — UART facing the effector via 4-pin connector (e.g. Serial1)
    MountNode(Stream& toController, Stream& toEffector);

    // How many missed heartbeat intervals before declaring disconnect.
    // Default timeout = 3 × effector's heartbeat interval; set explicitly
    // here in ms if needed (default 6000 ms).
    void setHeartbeatTimeout(uint32_t ms) { _heartbeatTimeout = ms; }

#ifdef MSC_OTEL_ENABLED
    // WiFi must be connected before begin() is called.
    void enableOtel(const char* collectorUrl, const char* serviceNs = "msc");
#endif

    // Called when the effector sends its advertisement (connect or query response).
    void onEffectorConnected(std::function<void(const Capabilities&)> cb) {
        _onConnected = cb;
    }
    // Called when the heartbeat watchdog fires.
    void onEffectorDisconnected(std::function<void()> cb) {
        _onDisconnected = cb;
    }

    void begin();
    void loop();

private:
    Stream& _toController;
    Stream& _toEffector;

    FrameReader _fromController;
    FrameReader _fromEffector;

    bool     _effectorConnected  = false;
    uint32_t _lastEffectorPacket = 0;
    uint32_t _heartbeatTimeout   = 6000;

    std::function<void(const Capabilities&)> _onConnected;
    std::function<void()>                    _onDisconnected;

    // ── In-flight command tracking (for span correlation) ────────────────────
    struct InflightCmd {
        char     id[33]    = {};
        uint32_t startMs   = 0;
        bool     active    = false;
#ifdef MSC_OTEL_ENABLED
        // Saved context so we can restore it cleanly after the span ends
        String savedTraceId;
        String savedSpanId;
        std::optional<OTel::Span> span;
#endif
    };
    InflightCmd _inflight[MSC_MAX_INFLIGHT];

    int  _findFreeSlot() const;
    int  _findSlotById(const char* id) const;
    void _freeSlot(int slot);

    void _handleFromController(const char* json);
    void _handleFromEffector(const char* json);
    void _forwardToEffector(JsonDocument& doc);
    void _forwardToController(JsonDocument& doc);
    void _sendAck(const char* id);
    void _sendEvent(const char* event);
    void _sendFrame(JsonDocument& doc, Stream& dest);
    void _checkHeartbeatTimeout();
    void _onDisconnectDetected();

#ifdef MSC_OTEL_ENABLED
    bool _otelEnabled = false;
    char _collectorUrl[64] = {};
    char _serviceNs[32]    = {};

    void _initOtel();
    void _beginCommandSpan(int slot, const char* cmdName, const char* cmdId,
                           const char* traceparent);
    void _endCommandSpan(int slot, bool ok);
#endif
};

} // namespace MSC
#endif // ARDUINO
