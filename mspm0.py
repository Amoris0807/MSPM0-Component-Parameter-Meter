#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PYTHON = Path(sys.executable)
PROJECT = ROOT / "firmware" / "mspm0_autotest"
HEX = PROJECT / "build" / "mspm0_autotest.hex"


def run(*args: str) -> None:
    subprocess.run([str(PYTHON), *args], check=True)


def require_port(port: str | None, action: str) -> str:
    if not port:
        raise SystemExit(f"--port is required for {action}")
    return port


def main() -> None:
    parser = argparse.ArgumentParser(description="Tianmengxing MSPM0 automation")
    parser.add_argument(
        "action",
        choices=["build", "flash", "bsl-flash", "monitor", "console", "all"],
    )
    parser.add_argument("--port", help="Serial port, for example COM5 or /dev/ttyUSB0")
    args = parser.parse_args()

    if args.action in ("build", "all"):
        run(str(PROJECT / "build.py"))
    if args.action in ("flash", "all"):
        if not HEX.exists():
            run(str(PROJECT / "build.py"))
        run(str(ROOT / "tools" / "jlink_flash.py"), str(HEX))
    if args.action == "bsl-flash":
        port = require_port(args.port, args.action)
        if not HEX.exists():
            run(str(PROJECT / "build.py"))
        run(str(ROOT / "tools" / "bsl_flash.py"), "--port", port, str(HEX))
    if args.action in ("monitor", "all"):
        port = require_port(args.port, args.action)
        run(str(ROOT / "tools" / "serial_check.py"), "--port", port)
    if args.action == "console":
        port = require_port(args.port, args.action)
        run(str(ROOT / "tools" / "ad9833_console.py"), "--port", port)


if __name__ == "__main__":
    main()
