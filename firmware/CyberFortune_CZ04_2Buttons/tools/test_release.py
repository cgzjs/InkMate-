"""Validate partition layouts and serial migration safeguards without a device."""
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / (name + ".py"))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


build = module("build_release")
flash = module("flash_release")


def mock_image(large=False, incompatible=False):
    image = bytearray(b"\xff" * 0x9000)
    records = [
        ("nvs", 1, 2, 0x9000, 0x5000),
        ("otadata", 1, 0, 0xE000, 0x2000),
        ("app0", 0, 0x10, 0x10000, 0x1A0000),
        ("app1", 0, 0x11, 0x1B0000, 0x1A0000),
        ("spiffs", 1, 0x82, 0x350000, 0xCA0000 if large else 0xA0000),
    ]
    if incompatible:
        records[0] = ("nvs", 1, 2, 0x9000, 0x6000)
    for i, (name, kind, subtype, offset, size) in enumerate(records):
        struct.pack_into("<HBBII16sI", image, 0x8000 + i * 32,
                         0x50AA, kind, subtype, offset, size, name.encode(), 0)
    return image


class ReleaseTests(unittest.TestCase):
    def test_partitions(self):
        small = build.partitions(ROOT / "partitions.csv", 4 * 1024 * 1024)
        large = build.partitions(ROOT / "partitions_16mb.csv", 16 * 1024 * 1024)
        self.assertEqual(small["spiffs"]["size"], 640 * 1024)
        self.assertEqual(large["spiffs"]["size"], 13238272)
        for name in ("nvs", "otadata", "app0", "app1"):
            self.assertEqual(small[name], large[name])
        with self.assertRaises(AssertionError):
            build.partitions(ROOT / "partitions_16mb.csv", 4 * 1024 * 1024)

    def test_upgrade_layout(self):
        flash.validate_upgrade_layout(mock_image())
        flash.validate_upgrade_layout(mock_image(large=True))
        with self.assertRaises(ValueError):
            flash.validate_upgrade_layout(mock_image(incompatible=True))

    def test_unsupported_flash_never_writes(self):
        class Completed:
            stdout = "Detected flash size: 8MB\n"
        with patch.object(flash.subprocess, "run", return_value=Completed()) as run:
            with patch("sys.argv", ["flash_release.py", "--port", "COM99", "--esptool", "dummy.exe"]):
                with self.assertRaises(SystemExit):
                    flash.main()
            self.assertEqual(run.call_count, 1)
            self.assertIn("flash-id", run.call_args.args[0])


if __name__ == "__main__":
    unittest.main()
