/**
 * host_tests.cpp  -  Pure-logic unit tests for ESP32 Healthcare Monitor.
 *
 * Tests firmware logic that can be exercised without FreeRTOS or hardware.
 * Covers: medication clamp, ring buffer overflow, sampling interval selection.
 *
 * Compile (Windows, requires g++):
 *   g++ -std=c++17 tools/host_tests.cpp -o tools/host_tests_bin.exe
 *   tools\host_tests_bin.exe
 *
 * Compile (Linux/macOS):
 *   g++ -std=c++17 tools/host_tests.cpp -o tools/host_tests_bin
 *   ./tools/host_tests_bin
 */

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <algorithm>
using namespace std;

// ─── Pure medication clamp logic ─────────────────────────────────────────────
// Returns -1.0f if rejected (out of range [0, maxRate])
// Returns applied rate after clamping step jump to maxStep
float medClamp(float current, float requested, float maxStep, float maxRate) {
    if (requested < 0.0f || requested > maxRate) return -1.0f;  // REJECT
    float applied = requested;
    if ((requested - current) > maxStep) applied = current + maxStep;
    return applied;
}

bool medNeedsConfirm(float applied, float critThreshold) {
    return applied > critThreshold;
}

// ─── Ring buffer (mimics OFFLINE_BUFFER_CAPACITY=30 ring) ────────────────────
static const int RING_CAP = 30;
struct RingBuf {
    int      buf[RING_CAP];
    int      head    = 0;
    int      count   = 0;
    uint16_t drops   = 0;

    void push(int val) {
        if (count >= RING_CAP) drops++;
        buf[head] = val;
        head = (head + 1) % RING_CAP;
        if (count < RING_CAP) count++;
    }

    // peek oldest without consuming
    bool peekOldest(int& out) const {
        if (count == 0) return false;
        int tail = ((head - count) + RING_CAP) % RING_CAP;
        out = buf[tail];
        return true;
    }

    // pop oldest (simulates successful publish)
    bool popOldest(int& out) {
        if (count == 0) return false;
        int tail = ((head - count) + RING_CAP) % RING_CAP;
        out = buf[tail];
        count--;
        return true;
    }
};

// ─── Sampling interval selection ─────────────────────────────────────────────
int samplingIntervalMs(bool isAuto, bool isAbnormal, int manualMs,
                       int autoAbnormalMs = 5000, int autoStableMs = 15000) {
    if (!isAuto) return manualMs;
    return isAbnormal ? autoAbnormalMs : autoStableMs;
}

// ─── Alert priority (mirrors firmware alertPriority()) ───────────────────────
enum AlertPattern { NONE=-1, CHIRP=0, WARN=1, CRIT=2, MED_CRIT=3, OFFLINE=4, DEGRADED=5 };
int alertPriority(AlertPattern p) { return (int)p; }

bool alertShouldOverwrite(AlertPattern incoming, AlertPattern current) {
    return alertPriority(incoming) > alertPriority(current);
}

// ─── Main test runner ─────────────────────────────────────────────────────────
int main() {
    int passed = 0, failed = 0;

#define EXPECT(cond, label) \
    do { \
        if (cond) { printf("PASS: %s\n", label); passed++; } \
        else       { printf("FAIL: %s  (line %d)\n", label, __LINE__); failed++; } \
    } while (0)

    printf("=== ESP32 Healthcare Monitor - Host Unit Tests ===\n\n");

    // ── Test 1: Normal clamp (jump > maxStep) ────────────────────────────────
    {
        float r = medClamp(30.0f, 70.0f, 20.0f, 100.0f);
        EXPECT(r == 50.0f, "clamp: 30->70 (jump 40 > 20) = 50");
    }

    // ── Test 2: Exact step boundary (jump == maxStep, not clamped) ───────────
    {
        float r = medClamp(30.0f, 50.0f, 20.0f, 100.0f);
        EXPECT(r == 50.0f, "clamp: 30->50 (jump 20 == maxStep) = 50 (not clamped)");
    }

    // ── Test 3: Needs confirmation (> crit threshold) ─────────────────────────
    {
        float r = medClamp(70.0f, 90.0f, 20.0f, 100.0f);
        EXPECT(r == 90.0f, "clamp: 70->90 = 90 (jump 20 == maxStep)");
        EXPECT(medNeedsConfirm(r, 80.0f), "medNeedsConfirm: 90 > 80 = true");
    }

    // ── Test 4: Out of range (> maxRate) ────────────────────────────────────
    {
        float r = medClamp(0.0f, 150.0f, 20.0f, 100.0f);
        EXPECT(r == -1.0f, "clamp: 150 > 100 -> REJECTED (-1)");
    }

    // ── Test 5: Out of range (negative) ─────────────────────────────────────
    {
        float r = medClamp(50.0f, -5.0f, 20.0f, 100.0f);
        EXPECT(r == -1.0f, "clamp: -5 < 0 -> REJECTED (-1)");
    }

    // ── Test 6: Zero rate (always OK) ────────────────────────────────────────
    {
        float r = medClamp(80.0f, 0.0f, 20.0f, 100.0f);
        EXPECT(r == 0.0f, "clamp: 80->0 = 0 (decrease, no clamp)");
    }

    // ── Test 7: Ring buffer normal push/pop ──────────────────────────────────
    {
        RingBuf rb;
        for (int i = 0; i < 5; i++) rb.push(i * 10);
        EXPECT(rb.count == 5, "ring: 5 pushes -> count=5");
        EXPECT(rb.drops == 0, "ring: 5 pushes -> drops=0");
        int out;
        rb.popOldest(out);
        EXPECT(out == 0, "ring: oldest is first pushed = 0");
        rb.popOldest(out);
        EXPECT(out == 10, "ring: second oldest = 10");
    }

    // ── Test 8: Ring buffer overflow (drops counter) ─────────────────────────
    {
        RingBuf rb;
        for (int i = 0; i < 31; i++) rb.push(i);
        EXPECT(rb.drops == 1,       "ring: 31 pushes -> drops=1");
        EXPECT(rb.count == RING_CAP, "ring: 31 pushes -> count=RING_CAP=30");
    }

    // ── Test 9: drops counter is uint16_t (no overflow at 256) ──────────────
    {
        RingBuf rb;
        for (int i = 0; i < 300; i++) rb.push(i);
        EXPECT(rb.drops == 300 - RING_CAP, "ring: 300 pushes -> drops=270 (uint16 no overflow)");
    }

    // ── Test 10: Failed publish keeps record (count unchanged) ───────────────
    {
        RingBuf rb;
        rb.push(42);
        int out;
        bool got = rb.peekOldest(out);
        EXPECT(got,          "ring: peek on 1-element buffer = true");
        EXPECT(out == 42,    "ring: peeked value = 42");
        EXPECT(rb.count == 1, "ring: peek does not consume (count stays 1)");
    }

    // ── Test 11: Sampling interval - auto abnormal ────────────────────────────
    {
        int ms = samplingIntervalMs(true, true, 10000);
        EXPECT(ms == 5000, "sampling: auto+abnormal -> 5000ms");
    }

    // ── Test 12: Sampling interval - auto stable ─────────────────────────────
    {
        int ms = samplingIntervalMs(true, false, 10000);
        EXPECT(ms == 15000, "sampling: auto+stable -> 15000ms");
    }

    // ── Test 13: Sampling interval - manual override ─────────────────────────
    {
        int ms = samplingIntervalMs(false, true, 7000);
        EXPECT(ms == 7000, "sampling: manual+abnormal -> 7000ms (manual wins)");
    }

    // ── Test 14: Alert priority - NONE never overwrites ──────────────────────
    {
        EXPECT(!alertShouldOverwrite(NONE, CHIRP),    "alert: NONE never overwrites CHIRP");
        EXPECT(!alertShouldOverwrite(NONE, WARN),     "alert: NONE never overwrites WARN");
        EXPECT(!alertShouldOverwrite(NONE, OFFLINE),  "alert: NONE never overwrites OFFLINE");
    }

    // ── Test 15: Alert priority ordering ─────────────────────────────────────
    {
        EXPECT( alertShouldOverwrite(MED_CRIT, WARN),     "alert: MED_CRIT overwrites WARN");
        EXPECT( alertShouldOverwrite(OFFLINE, CHIRP),     "alert: OFFLINE overwrites CHIRP");
        EXPECT(!alertShouldOverwrite(WARN, CRIT),         "alert: WARN cannot overwrite CRIT");
        EXPECT(!alertShouldOverwrite(CHIRP, DEGRADED),    "alert: CHIRP cannot overwrite DEGRADED");
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}

