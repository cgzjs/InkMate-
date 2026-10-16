# FortunePager 2.8.0: 4MB / 16MB

## Storage

| Profile | Shared content partition | Book upload cap | Vocabulary cap |
| --- | --- | --- | --- |
| 4MB | 640 KiB | 256 KiB | 256 KiB, 5,000 words per deck |
| 16MB | 12.625 MiB | 8 MiB | 2 MiB, 20,000 words per deck |

Books, vocabulary packs, review progress, pictures and other saved content share
this space. Actual upload limits are reduced by the available space and a safety
reserve (64 KiB on the small partition, 256 KiB on the large partition).
Replacement uploads need enough free space for BOTH the old file and its temporary
replacement. The old file is retained on failed or interrupted uploads. Delete the
old book from the web console first if a large replacement does not fit.

The web header reports the PHYSICAL Flash capacity (4MB or 16MB), separately from
the mounted content capacity and free bytes. A 16MB chip running the old partition
table displays a serial-expansion warning, not an inflated available-space figure.

Books upload in chunks, with 32-bit page numbers and sparse disk checkpoints instead
of the old 60 KiB / 512-page cap. Vocabulary offsets also live on disk, avoiding a
large RAM index. The web console accepts UTF-8 TSV packs (word, TAB, meaning; each
line <= 256 bytes). Replacing a deck resets ONLY that deck's review progress.
Changing partitions or doing an ordinary firmware update does NOT reset progress.

## Existing device: serial upgrade and 16MB expansion

Ordinary application OTA cannot replace the bootloader or partition table. Use
the serial upgrade once to unlock the larger partition. Do not use a 16MB full
image on a 4MB chip. The provided script detects the actual flash ID automatically.

1. Unzip this release. Keep a battery connected. Connect CH340 TX/RX crossed to
   ESP32 RX0/TX0, and connect GND. Use 3.3V UART levels.
2. Hold SW6 (GPIO0 / BOOT), reset with SW1 (EN), release SW6. Close Serial Monitor.
3. Open PowerShell in this release folder. Replace COM5 with your actual port:

   ```powershell
   python .\flash_release.py --port COM5
   ```

4. This first backs up the ENTIRE physical flash, then writes the capacity-specific
   bootloader and partition table, resets OTA selection to app0, and writes v2.8.0.
   It does not write NVS or the content partition. This migration expects the old
   FortunePager partition layout (NVS 0x9000, app0 0x10000, content 0x350000).
   Do not use it on an unrelated firmware/partition layout.
5. Release BOOT and reset/power-cycle. Arduino ESP32 3.3.10 LittleFS grows the old
   filesystem to the enlarged partition on mount. Mount failures are not silently
   formatted; keep the backup until the web console confirms files and progress.
6. If power fails during serial writing, re-enter BOOT and repeat using the same
   release. Do not erase flash or overwrite your original backup.

## New board / intentionally reset content

Only when you want to replace saved content with the included vocabulary packs:

```powershell
python .\flash_release.py --port COM5 --fresh
```

This also backs up first, but writes the whole matching full image and replaces
books, review progress and settings in the covered flash range. The full images
already contain both vocabulary packs. When flashing with another tool, use the
matching `*_4MB_full.bin` or `*_16MB_full.bin` at address `0x0`.

## Subsequent web OTA

Both capacities use the SAME `FortunePager_v2.8.0_ota.bin` (application only).
Connect to `FortunePager`, password `12345678`, open `http://192.168.4.1/update`.
OTA login is `admin` / `12345678`. This preserves files, progress and partitions.
Do not upload the full image, partition table or filesystem image to this page.
OTA alone on an old 16MB device updates the web features but leaves content space
at 640 KiB until the serial expansion above is done.

Screen rotation and SW4 NEXT / SW8 PREV / SW5 OK / SW2 BACK are unchanged.

## Rebuild

The sketch's default `partitions.csv` remains the safe 4MB profile. Do not replace
it with the 16MB CSV for a 4MB board. Run `python tools/build_release.py` with the
installed Arduino CLI, ESP32 core and libraries to create both full images and the
common OTA binary. `manifest.json` records layouts and SHA256 hashes.

See the repository's `docs/WEB_GUIDE.md` and `docs/DEVICE_GUIDE.md` for daily use.
