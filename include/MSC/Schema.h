#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <string.h>

namespace MSC {

#ifndef MSC_MAX_COMMANDS
#define MSC_MAX_COMMANDS 16
#endif

#ifndef MSC_MAX_PARAMS
#define MSC_MAX_PARAMS 8
#endif

// ── Parameter types ───────────────────────────────────────────────────────────

enum class ParamType : uint8_t { INT, FLOAT, BOOL, STRING, ENUM };

struct ParamDef {
    char     name[32]        = {};
    ParamType type           = ParamType::STRING;
    float    minVal          = 0.0f;
    float    maxVal          = 0.0f;
    char     unit[16]        = {};
    char     enumValues[128] = {}; // comma-separated, e.g. "on,off,blink"

    void serialiseTo(JsonObject obj) const;
    static ParamDef fromJson(const char* name, JsonObjectConst obj);
};

// ── Command definition ────────────────────────────────────────────────────────

struct CommandDef {
    char    name[32]                = {};
    char    description[64]         = {};
    ParamDef params[MSC_MAX_PARAMS] = {};
    uint8_t paramCount              = 0;

    bool addParam(const ParamDef& p) {
        if (paramCount >= MSC_MAX_PARAMS) return false;
        params[paramCount++] = p;
        return true;
    }

    void serialiseTo(JsonObject obj) const;
};

// ── Device capabilities ───────────────────────────────────────────────────────

struct Capabilities {
    char       deviceName[32]              = {};
    char       version[16]                 = {};
    CommandDef commands[MSC_MAX_COMMANDS]  = {};
    uint8_t    commandCount                = 0;

    bool addCommand(const CommandDef& cmd) {
        if (commandCount >= MSC_MAX_COMMANDS) return false;
        commands[commandCount++] = cmd;
        return true;
    }

    // Returns bytes written (excl. null), or 0 if buf is too small.
    size_t toJson(char* buf, size_t bufSize) const;

    // Returns an invalid Capabilities (valid() == false) on any parse error.
    static Capabilities fromJson(const char* json);

    bool valid() const { return deviceName[0] != '\0'; }
};

} // namespace MSC
