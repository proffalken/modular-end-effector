#include "MSC/Frame.h"
#include <stdio.h>
#include <string.h>

namespace MSC {

uint32_t crc32(const uint8_t* data, size_t len) {
    if (!data || len == 0) return 0;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
        }
    }
    return ~crc;
}

size_t frameEncode(const char* json, char* out, size_t outSize) {
    if (!json || !out) return 0;
    size_t jsonLen = strlen(json);
    // Needs: json + \n + 8 hex + \n + \0  (minimum 11 overhead bytes)
    if (outSize < jsonLen + 11) return 0;

    uint32_t crc = crc32(reinterpret_cast<const uint8_t*>(json), jsonLen);

    size_t pos = 0;
    memcpy(out + pos, json, jsonLen);
    pos += jsonLen;
    out[pos++] = '\n';
    snprintf(out + pos, 9, "%08X", static_cast<unsigned int>(crc));
    pos += 8;
    out[pos++] = '\n';
    out[pos]   = '\0';
    return pos;
}

bool frameValidate(const char* json, const char* crcHex) {
    if (!json || !crcHex) return false;
    unsigned int received = 0;
    if (sscanf(crcHex, "%08X", &received) != 1) return false;
    size_t jsonLen = strlen(json);
    uint32_t expected = crc32(reinterpret_cast<const uint8_t*>(json), jsonLen);
    return expected == static_cast<uint32_t>(received);
}

void FrameReader::feed(uint8_t b) {
    // Block new input while a message or error is waiting to be consumed
    if (_hasMessage || _hasError) return;

    if (_state == JSON_LINE) {
        if (b == '\n') {
            _jsonBuf[_jsonLen] = '\0';
            _state  = CRC_LINE;
            _crcLen = 0;
        } else if (_jsonLen < MSC_MAX_FRAME_SIZE - 1) {
            _jsonBuf[_jsonLen++] = static_cast<char>(b);
        }
    } else {
        if (b == '\n') {
            _crcBuf[_crcLen] = '\0';
            _onCrcComplete();
            _state  = JSON_LINE;
            _jsonLen = 0;
        } else if (_crcLen < sizeof(_crcBuf) - 1) {
            _crcBuf[_crcLen++] = static_cast<char>(b);
        }
    }
}

void FrameReader::_onCrcComplete() {
    if (frameValidate(_jsonBuf, _crcBuf)) {
        memcpy(_pending, _jsonBuf, _jsonLen + 1);
        _hasMessage = true;
    } else {
        _hasError = true;
    }
}

} // namespace MSC
