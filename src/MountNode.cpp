#ifdef ARDUINO
#include "MSC/MountNode.h"
#include <string.h>

namespace MSC {

MountNode::MountNode(Stream& toController, Stream& toEffector)
    : _toController(toController), _toEffector(toEffector)
{}

#ifdef MSC_OTEL_ENABLED
void MountNode::enableOtel(const char* collectorUrl, const char* serviceNs) {
    strncpy(_collectorUrl, collectorUrl, sizeof(_collectorUrl) - 1);
    strncpy(_serviceNs,    serviceNs,    sizeof(_serviceNs) - 1);
    _otelEnabled = true;
}

void MountNode::_initOtel() {
    auto& res = OTel::defaultResource();
    res.set("service.name",      "msc-mount");
    res.set("service.namespace", _serviceNs);

    OTel::Tracer::begin("msc-mount", "0.1.0");
    OTel::Metrics::begin("msc-mount", "0.1.0");
    OTel::Logger::logInfo("MountNode started");
}
#endif

void MountNode::begin() {
#ifdef MSC_OTEL_ENABLED
    if (_otelEnabled) _initOtel();
#endif
    // Query the effector in case it booted before the mount
    JsonDocument q;
    q["type"] = "query";
    _sendFrame(q, _toEffector);
}

void MountNode::loop() {
    // ── Controller → Effector ────────────────────────────────────────────────
    while (_toController.available()) {
        _fromController.feed(static_cast<uint8_t>(_toController.read()));
    }
    if (_fromController.hasError()) {
#ifdef MSC_OTEL_ENABLED
        if (_otelEnabled) OTel::Logger::logWarn("Frame CRC error on controller RX");
#endif
        _fromController.consume();
    }
    if (_fromController.hasMessage()) {
        _handleFromController(_fromController.message());
        _fromController.consume();
    }

    // ── Effector → Controller ────────────────────────────────────────────────
    while (_toEffector.available()) {
        _fromEffector.feed(static_cast<uint8_t>(_toEffector.read()));
    }
    if (_fromEffector.hasError()) {
#ifdef MSC_OTEL_ENABLED
        if (_otelEnabled) OTel::Logger::logWarn("Frame CRC error on effector RX");
#endif
        _fromEffector.consume();
    }
    if (_fromEffector.hasMessage()) {
        _handleFromEffector(_fromEffector.message());
        _fromEffector.consume();
    }

    _checkHeartbeatTimeout();
}

// ── From controller (Pi) ──────────────────────────────────────────────────────

void MountNode::_handleFromController(const char* json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    const char* type = doc["type"] | "";

    if (strcmp(type, "command") == 0) {
        const char* id      = doc["id"]      | "";
        const char* cmdName = doc["command"] | "";

        int slot = _findFreeSlot();
        if (slot < 0) {
            // All slots busy: reject with error
            JsonDocument err;
            err["type"]   = "response";
            err["id"]     = id;
            err["status"] = "error";
            err["error"]  = "mount: too many in-flight commands";
            _sendFrame(err, _toController);
            return;
        }

        // Acknowledge receipt to the controller immediately
        _sendAck(id);

#ifdef MSC_OTEL_ENABLED
        if (_otelEnabled) {
            _beginCommandSpan(slot, cmdName, id, doc["traceparent"] | "");
            // Rewrite traceparent so effector becomes child of mount's span
            doc.remove("traceparent");
            OTel::Propagators::injectToJson(doc);
        }
#endif

        // Record inflight
        strncpy(_inflight[slot].id, id, sizeof(_inflight[slot].id) - 1);
        _inflight[slot].startMs = millis();
        _inflight[slot].active  = true;

        // Forward to effector
        _forwardToEffector(doc);

    } else if (strcmp(type, "query") == 0) {
        // Pi is asking the mount to re-query the effector
        JsonDocument q;
        q["type"] = "query";
        _sendFrame(q, _toEffector);
    }
}

// ── From effector ─────────────────────────────────────────────────────────────

void MountNode::_handleFromEffector(const char* json) {
    // Any packet from the effector resets the watchdog
    _lastEffectorPacket = millis();

    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    const char* type = doc["type"] | "";

    if (strcmp(type, "advertisement") == 0) {
        Capabilities caps = Capabilities::fromJson(json);
        if (!_effectorConnected) {
            _effectorConnected = true;
            _sendEvent("connect");
            if (_onConnected) _onConnected(caps);
#ifdef MSC_OTEL_ENABLED
            if (_otelEnabled)
                OTel::Logger::logInfo(String("Effector connected: ") + caps.deviceName);
#endif
        }
        // Forward advertisement to controller so it knows the capabilities
        _forwardToController(doc);

    } else if (strcmp(type, "response") == 0) {
        const char* id  = doc["id"]     | "";
        bool        ok  = strcmp(doc["status"] | "", "ok") == 0;

        int slot = _findSlotById(id);
#ifdef MSC_OTEL_ENABLED
        if (_otelEnabled && slot >= 0) _endCommandSpan(slot, ok);
#endif
        if (slot >= 0) _freeSlot(slot);

        _forwardToController(doc);

    } else if (strcmp(type, "heartbeat") == 0) {
        // Consumed here — heartbeats are for watchdog only, not forwarded
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void MountNode::_forwardToEffector(JsonDocument& doc) {
    _sendFrame(doc, _toEffector);
}

void MountNode::_forwardToController(JsonDocument& doc) {
    _sendFrame(doc, _toController);
}

void MountNode::_sendAck(const char* id) {
    JsonDocument doc;
    doc["type"] = "ack";
    doc["id"]   = id;
    _sendFrame(doc, _toController);
}

void MountNode::_sendEvent(const char* event) {
    JsonDocument doc;
    doc["type"]  = "event";
    doc["event"] = event;
    _sendFrame(doc, _toController);
}

void MountNode::_sendFrame(JsonDocument& doc, Stream& dest) {
    char jsonBuf[MSC_MAX_FRAME_SIZE];
    if (serializeJson(doc, jsonBuf, sizeof(jsonBuf)) == 0) return;

    char frameBuf[MSC_MAX_FRAME_SIZE + 16];
    if (frameEncode(jsonBuf, frameBuf, sizeof(frameBuf))) {
        dest.print(frameBuf);
    }
}

void MountNode::_checkHeartbeatTimeout() {
    if (!_effectorConnected) return;
    if (millis() - _lastEffectorPacket > _heartbeatTimeout) {
        _onDisconnectDetected();
    }
}

void MountNode::_onDisconnectDetected() {
    _effectorConnected = false;

    // End any inflight spans as errors and free all slots
    for (int i = 0; i < MSC_MAX_INFLIGHT; i++) {
        if (!_inflight[i].active) continue;
#ifdef MSC_OTEL_ENABLED
        if (_otelEnabled) _endCommandSpan(i, false);
#endif
        _freeSlot(i);
    }

    _sendEvent("disconnect");
    if (_onDisconnected) _onDisconnected();

#ifdef MSC_OTEL_ENABLED
    if (_otelEnabled) OTel::Logger::logWarn("Effector disconnected (heartbeat timeout)");
#endif
}

int MountNode::_findFreeSlot() const {
    for (int i = 0; i < MSC_MAX_INFLIGHT; i++) {
        if (!_inflight[i].active) return i;
    }
    return -1;
}

int MountNode::_findSlotById(const char* id) const {
    for (int i = 0; i < MSC_MAX_INFLIGHT; i++) {
        if (_inflight[i].active && strcmp(_inflight[i].id, id) == 0) return i;
    }
    return -1;
}

void MountNode::_freeSlot(int slot) {
    _inflight[slot].active  = false;
    _inflight[slot].id[0]   = '\0';
    _inflight[slot].startMs = 0;
#ifdef MSC_OTEL_ENABLED
    delete _inflight[slot].span;
    _inflight[slot].span = nullptr;
#endif
}

// ── OTel span management ──────────────────────────────────────────────────────

#ifdef MSC_OTEL_ENABLED
void MountNode::_beginCommandSpan(int slot, const char* cmdName,
                                  const char* cmdId, const char* traceparent) {
    // Save the context that was current before this command
    _inflight[slot].savedTraceId = OTel::currentTraceContext().traceId;
    _inflight[slot].savedSpanId  = OTel::currentTraceContext().spanId;

    // Install the Pi's context so our new span becomes its child
    if (traceparent && traceparent[0] != '\0') {
        OTel::ExtractedContext ext;
        if (OTel::parseTraceparent(String(traceparent), ext)) {
            OTel::currentTraceContext().traceId = ext.ctx.traceId;
            OTel::currentTraceContext().spanId  = ext.ctx.spanId;
        }
    }

    // Start the mount's child span — Span constructor captures the Pi context
    // as parent and installs the mount's own spanId as current
    delete _inflight[slot].span; // defensive: shouldn't be set, but be safe
    _inflight[slot].span = new OTel::Span("command.forward");
    _inflight[slot].span->setAttribute("command.id",   String(cmdId));
    _inflight[slot].span->setAttribute("command.name", String(cmdName));
}

void MountNode::_endCommandSpan(int slot, bool ok) {
    if (!_inflight[slot].span) return;

    uint32_t dur = millis() - _inflight[slot].startMs;
    _inflight[slot].span->setAttribute("success", ok);
    _inflight[slot].span->setAttribute(
        "duration_ms", static_cast<int64_t>(dur));
    _inflight[slot].span->end();
    delete _inflight[slot].span;
    _inflight[slot].span = nullptr;

    // span.end() restored Pi's context; now restore what was there before Pi's
    OTel::currentTraceContext().traceId = _inflight[slot].savedTraceId;
    OTel::currentTraceContext().spanId  = _inflight[slot].savedSpanId;

    OTel::Metrics::gauge("msc.mount.forward_duration_ms",
        static_cast<double>(dur), "ms");
}
#endif

} // namespace MSC
#endif // ARDUINO
