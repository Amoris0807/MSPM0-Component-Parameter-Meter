#!/usr/bin/env python3
from __future__ import annotations

import argparse
import time

import serial


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--baud", type=int, default=9600)
    parser.add_argument("--seconds", type=float, default=8)
    args = parser.parse_args()

    deadline = time.monotonic() + args.seconds
    with serial.Serial(args.port, args.baud, timeout=0.2) as port:
        while time.monotonic() < deadline:
            data = port.readline()
            if data:
                print(data.decode("utf-8", errors="replace").rstrip())


if __name__ == "__main__":
    main()
