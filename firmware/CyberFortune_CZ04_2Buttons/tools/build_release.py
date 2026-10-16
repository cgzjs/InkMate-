"""Build one 4/16 MB-compatible app and capacity-specific serial images."""
import argparse
import csv
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import zipfile

SKETCH = Path(__file__).resolve().parents[1]
VERSION = "2.8.0"


def run(*args, cwd=None):
    print(" ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, cwd=cwd)


def arduino_tools():
    local = Path(os.environ["LOCALAPPDATA"])
    packages = local / "Arduino15/packages/esp32"
    core = max((packages / "hardware/esp32").iterdir(), key=lambda p: p.stat().st_mtime)
    cli = local / "Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
    if not cli.exists():
        cli = Path(shutil.which("arduino-cli") or "C:/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe")
    esptool = max((packages / "tools/esptool_py").glob("*/esptool.exe"), key=lambda p: p.stat().st_mtime)
    littlefs = max((packages / "tools/mklittlefs").glob("*/mklittlefs.exe"), key=lambda p: p.stat().st_mtime)
    return cli, core, esptool, littlefs


def partitions(path, capacity):
    entries = []
    with path.open(encoding="utf-8") as f:
        for row in csv.reader(line for line in f if not line.lstrip().startswith("#")):
            if not row:
                continue
            row = [v.strip() for v in row]
            offset, size = int(row[3], 0), int(row[4], 0)
            assert offset % 4096 == 0 and size % 4096 == 0
            assert offset + size <= capacity
            if entries:
                assert offset >= entries[-1]["offset"] + entries[-1]["size"]
            entries.append(dict(name=row[0], offset=offset, size=size))
    return {item["name"]: item for item in entries}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-compile", action="store_true")
    args = parser.parse_args()
    cli, core, esptool, littlefs = arduino_tools()
    build = SKETCH / "build/v2.8.0"
    release = SKETCH / "releases" / f"v{VERSION}"
    release.mkdir(parents=True, exist_ok=True)
    if not args.skip_compile:
        run(cli, "compile", "--fqbn", "esp32:esp32:esp32:FlashSize=4M",
            "--build-property", "upload.maximum_size=1703936", "--build-path", build, SKETCH)
    project = SKETCH.name + ".ino"
    app = build / (project + ".bin")
    generated = (build / "sketch" / (project + ".cpp")).read_text(encoding="utf-8")
    current = (SKETCH / (SKETCH.name + ".ino")).read_text(encoding="utf-8")
    for name in ("loadVocabIndex", "readVocabWord", "handleWebRoot", "handleContentUpload",
                 "handleContentUploadFinished", "handleWebStatus", "nextBookPage", "previousBookPage", "setup"):
        start = re.search(r"^\w[^\n]*\b" + name + r"\(", current, re.M).start()
        begin = current.index("{", start)
        end = current.index("\n}", begin) + 2
        assert current[begin:end] in generated, f"Recompile the latest {name} implementation first"
    assert "repeat(2,minmax(0,1fr))" in generated, "Recompile the latest web layout first"
    assert "LittleFS.begin(false)" in generated, "Recompile the safe-mount change first"
    assert app.stat().st_size <= 0x1A0000
    ota = release / f"FortunePager_v{VERSION}_ota.bin"
    shutil.copy2(app, ota)
    shutil.copy2(core / "tools/partitions/boot_app0.bin", release / "boot_app0.bin")
    report = {"version": VERSION, "appBytes": app.stat().st_size, "profiles": {}}
    for mb in (4, 16):
        capacity = mb * 1024 * 1024
        csv_path = SKETCH / ("partitions.csv" if mb == 4 else "partitions_16mb.csv")
        layout = partitions(csv_path, capacity)
        assert layout["app0"]["offset"] == 0x10000
        assert layout["app1"]["offset"] == 0x1B0000
        assert layout["spiffs"]["offset"] == 0x350000
        fs = layout["spiffs"]
        partition_bin = release / f"partitions_{mb}MB.bin"
        run(core / "tools/gen_esp32part.exe", "-q", csv_path, partition_bin)
        fs_image = build / f"littlefs_{mb}MB.bin"
        run(littlefs, "-c", SKETCH / "data", "-b", "4096", "-p", "256", "-s", fs["size"], fs_image)
        unpacked = build / f"fs_roundtrip_{mb}MB"
        unpacked.mkdir(exist_ok=True)
        run(littlefs, "-u", unpacked.name, "-b", "4096", "-p", "256", "-s", fs["size"], fs_image.name, cwd=build)
        for packed in (SKETCH / "data").iterdir():
            if packed.is_file():
                assert (unpacked / packed.name).read_bytes() == packed.read_bytes()
        full = release / f"FortunePager_v{VERSION}_{mb}MB_full.bin"
        run(esptool, "--chip", "esp32", "merge-bin", "--flash-mode", "dio",
            "--flash-freq", "40m", "--flash-size", f"{mb}MB", "--pad-to-size", f"{mb}MB",
            "-o", full, "0x1000", build / (project + ".bootloader.bin"),
            "0x8000", partition_bin, "0xe000", release / "boot_app0.bin",
            "0x10000", ota, hex(fs["offset"]), fs_image)
        image = full.read_bytes()
        assert len(image) == capacity
        assert image[0x8000:0x8000 + partition_bin.stat().st_size] == partition_bin.read_bytes()
        assert image[0x10000:0x10000 + ota.stat().st_size] == ota.read_bytes()
        # esptool updates the bootloader size field and its hash for each profile.
        boot_size = (build / (project + ".bootloader.bin")).stat().st_size
        (release / f"bootloader_{mb}MB.bin").write_bytes(image[0x1000:0x1000 + boot_size])
        assert image[0x1003] >> 4 == (2 if mb == 4 else 4)
        report["profiles"][f"{mb}MB"] = layout
        shutil.copy2(csv_path, release / f"partitions_{mb}MB.csv")
    shutil.copytree(SKETCH / "data", release / "data", dirs_exist_ok=True)
    shutil.copy2(SKETCH / "FLASHING_v2.8.0.md", release / "README.md")
    shutil.copy2(SKETCH / "tools/flash_release.py", release / "flash_release.py")
    shutil.copy2(SKETCH / "FLASHING_v2.8.0_zh.md", release / "使用说明.md")
    src = release / "source" / SKETCH.name
    src.mkdir(parents=True, exist_ok=True)
    for pattern in ("*.ino", "*.h", "partitions*.csv"):
        for file in SKETCH.glob(pattern):
            shutil.copy2(file, src / file.name)
    shutil.copytree(SKETCH / "tools", src / "tools", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copytree(SKETCH / "data", src / "data", dirs_exist_ok=True)
    for document in SKETCH.glob("FLASHING_v2.8.0*.md"):
        shutil.copy2(document, src / document.name)
    tests = SKETCH / "build/storage_tests"
    if (tests / "web_results.json").exists():
        shutil.copy2(tests / "web_results.json", release / "web_tests.json")
        shutil.copy2(tests / "web_16MB_390_home.png", release / "web_16MB_preview.png")
    report["sourceSha256"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in src.glob("*.ino")}
    report["sha256"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in release.glob("*.bin")}
    (release / "manifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    downloads = Path.home() / "Downloads" / f"FortunePager_v{VERSION}"
    shutil.copytree(release, downloads, dirs_exist_ok=True)
    archive = downloads.parent / (downloads.name + ".zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for path in downloads.rglob("*"):
            if path.is_file():
                z.write(path, Path(downloads.name) / path.relative_to(downloads))
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
    print(f"Release: {downloads}\nArchive: {archive}")


if __name__ == "__main__":
    main()
