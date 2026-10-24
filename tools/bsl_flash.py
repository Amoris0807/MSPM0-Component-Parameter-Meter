#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
import time
from pathlib import Path

import serial

PACKET_HEADER = 0x80
DEFAULT_PASSWORD = bytes([0xFF] * 32)
MAX_CHUNK = 128

CMD_CONNECTION = 0x12
CMD_RX_PASSWORD = 0x21
CMD_MASS_ERASE = 0x15
CMD_PROGRAM_DATA = 0x20
CMD_START_APP = 0x40


def crc32_mspm0(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return crc & 0xFFFFFFFF


def packet(command: int, payload: bytes = b"") -> bytes:
    core = bytes([command]) + payload
    return (
        bytes([PACKET_HEADER])
        + struct.pack("<H", len(core))
        + core
        + struct.pack("<I", crc32_mspm0(core))
    )


def read_exact(port: serial.Serial, count: int, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    data = bytearray()
    while len(data) < count and time.monotonic() < deadline:
        block = port.read(count - len(data))
        if block:
            data.extend(block)
    if len(data) != count:
        raise TimeoutError(
            f"Serial response timed out: expected {count} bytes, got {len(data)}"
        )
    return bytes(data)


def send_ack_command(
    port: serial.Serial,
    command: int,
    payload: bytes = b"",
    response_data_len: int | None = None,
    timeout: float = 5.0,
) -> bytes:
    port.reset_input_buffer()
    port.write(packet(command, payload))
    port.flush()
    ack = read_exact(port, 1, timeout)[0]
    if ack != 0x00:
        raise RuntimeError(f"Command 0x{command:02X} returned ACK=0x{ack:02X}")
    if response_data_len is None:
        return b""
    response = read_exact(port, 4 + response_data_len + 4, timeout)
    if response[0] != 0x08:
        raise RuntimeError(f"Unexpected response header: 0x{response[0]:02X}")
    return response[4 : 4 + response_data_len]


def connect(port: serial.Serial) -> None:
    print("Waiting for the MSPM0 UART BSL...")
    last_error: Exception | None = None
    for _ in range(900):
        try:
            send_ack_command(port, CMD_CONNECTION, timeout=0.35)
            print("BSL connection established")
            return
        except Exception as exc:
            last_error = exc
            time.sleep(0.2)
    raise RuntimeError(f"MSPM0 did not enter UART BSL: {last_error}")


def load_segments(path: Path) -> list[tuple[int, bytes]]:
    try:
        from intelhex import IntelHex
    except ImportError as exc:
        raise SystemExit(
            "intelhex is required for UART BSL flashing. "
            "Install dependencies with: python -m pip install -r requirements.txt"
        ) from exc

    image = IntelHex(str(path))
    segments: list[tuple[int, bytes]] = []
    for start, end in image.segments():
        segments.append((start, bytes(image.tobinarray(start=start, end=end - 1))))
    return segments


def flash(com_port: str, firmware: Path, start_app: bool) -> None:
    segments = load_segments(firmware)
    total = sum(len(data) for _, data in segments)
    print(f"Firmware: {firmware}")
    print(f"Port: {com_port}; payload: {total} bytes")

    with serial.Serial(
        com_port,
        baudrate=9600,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.1,
        write_timeout=3,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    ) as port:
        connect(port)

        port.reset_input_buffer()
        port.write(b"\xBB")
        port.flush()
        status = read_exact(port, 1, 3.0)[0]
        if status != 0x51:
            raise RuntimeError(f"Unexpected BSL status: 0x{status:02X}")
        print("BSL status response is valid (0x51)")

        result = send_ack_command(
            port, CMD_RX_PASSWORD, DEFAULT_PASSWORD, response_data_len=1
        )
        if result != b"\x00":
            raise RuntimeError(f"Default BSL password rejected: {result.hex()}")
        print("BSL unlocked")

        result = send_ack_command(
            port, CMD_MASS_ERASE, response_data_len=1, timeout=30
        )
        if result != b"\x00":
            raise RuntimeError(f"Mass erase failed: {result.hex()}")
        print("Mass erase complete")

        written = 0
        for address, data in segments:
            for offset in range(0, len(data), MAX_CHUNK):
                chunk = data[offset : offset + MAX_CHUNK]
                payload = struct.pack("<I", address + offset) + chunk
                result = send_ack_command(
                    port, CMD_PROGRAM_DATA, payload, response_data_len=1
                )
                if result != b"\x00":
                    raise RuntimeError(
                        f"Write failed @ 0x{address + offset:08X}: {result.hex()}"
                    )
                written += len(chunk)
                print(f"\rWrote {written}/{total} bytes", end="", flush=True)
                time.sleep(0.06)
        print("\nFirmware write complete")

        if start_app:
            port.reset_input_buffer()
            port.write(packet(CMD_START_APP))
            port.flush()
            print("Start-application command sent")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Experimental MSPM0 UART BSL command-line flasher"
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("firmware", type=Path)
    parser.add_argument("--no-start", action="store_true")
    args = parser.parse_args()
    flash(args.port, args.firmware.resolve(), not args.no_start)


if __name__ == "__main__":
    main()
