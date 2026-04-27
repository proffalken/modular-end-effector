#include <unity.h>
#include "MSC/Frame.h"
#include <string.h>

using namespace MSC;

void setUp() {}
void tearDown() {}

// ── CRC32 ────────────────────────────────────────────────────────────────────

void test_crc32_known_vector() {
    // CRC32 of "123456789" = 0xCBF43926 (standard Ethernet/zlib polynomial)
    const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32(data, 9));
}

void test_crc32_empty_input() {
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, crc32(nullptr, 0));
}

// ── frameEncode ──────────────────────────────────────────────────────────────

void test_encode_produces_two_lines() {
    const char* json = "{\"type\":\"heartbeat\"}";
    char out[256];
    size_t len = frameEncode(json, out, sizeof(out));

    TEST_ASSERT_GREATER_THAN(0u, len);
    // First \n separates json from crc
    const char* first_nl = strchr(out, '\n');
    TEST_ASSERT_NOT_NULL(first_nl);
    // Second \n terminates the frame
    const char* second_nl = strchr(first_nl + 1, '\n');
    TEST_ASSERT_NOT_NULL(second_nl);
}

void test_encode_buffer_too_small_returns_zero() {
    const char* json = "{\"type\":\"heartbeat\"}";
    char out[5]; // too small
    TEST_ASSERT_EQUAL_UINT(0u, frameEncode(json, out, sizeof(out)));
}

// ── frameValidate ────────────────────────────────────────────────────────────

void test_validate_accepts_correct_crc() {
    const char* json = "{\"type\":\"query\"}";
    char out[256];
    frameEncode(json, out, sizeof(out));

    char* nl1 = strchr(out, '\n'); *nl1 = '\0';
    char* nl2 = strchr(nl1 + 1, '\n'); *nl2 = '\0';
    const char* crcLine = nl1 + 1;

    TEST_ASSERT_TRUE(frameValidate(out, crcLine));
}

void test_validate_rejects_corrupted_json() {
    const char* json = "{\"type\":\"command\"}";
    char out[256];
    frameEncode(json, out, sizeof(out));

    char* nl1 = strchr(out, '\n'); *nl1 = '\0';
    char* nl2 = strchr(nl1 + 1, '\n'); *nl2 = '\0';
    const char* crcLine = nl1 + 1;

    char corrupted[256];
    strncpy(corrupted, out, sizeof(corrupted));
    corrupted[3] = 'X'; // flip one byte

    TEST_ASSERT_FALSE(frameValidate(corrupted, crcLine));
}

void test_validate_rejects_wrong_crc() {
    const char* json = "{\"type\":\"command\"}";
    TEST_ASSERT_FALSE(frameValidate(json, "00000000"));
}

void test_validate_rejects_malformed_crc_hex() {
    TEST_ASSERT_FALSE(frameValidate("{}", "ZZZZZZZZ"));
}

// ── FrameReader ───────────────────────────────────────────────────────────────

void test_reader_emits_message_after_full_frame() {
    const char* json = "{\"type\":\"heartbeat\"}";
    char frame[256];
    frameEncode(json, frame, sizeof(frame));

    FrameReader reader;
    for (const char* p = frame; *p; p++) {
        reader.feed(static_cast<uint8_t>(*p));
    }

    TEST_ASSERT_TRUE(reader.hasMessage());
    TEST_ASSERT_FALSE(reader.hasError());
    TEST_ASSERT_EQUAL_STRING(json, reader.message());
}

void test_reader_error_on_bad_crc() {
    const char* bad_frame = "{\"type\":\"heartbeat\"}\n00000000\n";

    FrameReader reader;
    for (const char* p = bad_frame; *p; p++) {
        reader.feed(static_cast<uint8_t>(*p));
    }

    TEST_ASSERT_FALSE(reader.hasMessage());
    TEST_ASSERT_TRUE(reader.hasError());
}

void test_reader_consume_clears_state() {
    const char* json = "{\"a\":1}";
    char frame[256];
    frameEncode(json, frame, sizeof(frame));

    FrameReader reader;
    for (const char* p = frame; *p; p++) reader.feed(*p);

    TEST_ASSERT_TRUE(reader.hasMessage());
    reader.consume();
    TEST_ASSERT_FALSE(reader.hasMessage());
    TEST_ASSERT_FALSE(reader.hasError());
}

void test_reader_handles_sequential_messages() {
    const char* json1 = "{\"seq\":1}";
    const char* json2 = "{\"seq\":2}";
    char f1[128], f2[128];
    frameEncode(json1, f1, sizeof(f1));
    frameEncode(json2, f2, sizeof(f2));

    FrameReader reader;

    for (const char* p = f1; *p; p++) reader.feed(*p);
    TEST_ASSERT_TRUE(reader.hasMessage());
    TEST_ASSERT_EQUAL_STRING(json1, reader.message());
    reader.consume();

    for (const char* p = f2; *p; p++) reader.feed(*p);
    TEST_ASSERT_TRUE(reader.hasMessage());
    TEST_ASSERT_EQUAL_STRING(json2, reader.message());
}

void test_reader_ignores_feed_while_message_pending() {
    const char* json = "{\"x\":1}";
    char frame[128];
    frameEncode(json, frame, sizeof(frame));

    FrameReader reader;
    for (const char* p = frame; *p; p++) reader.feed(*p);
    TEST_ASSERT_TRUE(reader.hasMessage());

    // Feed another frame without consuming — should be ignored
    const char* json2 = "{\"x\":2}";
    char frame2[128];
    frameEncode(json2, frame2, sizeof(frame2));
    for (const char* p = frame2; *p; p++) reader.feed(*p);

    // Still the first message
    TEST_ASSERT_EQUAL_STRING(json, reader.message());
}

// ── Runner ───────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_crc32_known_vector);
    RUN_TEST(test_crc32_empty_input);
    RUN_TEST(test_encode_produces_two_lines);
    RUN_TEST(test_encode_buffer_too_small_returns_zero);
    RUN_TEST(test_validate_accepts_correct_crc);
    RUN_TEST(test_validate_rejects_corrupted_json);
    RUN_TEST(test_validate_rejects_wrong_crc);
    RUN_TEST(test_validate_rejects_malformed_crc_hex);
    RUN_TEST(test_reader_emits_message_after_full_frame);
    RUN_TEST(test_reader_error_on_bad_crc);
    RUN_TEST(test_reader_consume_clears_state);
    RUN_TEST(test_reader_handles_sequential_messages);
    RUN_TEST(test_reader_ignores_feed_while_message_pending);
    return UNITY_END();
}
