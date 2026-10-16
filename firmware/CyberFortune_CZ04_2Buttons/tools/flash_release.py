"""Serial upgrade with backup; select 4/16 MB from the real flash ID."""
import argparse
import datetime
import os
from pathlib import Path
import re
import struct
import subprocess


def validate_upgrade_layout(image):
    expected = {
        "nvs": (1, 2, 0x9000, 0x5000),
        "otadata": (1, 0, 0xE000, 0x2000),
        "app0": (0, 0x10, 0x10000, 0x1A0000),
        "app1": (0, 0x11, 0x1B0000, 0x1A0000),
    }
    entries = {}
    for offset in range(0x8000, 0x8C00, 32):
        magic, kind, subtype, address, size, label, _ = struct.unpack_from("<HBBII16sI", image, offset)
        if magic != 0x50AA:
            break
        entries[label.rstrip(b"\0").decode("ascii")] = (kind, subtype, address, size)
    for name, value in expected.items():
        if entries.get(name) != value:
            raise ValueError("Not the supported FortunePager layout; refusing to overwrite it")
    if entries.get("spiffs") not in ((1, 0x82, 0x350000, 0xA0000), (1, 0x82, 0x350000, 0xCA0000)):
        raise ValueError("Unsupported content partition; nothing was written")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, help="e.g. COM5; enter BOOT mode manually first")
    parser.add_argument("--fresh", action="store_true", help="replace filesystem too; ERASES content in the image range")
    parser.add_argument("--esptool", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    esptool = args.esptool
    if esptool is None:
        candidates = list((Path(os.environ["LOCALAPPDATA"]) / "Arduino15/packages/esp32/tools/esptool_py").glob("*/esptool.exe"))
        if not candidates:
            parser.error("esptool not found; install ESP32 Arduino core or pass --esptool")
        esptool = max(candidates, key=lambda p: p.stat().st_mtime)
    base = [str(esptool), "--chip", "esp32", "--port", args.port,
            "--baud", "115200", "--before", "no-reset", "--after", "no-reset"]
    info = subprocess.run(base + ["flash-id"], check=True, capture_output=True, text=True).stdout
    print(info)
    detected = re.search(r"Detected flash size:\s*(4|16)\s*MB\b", info, re.I)
    if not detected:
        parser.error("Only verified 4MB and 16MB ESP32 are supported; nothing was written")
    mb = int(detected.group(1))
    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    backup = root / f"backup_{args.port}_{mb}MB_{timestamp}.bin"
    print(f"Backing up entire flash to {backup}", flush=True)
    subprocess.run(base + ["read-flash", "0", str(mb * 1024 * 1024), str(backup)], check=True)
    if backup.stat().st_size != mb * 1024 * 1024:
        parser.error("Incomplete backup; refusing to write")
    if not args.fresh:
        try:
            validate_upgrade_layout(backup.read_bytes())
        except ValueError as error:
            parser.error(str(error))
    prefix = "FortunePager_v2.8.0"
    if args.fresh:
        images = ["0x0", str(root / f"{prefix}_{mb}MB_full.bin")]
    else:
        # Keep NVS at 0x9000 and LittleFS at 0x350000 completely untouched.
        # Reset otadata to boot app0, so an old active app1 cannot override this update.
        images = ["0x1000", str(root / f"bootloader_{mb}MB.bin"),
                  "0x8000", str(root / f"partitions_{mb}MB.bin"),
                  "0xe000", str(root / "boot_app0.bin"),
                  "0x10000", str(root / f"{prefix}_ota.bin")]
    subprocess.run(base + ["write-flash", "--flash-mode", "dio", "--flash-freq", "40m",
                           "--flash-size", f"{mb}MB"] + images, check=True)
    print("Done. Release BOOT and reset/power-cycle the device. Do not restore a 4MB partition table after expanding.")


if __name__ == "__main__":
    main()
