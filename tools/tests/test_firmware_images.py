"""Image/update safety checks; no hardware, Arduino setup or third-party packages needed."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import subprocess
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import package_images
import validate_image as images

ROOT = Path(__file__).resolve().parents[2]
VERSION = "0.15.1"
SOURCE = {"commit": "a" * 40, "working_tree_dirty": False}


def esp_image(payload, application=True, diagnostic=False):
    if application:
        payload = struct.pack("<I", images.APP_DESC_MAGIC) + b"\0" * 28 + payload
    if diagnostic:
        payload += b"diag: frames\0"
    header = bytearray(24)
    header[0:2] = bytes((images.ESP_MAGIC, 1))
    struct.pack_into("<H", header, 12, images.CHIP_ID_ESP32S3)
    header[23] = 1
    data = bytes(header) + struct.pack("<II", 0x3C000020, len(payload)) + payload
    checksum = 0xEF
    for byte in payload:
        checksum ^= byte
    data += b"\0" * (15 - len(data) % 16) + bytes((checksum,))
    return data + hashlib.sha256(data).digest()


def factory_image(app, partitions=None):
    image = bytearray(b"\xff" * images.APP_OFFSET)
    boot = esp_image(b"boot", application=False)
    image[:len(boot)] = boot
    entries = b""
    for name, kind, subtype, offset, size, flags in partitions or images.EXPECTED_PARTITIONS:
        entries += struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size,
                               name.encode(), flags)
    entries += images.MD5_MAGIC + b"\xff" * 14 + hashlib.md5(entries).digest()
    image[images.PARTITION_OFFSET:images.PARTITION_OFFSET + len(entries)] = entries
    return bytes(image) + app


class ImageSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.app = esp_image(VERSION.encode() + b"\0firmware")
        self.factory = factory_image(self.app)

    def package(self, factory=None, app=None, diagnostic=False):
        factory_path = self.directory / ("candidate_diag.bin" if diagnostic else "candidate.bin")
        app_path = self.directory / "build_app.bin"
        factory_path.write_bytes(self.factory if factory is None else factory)
        app_path.write_bytes(self.app if app is None else app)
        output = package_images.package(VERSION, factory_path, app_path, diagnostic, 6, SOURCE)
        return factory_path, *output

    def edit_manifest(self, path, change):
        record = json.loads(path.read_text(encoding="utf-8"))
        change(record)
        path.write_text(json.dumps(record), encoding="utf-8")

    def test_packaging_pairs_identical_application_and_provenance(self):
        _, update, manifest = self.package()
        self.assertEqual(update.read_bytes(), self.app)
        record = json.loads(manifest.read_text(encoding="utf-8"))
        self.assertEqual(record["source"], SOURCE)
        self.assertEqual(record["settings_schema_version"], 6)
        images.validate_manifest(VERSION, manifest)

    def test_source_provenance_ignores_only_generated_ci_outputs(self):
        cases = [
            ("", "build-normal.log\0build-diag.log\0reports/image.bin\0", False),
            (" M build.sh\n", "reports/image.bin\0", True),
            ("", "reports/image.bin\0DriftWatch/new.cpp\0", True),
            ("", "tools/new_packager.py\0", True),
            ("", "new-document.md\0", True),
        ]
        for tracked, untracked, dirty in cases:
            responses = [subprocess.CompletedProcess([], 0, "a" * 40 + "\n"),
                         subprocess.CompletedProcess([], 0, tracked),
                         subprocess.CompletedProcess([], 0, untracked)]
            with self.subTest(tracked=tracked, untracked=untracked), \
                    mock.patch.object(package_images.subprocess, "run", side_effect=responses):
                self.assertEqual(package_images.source_record(self.directory),
                                 {"commit": "a" * 40, "working_tree_dirty": dirty})

    def test_manifest_rejects_missing_or_invalid_provenance(self):
        for bad_source in (None, {}, {"commit": "not-a-sha", "working_tree_dirty": False},
                           {"commit": "a" * 40, "working_tree_dirty": "false"}):
            with self.subTest(source=bad_source):
                _, _, manifest = self.package()
                self.edit_manifest(manifest, lambda m: m.update(source=bad_source))
                with self.assertRaisesRegex(images.ImageError, "source"):
                    images.validate_manifest(VERSION, manifest)

    def test_actual_released_images_still_validate_and_package(self):
        for diagnostic in (False, True):
            with self.subTest(diagnostic=diagnostic):
                suffix = "_diag" if diagnostic else ""
                factory = (ROOT / "firmware" / f"drift_watch_v{VERSION}{suffix}.bin").read_bytes()
                images.validate_binary(factory, VERSION, "factory", diagnostic)
                _, update, manifest = self.package(factory, factory[images.APP_OFFSET:], diagnostic)
                images.validate_binary(update.read_bytes(), VERSION, "application_update", diagnostic)
                images.validate_manifest(VERSION, manifest)

    def test_real_update_erase_and_write_preserve_seeded_nvs(self):
        factory = (ROOT / "firmware" / f"drift_watch_v{VERSION}.bin").read_bytes()
        factory_path, update_path, _ = self.package(factory, factory[images.APP_OFFSET:])
        seeded_nvs = bytes(range(256)) * (images.NVS_SIZE // 256)
        for path, kind, should_preserve in ((update_path, "application_update", True),
                                            (factory_path, "factory", False)):
            with self.subTest(kind=kind):
                flash = bytearray(b"\xff" * images.FLASH_SIZE)
                flash[images.NVS_OFFSET:images.NVS_OFFSET + images.NVS_SIZE] = seeded_nvs
                artifact = images.artifact_record(path, kind)
                start = artifact["erase_range"]["offset"]
                end = start + artifact["erase_range"]["size_bytes"]
                flash[start:end] = b"\xff" * (end - start)
                offset = artifact["flash_offset"]
                flash[offset:offset + artifact["size_bytes"]] = path.read_bytes()
                actual = flash[images.NVS_OFFSET:images.NVS_OFFSET + images.NVS_SIZE]
                self.assertEqual(actual == seeded_nvs, should_preserve)
                if should_preserve:
                    self.assertGreaterEqual(start, images.APP_OFFSET)
                    self.assertLessEqual(end, images.APP_OFFSET + images.APP_PARTITION)

    def test_reject_renamed_factory_and_trailing_bytes_as_update(self):
        for value in (self.factory, self.app + b"\xff", self.app + self.app):
            with self.subTest(length=len(value)), self.assertRaises(images.ImageError):
                images.validate_binary(value, VERSION, "application_update", False)

    def test_reject_empty_truncated_and_wrong_chip_images(self):
        wrong_chip = bytearray(self.app)
        wrong_chip[12] = 0
        for value in (b"", self.app[:23], self.app[:32], self.app[:-1], bytes(wrong_chip)):
            with self.subTest(length=len(value)), self.assertRaises(images.ImageError):
                images.validate_binary(value, VERSION, "application_update", False)

    def test_reject_corrupt_segment_checksum_and_appended_hash(self):
        for offset in (50, len(self.app) - 33, len(self.app) - 1):
            bad = bytearray(self.app)
            bad[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(images.ImageError):
                images.validate_binary(bytes(bad), VERSION, "application_update", False)

    def test_reject_wrong_version_diagnostic_flavor_and_test_secret(self):
        cases = [(self.app, "0.99.0", False), (self.app, VERSION, True),
                 (esp_image(VERSION.encode() + b"\0HomeNet"), VERSION, False)]
        for value, version, diagnostic in cases:
            with self.subTest(version=version, diagnostic=diagnostic), self.assertRaises(images.ImageError):
                images.validate_binary(value, version, "application_update", diagnostic)

    def test_reject_oversized_update(self):
        with self.assertRaisesRegex(images.ImageError, "3 MB"):
            images.validate_binary(b"\0" * (images.APP_PARTITION + 1), VERSION, "application_update", False)

    def test_reject_different_partition_layout_even_with_valid_md5(self):
        layout = list(images.EXPECTED_PARTITIONS)
        label, kind, subtype, offset, size, flags = layout[2]
        layout[2] = (label, kind, subtype, offset + images.SECTOR_SIZE, size, flags)
        with self.assertRaisesRegex(images.ImageError, "partition layout"):
            images.validate_binary(factory_image(self.app, layout), VERSION, "factory", False)

    def test_reject_corrupt_partition_checksum_or_embedded_nvs(self):
        for offset in (images.PARTITION_OFFSET + 16, images.NVS_OFFSET):
            value = bytearray(self.factory)
            value[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(images.ImageError):
                images.validate_binary(bytes(value), VERSION, "factory", False)

    def test_reject_app_from_different_build(self):
        other_app = esp_image(VERSION.encode() + b"\0different firmware")
        with self.assertRaisesRegex(images.ImageError, "differs"):
            self.package(app=other_app)
        self.assertFalse((self.directory / "candidate_update.bin").exists())

    def test_manifest_rejects_unsafe_offsets_preservation_claims_and_hashes(self):
        changes = {
            "offset": lambda m: m["artifacts"][1].update(flash_offset=0),
            "preservation": lambda m: m["artifacts"][0].update(preserves_nvs=True),
            "hash": lambda m: m["artifacts"][1].update(sha256="0" * 64),
            "erase": lambda m: m["artifacts"][1]["erase_range"].update(offset=images.NVS_OFFSET),
            "slot": lambda m: m["target"].update(application_slot="app1"),
            "layout hash": lambda m: m["target"].update(partition_table_sha256="0" * 64),
            "erase all": lambda m: m["update_requirements"].update(erase_all_allowed=True),
            "traversal": lambda m: m["artifacts"][1].update(file="../outside.bin"),
        }
        for name, change in changes.items():
            with self.subTest(change=name):
                _, _, manifest = self.package()
                self.edit_manifest(manifest, change)
                with self.assertRaises(images.ImageError):
                    images.validate_manifest(VERSION, manifest)

    def test_mismatched_pair_rejected_even_if_hash_is_updated(self):
        _, update, manifest = self.package()
        update.write_bytes(esp_image(VERSION.encode() + b"\0other valid app"))
        self.edit_manifest(manifest, lambda m: m["artifacts"].__setitem__(
            1, images.artifact_record(update, "application_update")))
        with self.assertRaisesRegex(images.ImageError, "differs"):
            images.validate_manifest(VERSION, manifest)

    def test_cli_helper_reports_truncation_without_traceback(self):
        bad = self.directory / "broken_update.bin"
        bad.write_bytes(b"\xe9")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertFalse(images.check(VERSION, bad))
        self.assertIn("FAIL", output.getvalue())


if __name__ == "__main__":
    unittest.main()
