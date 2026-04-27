#ifdef ARDUINO
#include "MSC/EffectorNode.h"
#include <string.h>

namespace MSC {

EffectorNode::EffectorNode(Stream& serial, const char* deviceName, const char* version)
    : _serial(serial)
{
    strncpy(_deviceName, deviceName, sizeof(_deviceName) - 1);
    strncpy(_version,    version,    sizeof(_version) - 1);
}

void EffectorNode::addCommand(const CommandDef& def, CommandHandler handler) {
    if (_commandCount >= MSC_MAX_COMMANDS) return;
    _commands[_commandCount].def     = def;
    _commands[_commandCount].handler = handler;
    _commandCount++;
}

#ifdef MSC_OTEL_ENABLED
void EffectorNode::enableOtel(const char* collectorUrl, const char* serviceNs) {
    strncpy(_collectorUrl, collectorUrl, sizeof(_collectorUrl) - 1);
    strncpy(_serviceNs,    serviceNs,    sizeof(_serviceNs) - 1);
    _otelEnabled = true;
}

void EffectorNode::_initOtel() {
    auto& res = OTel::defaultResource();
    res.set("service.name",      _deviceName);
    res.set("service.namespace", _serviceNs);
    res.set("service.version",   _version);

    OTel::Tracer::begin("msc-effector", "0.1.0");
    OTel::Metrics::begin("msc-effector", "0.1.0");
    OTel::Metrics::setDefaultMetricLabel("device.name", String(_deviceName));

    OTel::Logger::logInfo(String("EffectorNode started: ") + _deviceName);
}
#endif

void EffectorNode::begin() {
#ifdef MSC_OTEL_ENABLED
    if (_otelEnabled) _initOtel();
#endif
    _sendAdvertisement();
}

void EffectorNode::loop() {
    // Drain available serial bytes into the frame reader
    while (_serial.available()) {
        uint8_t b = static_cast<uint8_t>(_serial.read());
        _reader.feed(b);
    }

    if (_reader.hasError()) {
        // Bad CRC — log and discard
#ifdef MSC_OTEL_ENABLED
        if (_otelEnabled) OTel::Logger::logWarn("Frame CRC error on effector RX");
#endif
        _reader.consume();
    }

    if (_reader.hasMessage()) {
        _processPacket(_reader.message());
        _reader.consume();
    }

    // Periodic heartbeat
    if (millis() - _lastHeartbeatMs >= _heartbeatInterval) {
        _sendHeartbeat();
        _lastHeartbeatMs = millis();
    }
}

// ── Private ───────────────────────────────────────────────────────────────────

void EffectorNode::_sendAdvertisement() {
    Capabilities caps;
    strncpy(caps.deviceName, _deviceName, sizeof(caps.deviceName) - 1);
    strncpy(caps.version,    _version,    sizeof(caps.version) - 1);
    for (uint8_t i = 0; i < _commandCount; i++) {
        caps.addCommand(_commands[i].def);
    }

    char jsonBuf[MSC_MAX_FRAME_SIZE];
    caps.toJson(jsonBuf, sizeof(jsonBuf));

    char frameBuf[MSC_MAX_FRAME_SIZE + 16];
    if (frameEncode(jsonBuf, frameBuf, sizeof(frameBuf))) {
        _serial.print(frameBuf);
    }

#ifdef MSC_OTEL_ENABLED
    if (_otelEnabled) OTel::Logger::logInfo(String("Advertisement sent: ") + _deviceName);
#endif
}

void EffectorNode::_sendHeartbeat() {
    JsonDocument doc;
    doc["type"] = "heartbeat";
    _sendFrame(doc);
}

void EffectorNode::_processPacket(const char* json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    const char* type = doc["type"] | "";

    if (strcmp(type, "command") == 0) {
        _handleCommand(doc.as<JsonObjectConst>());
    } else if (strcmp(type, "query") == 0) {
        _sendAdvertisement();
    }
    // Other packet types (e.g. ack) are silently ignored on the effector
}

void EffectorNode::_handleCommand(JsonObjectConst doc) {
    const char* id      = doc["id"]      | "";
    const char* cmdName = doc["command"] | "";

#ifdef MSC_OTEL_ENABLED
    uint32_t startMs = 0;
    if (_otelEnabled) {
        // Extract traceparent from Pi→Mount→Effector chain and start child span
        const char* tp = doc["traceparent"] | "";
        if (tp[0] != '\0') {
            OTel::ExtractedContext ext;
            if (OTel::parseTraceparent(String(tp), ext)) {
                OTel::currentTraceContext().traceId = ext.ctx.traceId;
                OTel::currentTraceContext().spanId  = ext.ctx.spanId;
            }
        }
        startMs = millis();
    }
    auto span = _otelEnabled
        ? OTel::Tracer::startSpan(String("command.execute.") + cmdName)
        : OTel::Tracer::startSpan("noop"); // won't be used
    if (_otelEnabled) {
        span.setAttribute("command.id",   String(id));
        span.setAttribute("command.name", String(cmdName));
    }
#endif

    for (uint8_t i = 0; i < _commandCount; i++) {
        if (strcmp(_commands[i].def.name, cmdName) != 0) continue;

        JsonDocument resultDoc;
        JsonObject   resultObj = resultDoc.to<JsonObject>();
        bool ok = _commands[i].handler(
            doc["params"].is<JsonObjectConst>()
                ? doc["params"].as<JsonObjectConst>()
                : JsonObjectConst{},
            resultObj
        );

        _sendResponse(id, ok, resultDoc.as<JsonObjectConst>(), ok ? nullptr : "handler error");

#ifdef MSC_OTEL_ENABLED
        if (_otelEnabled) {
            uint32_t dur = millis() - startMs;
            span.setAttribute("success", ok);
            OTel::Metrics::sum("msc.commands.executed", 1.0, true, "DELTA", "1",
                {{"command", cmdName}, {"status", ok ? "ok" : "error"}});
            OTel::Metrics::gauge("msc.command.duration_ms", static_cast<double>(dur), "ms",
                {{"command", cmdName}});
            span.end();
        }
#endif
        return;
    }

    // Unknown command
    _sendResponse(id, false, JsonObjectConst{}, "unknown command");

#ifdef MSC_OTEL_ENABLED
    if (_otelEnabled) {
        span.setAttribute("success", false);
        span.setAttribute("error", "unknown command");
        OTel::Metrics::sum("msc.commands.executed", 1.0, true, "DELTA", "1",
            {{"command", cmdName}, {"status", "unknown"}});
        span.end();
    }
#endif
}

void EffectorNode::_sendResponse(const char* id, bool ok,
                                 JsonObjectConst data, const char* errMsg) {
    JsonDocument doc;
    doc["type"]   = "response";
    doc["id"]     = id;
    doc["status"] = ok ? "ok" : "error";
    if (!ok && errMsg) doc["error"] = errMsg;

    if (ok && data) {
        for (JsonPairConst kv : data) {
            doc["data"][kv.key()] = kv.value();
        }
    }
    _sendFrame(doc);
}

void EffectorNode::_sendFrame(JsonDocument& doc) {
    char jsonBuf[MSC_MAX_FRAME_SIZE];
    if (serializeJson(doc, jsonBuf, sizeof(jsonBuf)) == 0) return;

    char frameBuf[MSC_MAX_FRAME_SIZE + 16];
    if (frameEncode(jsonBuf, frameBuf, sizeof(frameBuf))) {
        _serial.print(frameBuf);
    }
}

} // namespace MSC
#endif // ARDUINO
