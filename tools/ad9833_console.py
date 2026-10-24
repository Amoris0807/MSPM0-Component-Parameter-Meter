#!/usr/bin/env python3
from __future__ import annotations

import argparse
import threading
import time

import serial


def reader(port: serial.Serial, stop: threading.Event) -> None:
    while not stop.is_set():
        data = port.readline()
        if data:
            print(data.decode("utf-8", errors="replace").rstrip())


def main() -> None:
    parser = argparse.ArgumentParser(description="AD9833 interactive serial console")
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--baud", type=int, default=9600)
    args = parser.parse_args()

    stop = threading.Event()
    with serial.Serial(args.port, args.baud, timeout=0.2) as port:
        port.reset_input_buffer()
        thread = threading.Thread(target=reader, args=(port, stop), daemon=True)
        thread.start()

        print(f"Connected to {args.port} at {args.baud} baud.")
        print("Enter F1000, F10000, F50000 or F100000. Enter Q to quit.")

        try:
            while True:
                command = input("AD9833> ").strip()
                if command.upper() in {"Q", "QUIT", "EXIT"}:
                    break
                if not command:
                    continue
                port.write((command + "\r\n").encode("ascii", errors="ignore"))
                port.flush()
                time.sleep(0.1)
        except (EOFError, KeyboardInterrupt):
            pass
        finally:
            stop.set()
            thread.join(timeout=1.0)


if __name__ == "__main__":
    main()
