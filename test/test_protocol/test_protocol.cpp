// Protocol integration tests — compiled for Arduino (ESP32-C3) only.
// In CI these verify compilation; run on hardware for full validation.
#ifdef ARDUINO

#include <Arduino.h>
#include <unity.h>
#include "ModularSerial.h"
#include <deque>
#include <vector>
#include <string>

using namespace MSC;

// ── MockStream ────────────────────────────────────────────────────────────────
// Bidirectional in-memory stream for testing without hardware.

class MockStream : public Stream {
public:
    void inject(const char* data) {
        for (const char* p = data; *p; p++) _in.push_back(static_cast<uint8_t>(*p));
    }
    void injectFrame(const char* json) {
        char frame[MSC_MAX_FRAME_SIZE + 16];
        frameEncode(json, frame, sizeof(frame));
        inject(frame);
    }
    std::string output() const { return std::string(_out.begin(), _out.end()); }
    void clearOutput() { _out.clear(); }

    int available() override { return static_cast<int>(_in.size()); }
    int read()      override { if (_in.empty()) return -1; uint8_t b = _in.front(); _in.pop_front(); return b; }
    int peek()      override { if (_in.empty()) return -1; return _in.front(); }
    size_t write(uint8_t b) override { _out.push_back(b); return 1; }
    size_t write(const uint8_t* buf, size_t n) override {
        for (size_t i = 0; i < n; i++) _out.push_back(buf[i]);
        return n;
    }
    // Print requires this
    using Print::write;

private:
    std::deque<uint8_t>  _in;
    std::vector<uint8_t> _out;
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool outputContains(const MockStream& s, const char* needle) {
    return s.output().find(needle) != std::string::npos;
}

// Run the mount's loop once, draining both mock streams
static void pumpMount(MountNode& mount, int iterations = 5) {
    for (int i = 0; i < iterations; i++) mount.loop();
}

// ── Fixtures ──────────────────────────────────────────────────────────────────

void setUp() {}
void tearDown() {}

// ── Tests: EffectorNode ───────────────────────────────────────────────────────

void test_effector_sends_advertisement_on_begin() {
    MockStream serial;
    EffectorNode node(serial, "test-effector", "1.0.0");
    node.begin();

    TEST_ASSERT_TRUE(outputContains(serial, "advertisement"));
    TEST_ASSERT_TRUE(outputContains(serial, "test-effector"));
}

void test_effector_responds_to_query() {
    MockStream serial;
    EffectorNode node(serial, "queried", "1.0.0");
    node.begin();
    serial.clearOutput();

    serial.injectFrame("{\"type\":\"query\"}");
    node.loop();

    TEST_ASSERT_TRUE(outputContains(serial, "advertisement"));
}

void test_effector_executes_command_and_responds() {
    MockStream serial;
    EffectorNode node(serial, "servo", "1.0.0");

    CommandDef cmd;
    strncpy(cmd.name, "move", sizeof(cmd.name) - 1);
    bool handlerCalled = false;

    node.addCommand(cmd, [&](JsonObjectConst, JsonObject) {
        handlerCalled = true;
        return true;
    });
    node.begin();
    serial.clearOutput();

    serial.injectFrame("{\"type\":\"command\",\"id\":\"001\",\"command\":\"move\",\"params\":{}}");
    node.loop();

    TEST_ASSERT_TRUE(handlerCalled);
    TEST_ASSERT_TRUE(outputContains(serial, "\"status\":\"ok\""));
    TEST_ASSERT_TRUE(outputContains(serial, "\"id\":\"001\""));
}

void test_effector_returns_error_for_unknown_command() {
    MockStream serial;
    EffectorNode node(serial, "servo", "1.0.0");
    node.begin();
    serial.clearOutput();

    serial.injectFrame("{\"type\":\"command\",\"id\":\"002\",\"command\":\"nonexistent\",\"params\":{}}");
    node.loop();

    TEST_ASSERT_TRUE(outputContains(serial, "\"status\":\"error\""));
}

void test_effector_drops_frame_with_bad_crc() {
    MockStream serial;
    EffectorNode node(serial, "servo", "1.0.0");
    bool handlerCalled = false;

    CommandDef cmd;
    strncpy(cmd.name, "move", sizeof(cmd.name) - 1);
    node.addCommand(cmd, [&](JsonObjectConst, JsonObject) { handlerCalled = true; return true; });
    node.begin();
    serial.clearOutput();

    // Inject a frame with wrong CRC
    serial.inject("{\"type\":\"command\",\"id\":\"003\",\"command\":\"move\",\"params\":{}}\n00000000\n");
    node.loop();

    TEST_ASSERT_FALSE(handlerCalled);
}

// ── Tests: MountNode ─────────────────────────────────────────────────────────

void test_mount_forwards_advertisement_to_controller() {
    MockStream controller, effector;
    MountNode mount(controller, effector);

    bool connected = false;
    Capabilities connectedCaps;
    mount.onEffectorConnected([&](const Capabilities& c) {
        connected     = true;
        connectedCaps = c;
    });
    mount.begin();
    controller.clearOutput();

    effector.injectFrame("{\"type\":\"advertisement\",\"device\":\"grabber\","
                          "\"version\":\"1.0.0\",\"commands\":{}}");
    pumpMount(mount);

    TEST_ASSERT_TRUE(connected);
    TEST_ASSERT_EQUAL_STRING("grabber", connectedCaps.deviceName);
    TEST_ASSERT_TRUE(outputContains(controller, "advertisement"));
}

void test_mount_acks_command_before_forwarding() {
    MockStream controller, effector;
    MountNode mount(controller, effector);
    mount.begin();

    // Simulate effector connected
    effector.injectFrame("{\"type\":\"advertisement\",\"device\":\"g\","
                          "\"version\":\"1\",\"commands\":{}}");
    pumpMount(mount);
    controller.clearOutput();

    controller.injectFrame("{\"type\":\"command\",\"id\":\"A1\","
                            "\"command\":\"open\",\"params\":{}}");
    pumpMount(mount);

    // ACK must appear in controller output
    TEST_ASSERT_TRUE(outputContains(controller, "\"type\":\"ack\""));
    TEST_ASSERT_TRUE(outputContains(controller, "\"id\":\"A1\""));

    // Command must have been forwarded to effector
    TEST_ASSERT_TRUE(outputContains(effector, "\"command\":\"open\""));
}

void test_mount_forwards_response_to_controller() {
    MockStream controller, effector;
    MountNode mount(controller, effector);
    mount.begin();

    effector.injectFrame("{\"type\":\"advertisement\",\"device\":\"g\","
                          "\"version\":\"1\",\"commands\":{}}");
    pumpMount(mount);
    controller.injectFrame("{\"type\":\"command\",\"id\":\"B2\","
                            "\"command\":\"close\",\"params\":{}}");
    pumpMount(mount);
    controller.clearOutput();

    effector.injectFrame("{\"type\":\"response\",\"id\":\"B2\",\"status\":\"ok\"}");
    pumpMount(mount);

    TEST_ASSERT_TRUE(outputContains(controller, "\"status\":\"ok\""));
    TEST_ASSERT_TRUE(outputContains(controller, "\"id\":\"B2\""));
}

void test_mount_detects_disconnect_on_heartbeat_timeout() {
    MockStream controller, effector;
    MountNode mount(controller, effector);
    mount.setHeartbeatTimeout(100); // 100 ms for test speed
    mount.begin();

    effector.injectFrame("{\"type\":\"advertisement\",\"device\":\"g\","
                          "\"version\":\"1\",\"commands\":{}}");
    pumpMount(mount);

    bool disconnected = false;
    mount.onEffectorDisconnected([&]() { disconnected = true; });

    // Don't send any heartbeat from effector; advance time via busy-wait
    delay(150);
    pumpMount(mount);

    TEST_ASSERT_TRUE(disconnected);
    TEST_ASSERT_TRUE(outputContains(controller, "\"event\":\"disconnect\""));
}

void test_mount_reconnects_after_new_advertisement() {
    MockStream controller, effector;
    MountNode mount(controller, effector);
    mount.setHeartbeatTimeout(100);
    mount.begin();

    int connectCount = 0;
    mount.onEffectorConnected([&](const Capabilities&) { connectCount++; });

    // First connection
    effector.injectFrame("{\"type\":\"advertisement\",\"device\":\"g\","
                          "\"version\":\"1\",\"commands\":{}}");
    pumpMount(mount);
    TEST_ASSERT_EQUAL_INT(1, connectCount);

    // Disconnect
    delay(150);
    pumpMount(mount);

    // Reconnect with new advertisement
    effector.injectFrame("{\"type\":\"advertisement\",\"device\":\"g\","
                          "\"version\":\"1\",\"commands\":{}}");
    pumpMount(mount);
    TEST_ASSERT_EQUAL_INT(2, connectCount);
}

// ── Runner ────────────────────────────────────────────────────────────────────

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_effector_sends_advertisement_on_begin);
    RUN_TEST(test_effector_responds_to_query);
    RUN_TEST(test_effector_executes_command_and_responds);
    RUN_TEST(test_effector_returns_error_for_unknown_command);
    RUN_TEST(test_effector_drops_frame_with_bad_crc);
    RUN_TEST(test_mount_forwards_advertisement_to_controller);
    RUN_TEST(test_mount_acks_command_before_forwarding);
    RUN_TEST(test_mount_forwards_response_to_controller);
    RUN_TEST(test_mount_detects_disconnect_on_heartbeat_timeout);
    RUN_TEST(test_mount_reconnects_after_new_advertisement);
    UNITY_END();
}

void loop() {}

#endif // ARDUINO
