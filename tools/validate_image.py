"""Checks a merged Drift-Watch image (flashed at 0x0) before it is released.

Usage: python3 tools/validate_image.py VERSION image.bin [image_diag.bin ...]

For each image: the bootloader at 0x0 and the app at 0x10000 start with the ESP image magic
byte and name the ESP32-S3; the partition table at 0x8000 has the expected app and data
partitions and ends with its checksum entry; the app fits its 3 MB partition; the app holds
the expected version and build type; and the image holds no old product name and none of the
test passwords the simulations use. Prints one line per check and exits 1 on any failure.
"""
import hashlib
import struct
import sys

ESP_MAGIC = 0xE9
CHIP_ID_ESP32S3 = 9
PART_MAGIC = b"\xaa\x50"
MD5_MAGIC = b"\xeb\xeb"
APP_OFFSET = 0x10000
APP_PARTITION = 3 * 1024 * 1024  # app3M_fat9M_16MB
# ("secret" alone is not checked: the TLS library holds "extended master secret".)
FORBIDDEN = [b"Qwatch", b"QWATCH", b"qwatch", b"latte", b"HomeNet"]


def chip_id(img, off):
    # Image header: magic, segments, flash mode, size/freq, entry (4), then the extended header
    # (wp pin, 3 drive settings, chip id as 2 bytes little-endian).
    return struct.unpack_from("<H", img, off + 12)[0]


def partitions(img):
    out, off = [], 0x8000
    while True:
        e = img[off:off + 32]
        if e[:2] == PART_MAGIC:
            ptype, sub, start, size = struct.unpack_from("<BBII", e, 2)
            out.append((e[12:28].rstrip(b"\x00").decode(), ptype, sub, start, size))
            off += 32
        elif e[:2] == MD5_MAGIC:
            body = img[0x8000:off]
            return out, hashlib.md5(body).digest() == e[16:32]
        else:
            return out, False


def check(version, path, diag):
    img = open(path, "rb").read()
    ok = True

    def say(cond, what):
        nonlocal ok
        ok &= bool(cond)
        print(f"  {'ok  ' if cond else 'FAIL'} {what}")

    print(path)
    say(img[0] == ESP_MAGIC and chip_id(img, 0) == CHIP_ID_ESP32S3, "bootloader at 0x0 is an ESP32-S3 image")
    parts, md5_ok = partitions(img)
    names = {p[0]: p for p in parts}
    say(md5_ok, f"partition table at 0x8000 is whole ({len(parts)} entries, checksum matches)")
    app = names.get("app0")
    say(app and app[3] == APP_OFFSET and app[4] == APP_PARTITION, "app0 starts at 0x10000 and holds 3 MB")
    say("app1" in names and "ffat" in names, "app1 and the FAT data partition are there (16 MB layout)")
    say(img[APP_OFFSET] == ESP_MAGIC and chip_id(img, APP_OFFSET) == CHIP_ID_ESP32S3, "app at 0x10000 is an ESP32-S3 image")
    app_size = len(img) - APP_OFFSET
    say(app_size <= APP_PARTITION, f"app is {app_size} bytes, {100 * app_size / APP_PARTITION:.0f}% of its partition")
    say(len(img) <= 16 * 1024 * 1024, f"whole image is {len(img)} bytes, inside 16 MB of flash")
    say(version.encode() + b"\x00" in img, f"holds the version {version}")
    say((b"diag: frames" in img) == diag, "the timing log is " + ("in this diagnostic build" if diag else "left out of this normal build"))
    for word in FORBIDDEN:
        say(word not in img, f"does not hold {word.decode(errors='replace').strip(chr(0))!r}")
    return ok


def main():
    version, paths = sys.argv[1], sys.argv[2:]
    good = all([check(version, p, p.endswith("_diag.bin")) for p in paths])
    sys.exit(0 if good else 1)


if __name__ == "__main__":
    main()
