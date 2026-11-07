#!/usr/bin/env python3
"""Compile real startup/event functions with host scheduler/display stubs.

Run: python3 firmware/frame_film_ark/tests/host_boot_startup/run.py
Requires a C11 compiler (CC, default cc); no ESP-IDF or device is needed.
Only startup/event routing is exercised; EPD hardware and the switch body are stubbed.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[1] / "components/film_app/core/app_manager.c"
FUNCTIONS = (
    "app_ensure_running", "app_handle_event", "app_manager_init",
    "app_manager_boot_show", "app_manager_boot_end",
)


def extract_function(source, name):
    match = re.search(r"(?:static )?(?:void|int) " + name + r"\([^;]*?\)\s*\{", source)
    if match is None:
        raise RuntimeError(f"Production function missing: {name}")
    index, depth = match.end(), 1
    while depth:
        depth += (source[index] == "{") - (source[index] == "}")
        index += 1
    return source[match.start():index]


def main():
    source = SOURCE.read_text(encoding="utf-8")
    program = '#include "stubs.h"\n' + "\n".join(
        extract_function(source, name) for name in FUNCTIONS
    ) + '\n#include "test_boot_startup.c"\n'
    with tempfile.TemporaryDirectory(prefix="framefilm-ark-startup-") as directory:
        temporary = Path(directory)
        harness, binary = temporary / "startup.c", temporary / "startup"
        harness.write_text(program, encoding="utf-8")
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
            "-Wno-unused-function", "-I", str(HERE), str(harness), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
