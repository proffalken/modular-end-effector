#include "MSC/Schema.h"
#include <string.h>
#include <stdio.h>

namespace MSC {

// ── Internal helpers ──────────────────────────────────────────────────────────

static const char* paramTypeName(ParamType t) {
    switch (t) {
        case ParamType::INT:    return "int";
        case ParamType::FLOAT:  return "float";
        case ParamType::BOOL:   return "bool";
        case ParamType::ENUM:   return "enum";
        default:                return "string";
    }
}

static ParamType paramTypeFromName(const char* name) {
    if (!name)                    return ParamType::STRING;
    if (strcmp(name, "int")   == 0) return ParamType::INT;
    if (strcmp(name, "float") == 0) return ParamType::FLOAT;
    if (strcmp(name, "bool")  == 0) return ParamType::BOOL;
    if (strcmp(name, "enum")  == 0) return ParamType::ENUM;
    return ParamType::STRING;
}

// ── ParamDef ──────────────────────────────────────────────────────────────────

void ParamDef::serialiseTo(JsonObject obj) const {
    obj["type"] = paramTypeName(type);

    if (type == ParamType::INT || type == ParamType::FLOAT) {
        obj["min"] = minVal;
        obj["max"] = maxVal;
        if (unit[0]) obj["unit"] = unit;
    }

    if (type == ParamType::ENUM && enumValues[0]) {
        JsonArray arr = obj["values"].to<JsonArray>();
        char tmp[sizeof(enumValues)];
        strncpy(tmp, enumValues, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char* tok = strtok(tmp, ",");
        while (tok) {
            arr.add(tok);
            tok = strtok(nullptr, ",");
        }
    }
}

ParamDef ParamDef::fromJson(const char* paramName, JsonObjectConst obj) {
    ParamDef p;
    strncpy(p.name, paramName, sizeof(p.name) - 1);
    p.type   = paramTypeFromName(obj["type"] | "string");
    p.minVal = obj["min"] | 0.0f;
    p.maxVal = obj["max"] | 0.0f;
    strncpy(p.unit, obj["unit"] | "", sizeof(p.unit) - 1);
    p.enumValues[0] = '\0';

    if (obj["values"].is<JsonArrayConst>()) {
        char* dst      = p.enumValues;
        size_t remain  = sizeof(p.enumValues) - 1;
        bool   first   = true;

        for (JsonVariantConst v : obj["values"].as<JsonArrayConst>()) {
            const char* s = v.as<const char*>();
            if (!s) continue;
            size_t slen = strlen(s);
            if (!first && remain > 1) { *dst++ = ','; remain--; }
            if (slen < remain) {
                memcpy(dst, s, slen);
                dst    += slen;
                remain -= slen;
            }
            first = false;
        }
        *dst = '\0';
    }
    return p;
}

// ── CommandDef ────────────────────────────────────────────────────────────────

void CommandDef::serialiseTo(JsonObject obj) const {
    if (description[0]) obj["description"] = description;

    if (paramCount > 0) {
        JsonObject paramsObj = obj["params"].to<JsonObject>();
        for (uint8_t i = 0; i < paramCount; i++) {
            JsonObject pObj = paramsObj[params[i].name].to<JsonObject>();
            params[i].serialiseTo(pObj);
        }
    }
}

// ── Capabilities ─────────────────────────────────────────────────────────────

size_t Capabilities::toJson(char* buf, size_t bufSize) const {
    JsonDocument doc;
    doc["type"]    = "advertisement";
    doc["device"]  = deviceName;
    doc["version"] = version;

    JsonObject cmdsObj = doc["commands"].to<JsonObject>();
    for (uint8_t i = 0; i < commandCount; i++) {
        JsonObject cmdObj = cmdsObj[commands[i].name].to<JsonObject>();
        commands[i].serialiseTo(cmdObj);
    }

    if (measureJson(doc) + 1 > bufSize) return 0;
    return serializeJson(doc, buf, bufSize);
}

Capabilities Capabilities::fromJson(const char* json) {
    Capabilities caps;

    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return caps;
    if (strcmp(doc["type"] | "", "advertisement") != 0) return caps;

    strncpy(caps.deviceName, doc["device"]  | "", sizeof(caps.deviceName) - 1);
    strncpy(caps.version,    doc["version"] | "", sizeof(caps.version) - 1);

    if (!doc["commands"].is<JsonObjectConst>()) return caps;

    for (JsonPairConst cmdPair : doc["commands"].as<JsonObjectConst>()) {
        if (caps.commandCount >= MSC_MAX_COMMANDS) break;

        CommandDef cmd;
        strncpy(cmd.name,        cmdPair.key().c_str(),                  sizeof(cmd.name) - 1);
        strncpy(cmd.description, cmdPair.value()["description"] | "",    sizeof(cmd.description) - 1);
        cmd.paramCount = 0;

        if (cmdPair.value()["params"].is<JsonObjectConst>()) {
            for (JsonPairConst pPair : cmdPair.value()["params"].as<JsonObjectConst>()) {
                if (cmd.paramCount >= MSC_MAX_PARAMS) break;
                ParamDef p = ParamDef::fromJson(
                    pPair.key().c_str(),
                    pPair.value().as<JsonObjectConst>()
                );
                cmd.addParam(p);
            }
        }
        caps.addCommand(cmd);
    }
    return caps;
}

} // namespace MSC
