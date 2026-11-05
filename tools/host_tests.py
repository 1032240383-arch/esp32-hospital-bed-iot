#!/usr/bin/env python3
"""
host_tests.py - Pure-logic unit tests for ESP32 Healthcare Monitor.
Exercises the exact same test cases as host_tests.cpp in Python.
"""

# --- 1. Medication clamp logic ---
def med_clamp(current: float, requested: float, max_step: float = 20.0, max_rate: float = 100.0) -> float:
    if requested < 0.0 or requested > max_rate:
        return -1.0  # REJECTED_OUT_OF_RANGE
    applied = requested
    if (requested - current) > max_step:
        applied = current + max_step
    return applied

def med_needs_confirm(applied: float, crit_threshold: float = 80.0) -> bool:
    return applied > crit_threshold

# --- 2. Ring buffer logic (capacity 30) ---
class RingBuf:
    CAPACITY = 30
    def __init__(self):
        self.buf = [None] * self.CAPACITY
        self.head = 0
        self.count = 0
        self.drops = 0

    def push(self, val):
        if self.count >= self.CAPACITY:
            self.drops += 1
        self.buf[self.head] = val
        self.head = (self.head + 1) % self.CAPACITY
        if self.count < self.CAPACITY:
            self.count += 1

    def peek_oldest(self):
        if self.count == 0:
            return None
        tail = ((self.head - self.count) + self.CAPACITY) % self.CAPACITY
        return self.buf[tail]

    def pop_oldest(self):
        if self.count == 0:
            return None
        val = self.peek_oldest()
        self.count -= 1
        return val

# --- 3. Sampling interval logic ---
def sampling_interval_ms(is_auto: bool, is_abnormal: bool, manual_ms: int,
                         auto_abnormal: int = 5000, auto_stable: int = 15000) -> int:
    if not is_auto:
        return manual_ms
    return auto_abnormal if is_abnormal else auto_stable

# --- 4. Alert priorities ---
ALERT_PRIORITIES = {
    "ALERT_NONE": -1,
    "ALERT_CHIRP": 0,
    "ALERT_WARN_VITALS": 1,
    "ALERT_DEGRADED_MQTT": 2,
    "ALERT_OFFLINE_WIFI": 3,
    "ALERT_CRIT_VITALS": 4,
    "ALERT_MED_CRIT": 5,
}

def alert_should_overwrite(incoming: str, current: str) -> bool:
    return ALERT_PRIORITIES[incoming] > ALERT_PRIORITIES[current]


def main():
    passed = 0
    failed = 0

    def expect(condition: bool, label: str):
        nonlocal passed, failed
        if condition:
            print(f"PASS: {label}")
            passed += 1
        else:
            print(f"FAIL: {label}")
            failed += 1

    print("=== ESP32 Healthcare Monitor - Host Logic Unit Tests ===\n")

    # Test 1: Normal clamp (30 -> 70 = 50)
    r1 = med_clamp(30.0, 70.0, 20.0, 100.0)
    expect(r1 == 50.0, "clamp: 30->70 (jump 40 > 20) clamped to 50")

    # Test 2: Boundary (jump == 20)
    r2 = med_clamp(30.0, 50.0, 20.0, 100.0)
    expect(r2 == 50.0, "clamp: 30->50 (jump 20 == 20) applied 50 (not clamped)")

    # Test 3: Needs confirmation (> 80)
    r3 = med_clamp(70.0, 90.0, 20.0, 100.0)
    expect(r3 == 90.0, "clamp: 70->90 applied 90")
    expect(med_needs_confirm(r3, 80.0), "needs confirm: 90 > 80")

    # Test 4: Out of range high
    r4 = med_clamp(0.0, 150.0, 20.0, 100.0)
    expect(r4 == -1.0, "reject: 150 > 100 max")

    # Test 5: Out of range negative
    r5 = med_clamp(50.0, -5.0, 20.0, 100.0)
    expect(r5 == -1.0, "reject: -5 < 0 min")

    # Test 6: Zero rate
    r6 = med_clamp(80.0, 0.0, 20.0, 100.0)
    expect(r6 == 0.0, "rate decrease: 80->0 applied 0")

    # Test 7: Ring buffer normal push/pop
    rb = RingBuf()
    for i in range(5):
        rb.push(i * 10)
    expect(rb.count == 5, "ring: 5 pushes -> count=5")
    expect(rb.drops == 0, "ring: 5 pushes -> drops=0")
    expect(rb.pop_oldest() == 0, "ring: oldest is 0")
    expect(rb.pop_oldest() == 10, "ring: second oldest is 10")
    expect(rb.count == 3, "ring: 2 pops -> count=3")

    # Test 8: Ring buffer overflow & drops
    rb2 = RingBuf()
    for i in range(31):
        rb2.push(i)
    expect(rb2.drops == 1, "ring: 31 pushes -> drops=1")
    expect(rb2.count == 30, "ring: 31 pushes -> count=30 (capacity capped)")
    expect(rb2.peek_oldest() == 1, "ring: oldest after overflow is 1 (0 dropped)")

    # Test 9: Uint16 drops counter simulation (300 pushes)
    rb3 = RingBuf()
    for i in range(300):
        rb3.push(i)
    expect(rb3.drops == 270, "ring: 300 pushes -> drops=270 (no 8-bit overflow)")

    # Test 10: Failed publish simulation (peek does not consume)
    rb4 = RingBuf()
    rb4.push(42)
    val = rb4.peek_oldest()
    expect(val == 42, "ring: peeked value is 42")
    expect(rb4.count == 1, "ring: peek does not decrement count")

    # Test 11: Sampling - auto abnormal
    expect(sampling_interval_ms(True, True, 10000) == 5000, "sampling: auto+abnormal = 5000ms")

    # Test 12: Sampling - auto stable
    expect(sampling_interval_ms(True, False, 10000) == 15000, "sampling: auto+stable = 15000ms")

    # Test 13: Sampling - manual override
    expect(sampling_interval_ms(False, True, 7000) == 7000, "sampling: manual+abnormal = 7000ms (manual wins)")

    # Test 14: Alert priority - NONE never overwrites
    expect(not alert_should_overwrite("ALERT_NONE", "ALERT_CHIRP"), "alert: NONE cannot overwrite CHIRP")
    expect(not alert_should_overwrite("ALERT_NONE", "ALERT_WARN_VITALS"), "alert: NONE cannot overwrite WARN")

    # Test 15: Alert priority hierarchy
    expect(alert_should_overwrite("ALERT_MED_CRIT", "ALERT_WARN_VITALS"), "alert: MED_CRIT overwrites WARN")
    expect(alert_should_overwrite("ALERT_OFFLINE_WIFI", "ALERT_CHIRP"), "alert: OFFLINE overwrites CHIRP")
    expect(not alert_should_overwrite("ALERT_WARN_VITALS", "ALERT_CRIT_VITALS"), "alert: WARN cannot overwrite CRIT")

    print(f"\n=== Summary: {passed} PASSED, {failed} FAILED ===")
    return 0 if failed == 0 else 1

if __name__ == "__main__":
    exit(main())

