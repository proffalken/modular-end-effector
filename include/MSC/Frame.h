#pragma once
#include <stdint.h>
#include <stddef.h>

namespace MSC {

#ifndef MSC_MAX_FRAME_SIZE
#define MSC_MAX_FRAME_SIZE 512
#endif

// ── CRC32 (IEEE 802.3 / zlib polynomial) ────────────────────────────────────
uint32_t crc32(const uint8_t* data, size_t len);

// ── Encode ───────────────────────────────────────────────────────────────────
// Writes: <json>\n<CRC32_8HEX>\n into out.
// Returns bytes written (excluding null terminator), or 0 if out is too small.
size_t frameEncode(const char* json, char* out, size_t outSize);

// ── Validate ─────────────────────────────────────────────────────────────────
// Returns true if crcHex (8 uppercase hex chars) matches CRC32 of json.
bool frameValidate(const char* json, const char* crcHex);

// ── FrameReader ───────────────────────────────────────────────────────────────
// Incremental byte-by-byte reader. Feed bytes from your serial stream; call
// hasMessage() to check if a complete, validated frame is ready.
class FrameReader {
public:
    // Feed one byte. Ignored while a message is pending (call consume() first).
    void feed(uint8_t b);

    bool hasMessage() const { return _hasMessage; }
    bool hasError()   const { return _hasError;   }

    // Valid only when hasMessage() == true. Pointer is owned by this object.
    const char* message() const { return _pending; }

    void consume() { _hasMessage = false; _hasError = false; }

private:
    enum State { JSON_LINE, CRC_LINE } _state = JSON_LINE;

    char   _jsonBuf[MSC_MAX_FRAME_SIZE] = {};
    char   _crcBuf[12]                  = {};
    size_t _jsonLen                     = 0;
    size_t _crcLen                      = 0;

    char _pending[MSC_MAX_FRAME_SIZE] = {};
    bool _hasMessage                  = false;
    bool _hasError                    = false;

    void _onCrcComplete();
};

} // namespace MSC
