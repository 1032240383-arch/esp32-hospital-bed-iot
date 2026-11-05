#!/usr/bin/env python3
"""
set_prefix.py - Synchronize MQTT_TOPIC_PREFIX across all project files.

Usage:
    python tools/set_prefix.py <unique_string>

    unique_string: 4-24 chars, only letters/digits/underscore/dash.
    Example:  python tools/set_prefix.py intern_2024_xyz

The script updates the following files:
    - src/config.h          (#define MQTT_TOPIC_PREFIX)
    - dashboard/index.html  (const TOPIC_PREFIX = ...)
    - tools/mqtt_test.py    (TOPIC_PREFIX = ...)
    - README.md             (all topic examples mentioning the prefix)

IMPORTANT: Never use a real student ID or personal data in the prefix.
           Topics are visible on the public HiveMQ broker.
"""

import sys
import re
import pathlib


def main():
    if len(sys.argv) != 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        sys.exit(0 if sys.argv[1:] else 1)

    uid = sys.argv[1]
    if not re.match(r'^[A-Za-z0-9_-]{4,24}$', uid):
        print(f"ERROR: '{uid}' is invalid.")
        print("       Use 4-24 chars: letters, digits, underscore (-), dash (-).")
        sys.exit(1)

    prefix = f"hospital_{uid}/ward_A/bed_01"
    root = pathlib.Path(__file__).resolve().parent.parent

    changes = 0

    # 1. src/config.h
    cfg = root / "src" / "config.h"
    if cfg.exists():
        txt = cfg.read_text(encoding="utf-8")
        new = re.sub(
            r'(#define\s+MQTT_TOPIC_PREFIX\s+)"[^"]*"',
            lambda m: f'{m.group(1)}"{prefix}"',
            txt
        )
        if new != txt:
            cfg.write_text(new, encoding="utf-8")
            print(f"  Updated: {cfg}")
            changes += 1
        else:
            print(f"  No change needed: {cfg}")
    else:
        print(f"  WARNING: not found: {cfg}")

    # 2. dashboard/index.html
    dash = root / "dashboard" / "index.html"
    if dash.exists():
        txt = dash.read_text(encoding="utf-8")
        # Match:  const TOPIC_PREFIX = 'hospital/ward_A/bed_01'
        #    or:  const TOPIC_PREFIX = "hospital/ward_A/bed_01"
        new = re.sub(
            r"(const\s+TOPIC_PREFIX\s*=\s*)['\"][^'\"]*['\"]",
            lambda m: f"{m.group(1)}'{prefix}'",
            txt
        )
        if new != txt:
            dash.write_text(new, encoding="utf-8")
            print(f"  Updated: {dash}")
            changes += 1
        else:
            print(f"  No change needed: {dash}")
    else:
        print(f"  WARNING: not found: {dash}")

    # 3. tools/mqtt_test.py
    testpy = root / "tools" / "mqtt_test.py"
    if testpy.exists():
        txt = testpy.read_text(encoding="utf-8")
        new = re.sub(
            r'(TOPIC_PREFIX\s*=\s*)["\'][^"\']*["\']',
            lambda m: f'{m.group(1)}"{prefix}"',
            txt
        )
        if new != txt:
            testpy.write_text(new, encoding="utf-8")
            print(f"  Updated: {testpy}")
            changes += 1
        else:
            print(f"  No change needed: {testpy}")
    else:
        print(f"  WARNING: not found: {testpy}")

    # 4. README.md - update any topic path that looks like the old prefix pattern
    readme = root / "README.md"
    if readme.exists():
        txt = readme.read_text(encoding="utf-8")
        new = re.sub(
            r'hospital(?:_[A-Za-z0-9_-]+)?/ward_A/bed_01',
            prefix,
            txt
        )
        if new != txt:
            readme.write_text(new, encoding="utf-8")
            print(f"  Updated: {readme}")
            changes += 1
        else:
            print(f"  No change needed: {readme}")
    else:
        print(f"  WARNING: not found: {readme}")

    print()
    if changes > 0:
        print(f"SUCCESS: {changes} file(s) updated with prefix: {prefix}")
    else:
        print(f"All files already use prefix: {prefix}")
    print()
    print("Next steps:")
    print("  1. Verify: grep -r 'MQTT_TOPIC_PREFIX' src/config.h")
    print("  2. Rebuild: pio run")
    print("  3. Update your dashboard URL with ?prefix=" + prefix)


if __name__ == "__main__":
    main()

