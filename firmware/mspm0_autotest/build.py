#!/usr/bin/env python3
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
STAGE = Path(tempfile.gettempdir()) / "mspm0_autotest_build"
BUILD = ROOT / "build"


def tool(name: str) -> str:
    path = shutil.which(name)
    if not path:
        raise SystemExit(
            f"Required tool not found: {name}. Add Arm GNU Toolchain to PATH."
        )
    return path


def run(args: list[str]) -> None:
    print("+", subprocess.list2cmdline(args))
    subprocess.run(args, check=True)


def find_sdk() -> Path:
    configured = os.environ.get("MSPM0_SDK_ROOT")
    candidates = [
        Path(configured).expanduser() if configured else None,
        Path.home() / "ti" / "mspm0_sdk_2_11_00_07",
        Path.home() / "ti" / "mspm0_sdk",
        Path.home() / ".codex" / "toolchains" / "mspm0-sdk-main",
    ]
    driverlib_path = Path(
        "source/ti/driverlib/lib/gcc/m0p/"
        "mspm0g1x0x_g3x0x/driverlib.a"
    )
    for candidate in candidates:
        if candidate and (candidate / driverlib_path).is_file():
            return candidate.resolve()
    raise SystemExit(
        "MSPM0 SDK not found. Set MSPM0_SDK_ROOT to the SDK installation root."
    )


def main() -> None:
    gcc = tool("arm-none-eabi-gcc")
    objcopy = tool("arm-none-eabi-objcopy")
    size = tool("arm-none-eabi-size")
    sdk = find_sdk()

    BUILD.mkdir(parents=True, exist_ok=True)
    STAGE.mkdir(parents=True, exist_ok=True)

    # Some Windows GCC builds cannot reliably consume non-ASCII source paths.
    source_names = [
        "main.c",
        "ad9833.c",
        "ad9833.h",
        "adc_monitor.c",
        "adc_monitor.h",
        "range_control.c",
        "range_control.h",
        "ti_msp_dl_config.c",
        "ti_msp_dl_config.h",
        "startup_mspm0g350x_gcc.c",
        "mspm0g3507.lds",
    ]
    for name in source_names:
        shutil.copy2(ROOT / name, STAGE / name)

    common = [
        "-D__MSPM0G3507__",
        "-mcpu=cortex-m0plus",
        "-march=armv6-m",
        "-mthumb",
        "-mfloat-abi=soft",
        "-std=c99",
        "-O2",
        "-g",
        "-Wall",
        "-ffunction-sections",
        "-fdata-sections",
        f"-I{STAGE}",
        f"-I{sdk / 'source'}",
        f"-I{sdk / 'source/third_party/CMSIS/Core/Include'}",
    ]

    sources = [
        STAGE / "main.c",
        STAGE / "ad9833.c",
        STAGE / "adc_monitor.c",
        STAGE / "range_control.c",
        STAGE / "ti_msp_dl_config.c",
        STAGE / "startup_mspm0g350x_gcc.c",
    ]
    objects: list[Path] = []
    for source in sources:
        obj = STAGE / f"{source.stem}.o"
        run([gcc, *common, "-c", str(source), "-o", str(obj)])
        objects.append(obj)

    elf = STAGE / "mspm0_autotest.elf"
    driverlib = (
        sdk / "source/ti/driverlib/lib/gcc/m0p/"
        "mspm0g1x0x_g3x0x/driverlib.a"
    )
    link = [
        gcc,
        *map(str, objects),
        str(driverlib),
        "-nostartfiles",
        "-mcpu=cortex-m0plus",
        "-march=armv6-m",
        "-mthumb",
        "-mfloat-abi=soft",
        f"-Wl,-T,{STAGE / 'mspm0g3507.lds'}",
        f"-Wl,-Map,{STAGE / 'mspm0_autotest.map'}",
        "-Wl,--gc-sections",
        "--specs=nano.specs",
        "--specs=nosys.specs",
        "-static",
        "-lgcc",
        "-lc",
        "-lm",
        "-o",
        str(elf),
    ]
    run(link)
    staged_hex = STAGE / "mspm0_autotest.hex"
    run([objcopy, "-O", "ihex", str(elf), str(staged_hex)])
    run([size, str(elf)])
    for name in ("mspm0_autotest.elf", "mspm0_autotest.hex", "mspm0_autotest.map"):
        shutil.copy2(STAGE / name, BUILD / name)
    print(f"\nBuild complete: {BUILD / 'mspm0_autotest.hex'}")


if __name__ == "__main__":
    main()
