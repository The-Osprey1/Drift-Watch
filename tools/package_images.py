"""Pair a factory image with its exact app-only update and a JSON build manifest.

Usage: python3 tools/package_images.py VERSION FACTORY.bin APPLICATION.bin [--diagnostic]
Writes FACTORY_update.bin and FACTORY_manifest.json beside FACTORY.bin. No device is
accessed and no hardware/migration qualification is implied by this packaging step.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

import validate_image as images


def source_record(root):
    try:
        commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, check=True,
                                capture_output=True, text=True).stdout.strip()
        tracked = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"],
                                 cwd=root, check=True, capture_output=True, text=True).stdout
        untracked = subprocess.run(["git", "ls-files", "--others", "--exclude-standard", "-z"],
                                   cwd=root, check=True, capture_output=True, text=True).stdout.split("\0")
        # CI's tee and artifact-copy steps create these outside ignored build/out.
        # Ignore only these generated untracked outputs; tracked changes still count,
        # as do new untracked source, tests, build helpers and documentation.
        new_inputs = [name for name in untracked if name and
                      name not in ("build-normal.log", "build-diag.log") and
                      not name.startswith("reports/")]
        return {"commit": commit, "working_tree_dirty": bool(tracked or new_inputs)}
    except (OSError, subprocess.CalledProcessError):
        return {"commit": None, "working_tree_dirty": None}


def package(version, factory_path, application_path, diagnostic, settings_schema, source):
    factory_path, application_path = Path(factory_path), Path(application_path)
    factory, application = factory_path.read_bytes(), application_path.read_bytes()
    images.validate_binary(factory, version, "factory", diagnostic)
    images.validate_binary(application, version, "application_update", diagnostic)
    images.require(factory[images.APP_OFFSET:] == application,
                   "application input differs from the factory image; refusing to package")
    update_path = factory_path.with_name(factory_path.stem + "_update.bin")
    manifest_path = factory_path.with_name(factory_path.stem + "_manifest.json")
    images.require(update_path.resolve() != application_path.resolve(),
                   "application input must not be the generated update path")
    update_path.write_bytes(application)
    manifest = {
        "schema_version": 1,
        "product": "DriftWatch",
        "firmware_version": version,
        "build_variant": "diagnostic" if diagnostic else "standard",
        "settings_schema_version": settings_schema,
        "source": source,
        "target": images.target_record(factory),
        "update_requirements": images.UPDATE_REQUIREMENTS,
        "settings_preservation_note": (
            "The update write/erase range leaves NVS untouched. Settings and Wi-Fi survive only "
            "when the installed layout/selected slot and firmware settings migration are compatible. "
            "This manifest does not verify a connected device or qualify an upgrade/downgrade."
        ),
        "artifacts": [images.artifact_record(factory_path, "factory"),
                      images.artifact_record(update_path, "application_update")],
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    images.validate_manifest(version, manifest_path)
    return update_path, manifest_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("factory")
    parser.add_argument("application")
    parser.add_argument("--diagnostic", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    settings = (root / "DriftWatch" / "settings.h").read_text(encoding="utf-8")
    match = re.search(r"SETTINGS_VERSION\s*=\s*(\d+)\s*;", settings)
    if not match:
        parser.error("cannot determine SETTINGS_VERSION")
    try:
        output = package(args.version, args.factory, args.application, args.diagnostic,
                         int(match.group(1)), source_record(root))
        for path in output:
            print(path)
        return 0
    except (OSError, images.ImageError) as exc:
        print(f"Packaging failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
