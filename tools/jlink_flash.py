#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def find_jlink() -> Path:
    configured = os.environ.get("JLINK_EXE")
    path_tool = shutil.which("JLink.exe") or shutil.which("JLink")
    candidates = [
        Path(configured).expanduser() if configured else None,
        Path(path_tool) if path_tool else None,
    ]
    segger_root = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "SEGGER"
    if segger_root.is_dir():
        candidates.extend(sorted(segger_root.glob("JLink*/JLink.exe"), reverse=True))
    for candidate in candidates:
        if candidate and candidate.is_file():
            return candidate.resolve()
    raise SystemExit(
        "J-Link Commander not found. Add it to PATH or set JLINK_EXE."
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Flash MSPM0G3507 with J-Link")
    parser.add_argument("firmware", type=Path)
    parser.add_argument("--speed", type=int, default=4000)
    args = parser.parse_args()

    firmware = args.firmware.resolve()
    if not firmware.is_file():
        raise SystemExit(f"Firmware not found: {firmware}")

    script = "\n".join(
        ["r", f'loadfile "{firmware}"', "r", "g", "exit", ""]
    )
    script_path = Path(tempfile.gettempdir()) / "mspm0_autotest_flash.jlink"
    script_path.write_text(script, encoding="ascii")
    command = [
        str(find_jlink()),
        "-device",
        "MSPM0G3507",
        "-if",
        "SWD",
        "-speed",
        str(args.speed),
        "-autoconnect",
        "1",
        "-CommanderScript",
        str(script_path),
    ]
    print("+", subprocess.list2cmdline(command))
    subprocess.run(command, check=True)
    print("J-Link flash complete")


if __name__ == "__main__":
    main()
