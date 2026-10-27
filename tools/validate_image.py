"""Validate DriftWatch factory/update images and their paired build manifests.

Usage: python3 tools/validate_image.py VERSION image.bin [image... manifest.json...]
Legacy .bin names are factory/rescue images at 0x0. *_update.bin contains exactly
one ESP32-S3 application for the existing app0 slot at 0x10000. This checks files,
not the connected device: its partition table, selected slot and settings migration
compatibility must be established separately before a preserving update.
"""
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ESP_MAGIC = 0xE9
CHIP_ID_ESP32S3 = 9
APP_DESC_MAGIC = 0xABCD5432
PART_MAGIC = b"\xaa\x50"
MD5_MAGIC = b"\xeb\xeb"
PARTITION_OFFSET = 0x8000
SECTOR_SIZE = 0x1000
APP_OFFSET = 0x10000
APP_PARTITION = 3 * 1024 * 1024
FLASH_SIZE = 16 * 1024 * 1024
NVS_OFFSET, NVS_SIZE = 0x9000, 0x5000
LAYOUT_ID = "app3M_fat9M_16MB-v1"
BOARD = "Waveshare ESP32-S3-Touch-LCD-1.28"
# name, type, subtype, offset, size, flags. A different layout needs qualification.
EXPECTED_PARTITIONS = [
    ("nvs", 1, 2, NVS_OFFSET, NVS_SIZE, 0),
    ("otadata", 1, 0, 0xE000, 0x2000, 0),
    ("app0", 0, 0x10, APP_OFFSET, APP_PARTITION, 0),
    ("app1", 0, 0x11, 0x310000, APP_PARTITION, 0),
    ("ffat", 1, 0x81, 0x610000, 0x9E0000, 0),
    ("coredump", 1, 3, 0xFF0000, 0x10000, 0),
]
# "secret" alone is not checked: TLS libraries contain "extended master secret".
FORBIDDEN = [b"Qwatch", b"QWATCH", b"qwatch", b"latte", b"HomeNet"]


class ImageError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ImageError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def erase_range(offset, size):
    """All sectors touched by a serial write, including final partial sectors."""
    start = offset // SECTOR_SIZE * SECTOR_SIZE
    end = (offset + size + SECTOR_SIZE - 1) // SECTOR_SIZE * SECTOR_SIZE
    return {"offset": start, "size_bytes": end - start}


def esp_image_size(image, application=False):
    """Check segment boundaries, ESP XOR checksum, and optional appended SHA-256.

    Return the image extent so an app-only file cannot hide a merged image or
    trailing data. The factory bootloader may be followed by erased padding.
    """
    require(len(image) >= 24, "truncated ESP image header")
    require(image[0] == ESP_MAGIC, "missing ESP image magic")
    require(struct.unpack_from("<H", image, 12)[0] == CHIP_ID_ESP32S3,
            "image is not for ESP32-S3")
    require(1 <= image[1] <= 16, "invalid ESP segment count")
    require(image[23] in (0, 1), "invalid ESP appended-hash flag")
    pos, checksum = 24, 0xEF
    for _ in range(image[1]):
        require(pos + 8 <= len(image), "truncated ESP segment header")
        size = struct.unpack_from("<I", image, pos + 4)[0]
        pos += 8
        require(size <= len(image) - pos, "truncated ESP segment data")
        for byte in image[pos:pos + size]:
            checksum ^= byte
        pos += size
    checksum_pos = pos + 15 - pos % 16
    require(checksum_pos < len(image), "missing ESP checksum")
    require(image[checksum_pos] == checksum, "ESP segment checksum mismatch")
    end = checksum_pos + 1
    if image[23]:
        require(end + 32 <= len(image), "truncated ESP appended hash")
        require(hashlib.sha256(image[:end]).digest() == image[end:end + 32],
                "ESP appended SHA-256 mismatch")
        end += 32
    if application:
        require(len(image) >= 36 and struct.unpack_from("<I", image, 32)[0] == APP_DESC_MAGIC,
                "missing ESP-IDF application descriptor (not an app-only image)")
    return end


def partition_table(image):
    require(len(image) >= PARTITION_OFFSET + SECTOR_SIZE, "truncated partition-table sector")
    parts, pos = [], PARTITION_OFFSET
    limit = PARTITION_OFFSET + SECTOR_SIZE
    while pos + 32 <= limit:
        entry = image[pos:pos + 32]
        if entry[:2] == PART_MAGIC:
            _, kind, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", entry)
            try:
                name = label.rstrip(b"\0").decode("ascii")
            except UnicodeDecodeError as exc:
                raise ImageError("non-ASCII partition label") from exc
            parts.append((name, kind, subtype, offset, size, flags))
            pos += 32
        elif entry[:2] == MD5_MAGIC:
            require(hashlib.md5(image[PARTITION_OFFSET:pos]).digest() == entry[16:32],
                    "partition-table MD5 mismatch")
            require(parts == EXPECTED_PARTITIONS, "partition layout differs from the supported app0 layout")
            return sha256(image[PARTITION_OFFSET:limit])
        else:
            raise ImageError("missing partition-table checksum entry")
    raise ImageError("partition table exceeds its sector")


def validate_application(image, version, diagnostic):
    require(0 < len(image) <= APP_PARTITION, "application does not fit its 3 MB partition")
    require(esp_image_size(image, application=True) == len(image),
            "app-only image contains trailing/merged data")
    require(version.encode() + b"\0" in image, f"application does not hold version {version}")
    require((b"diag: frames" in image) == diagnostic, "application diagnostic flavor is incorrect")
    for word in FORBIDDEN:
        require(word not in image, f"image contains prohibited test string {word!r}")


def validate_binary(image, version, image_kind, diagnostic):
    require(image_kind in ("factory", "application_update"), "unknown image kind")
    if image_kind == "application_update":
        validate_application(image, version, diagnostic)
        touched = erase_range(APP_OFFSET, len(image))
        require(touched["offset"] >= NVS_OFFSET + NVS_SIZE and
                touched["offset"] + touched["size_bytes"] <= APP_OFFSET + APP_PARTITION,
                "update erase range leaves app0 or overlaps NVS")
        return
    require(APP_OFFSET < len(image) <= FLASH_SIZE, "invalid factory image size")
    boot_end = esp_image_size(image[:PARTITION_OFFSET])
    require(all(b == 0xFF for b in image[boot_end:PARTITION_OFFSET]),
            "unexpected data between bootloader and partition table")
    partition_table(image)
    require(image[NVS_OFFSET:NVS_OFFSET + NVS_SIZE] == b"\xff" * NVS_SIZE,
            "factory NVS padding is not erased; possible embedded settings or credentials")
    validate_application(image[APP_OFFSET:], version, diagnostic)
    for word in FORBIDDEN:
        require(word not in image, f"factory image contains prohibited test string {word!r}")


def artifact_record(path, image_kind):
    path = Path(path)
    image = path.read_bytes()
    offset = 0 if image_kind == "factory" else APP_OFFSET
    return {
        "file": path.name,
        "kind": image_kind,
        "sha256": sha256(image),
        "size_bytes": len(image),
        "flash_offset": offset,
        "erase_range": erase_range(offset, len(image)),
        "preserves_nvs": image_kind == "application_update",
        "settings_preservation": "conditional" if image_kind == "application_update" else "erases",
    }


def target_record(factory):
    return {
        "board": BOARD,
        "chip": "esp32s3",
        "flash_size_bytes": FLASH_SIZE,
        "layout_id": LAYOUT_ID,
        "partition_table_offset": PARTITION_OFFSET,
        "partition_table_size_bytes": SECTOR_SIZE,
        "partition_table_sha256": partition_table(factory),
        "application_slot": "app0",
        "application_offset": APP_OFFSET,
        "application_size_limit_bytes": APP_PARTITION,
        "nvs_offset": NVS_OFFSET,
        "nvs_size_bytes": NVS_SIZE,
    }


UPDATE_REQUIREMENTS = {
    "existing_partition_table_matches": True,
    "existing_boot_selection": "app0",
    "compatible_settings_schema_required": True,
    "erase_all_allowed": False,
    "device_preflight_performed_by_packager": False,
    "automatic_rollback": False,
}


def validate_manifest(version, path):
    path = Path(path)
    manifest = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(manifest, dict), "manifest must be an object")
    require(manifest.get("schema_version") == 1, "unsupported manifest schema")
    require(manifest.get("product") == "DriftWatch", "wrong manifest product")
    require(manifest.get("firmware_version") == version, "manifest firmware version mismatch")
    variant = manifest.get("build_variant")
    require(variant in ("standard", "diagnostic"), "unknown build variant")
    settings_schema = manifest.get("settings_schema_version")
    require(type(settings_schema) is int and 1 <= settings_schema <= 255,
            "missing/invalid settings schema version")
    source = manifest.get("source")
    require(isinstance(source, dict) and set(source) == {"commit", "working_tree_dirty"},
            "missing source provenance")
    require((source["commit"] is None and source["working_tree_dirty"] is None) or
            (isinstance(source["commit"], str) and re.fullmatch(r"[0-9a-f]{40}", source["commit"]) and
             type(source["working_tree_dirty"]) is bool), "invalid source commit/dirty flag")
    require(manifest.get("update_requirements") == UPDATE_REQUIREMENTS,
            "missing/unsafe preserving-update preconditions")
    artifacts = manifest.get("artifacts")
    require(isinstance(artifacts, list) and len(artifacts) == 2, "manifest must pair factory and update images")
    images = {}
    for record in artifacts:
        require(isinstance(record, dict), "invalid artifact record")
        name, kind = record.get("file"), record.get("kind")
        require(isinstance(name, str) and name not in ("", ".", "..") and
                "/" not in name and "\\" not in name and ":" not in name,
                "artifact path must be a local filename")
        require(kind in ("factory", "application_update") and kind not in images,
                "duplicate or unknown artifact kind")
        artifact_path = path.parent / name
        image = artifact_path.read_bytes()
        validate_binary(image, version, kind, variant == "diagnostic")
        require(record == artifact_record(artifact_path, kind),
                f"artifact hash, offset, erase range or preservation metadata mismatch: {name}")
        images[kind] = image
    require(set(images) == {"factory", "application_update"}, "both artifact kinds are required")
    require(images["factory"][APP_OFFSET:] == images["application_update"],
            "update application differs from the paired factory image")
    require(manifest.get("target") == target_record(images["factory"]), "manifest target/layout mismatch")


def check(version, path, diag=None):
    """Retain the old check(version, path, diag) helper interface."""
    path = Path(path)
    try:
        if path.suffix == ".json":
            validate_manifest(version, path)
            print(f"{path}: ok (paired images, hashes, layout and update preconditions)")
        else:
            update = path.stem.endswith("_update")
            diagnostic = path.stem.endswith(("_diag", "_diag_update")) if diag is None else diag
            kind = "application_update" if update else "factory"
            validate_binary(path.read_bytes(), version, kind, diagnostic)
            offset = APP_OFFSET if update else 0
            print(f"{path}: ok ({kind}, offset 0x{offset:X}, ESP checksums, version and flavor)")
        return True
    except (OSError, ValueError, KeyError, TypeError, struct.error) as exc:
        print(f"{path}: FAIL: {exc}")
        return False


def main():
    if len(sys.argv) < 3:
        print(__doc__.splitlines()[2], file=sys.stderr)
        return 2
    version, paths = sys.argv[1], sys.argv[2:]
    return 0 if all([check(version, p) for p in paths]) else 1


if __name__ == "__main__":
    sys.exit(main())
