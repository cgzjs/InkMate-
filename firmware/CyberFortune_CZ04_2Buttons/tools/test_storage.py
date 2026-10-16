"""Run actual firmware storage functions against a small host filesystem/server."""
import argparse
from pathlib import Path
import re
import subprocess

SKETCH = Path(__file__).resolve().parents[1]
FUNCTIONS = (
    "loadVocabIndex", "readVocabWord", "readBookPage", "resetBookPagination",
    "saveBookCheckpoint", "findBookPageOffset", "nextBookPage", "previousBookPage",
    "failContentUpload", "validateVocabUploadLine", "handleContentUpload",
    "handleContentUploadFinished", "handleWebDeleteBook",
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    args = parser.parse_args()
    source = (SKETCH / (SKETCH.name + ".ino")).read_text(encoding="utf-8")
    output = SKETCH / "build/storage_tests"
    output.mkdir(parents=True, exist_ok=True)
    extracted = []
    for name in FUNCTIONS:
        start = re.search(r"^\w[^\n]*\b" + name + r"\(", source, re.M).start()
        end = source.index("\n}", start) + 2
        extracted.append(source[start:end])
    (output / "firmware_functions.inc").write_text("\n\n".join(extracted), encoding="utf-8")
    compiler = [str(args.compiler)]
    if args.compiler.stem == "zig":
        compiler.append("c++")
    executable = output / "storage_tests.exe"
    subprocess.run(compiler + ["-std=c++17", "-O0", "-UNDEBUG", "-I", str(SKETCH), "-I", str(output),
                               str(SKETCH / "tools/storage_host_test.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
