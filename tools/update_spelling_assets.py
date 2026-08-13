#!/usr/bin/env python3
"""Verify, update, and package Emerald's vendored spelling assets.

Normal Emerald builds never invoke this script and need no network access. The
scheduled Spell dependencies workflow runs ``update`` and opens a pull request
for review when upstream changes. Publishing remains a deliberate tag action.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "resources" / "dictionaries" / "manifest.json"
PACK_ROOT = ROOT / "packaging" / "spelling-packs" / "v1"
HUNSPELL_ROOT = ROOT / "third_party" / "hunspell"
HUNSPELL_METADATA = HUNSPELL_ROOT / "UPSTREAM.json"
GITHUB_API = "https://api.github.com"
LIBREOFFICE_RAW = "https://raw.githubusercontent.com/LibreOffice/dictionaries"
RELEASE_BASE = "https://github.com/MinottiAlessandro/Emerald/releases/download"
MAX_ARCHIVE_BYTES = 10 * 1024 * 1024
LICENSE_FILES = (
    "COPYING",
    "COPYING.LESSER",
    "COPYING.MPL",
    "license.hunspell",
    "license.myspell",
)
REQUIRED_HUNSPELL_FILES = (
    "affentry.cxx",
    "affixmgr.cxx",
    "csutil.cxx",
    "filemgr.cxx",
    "hashmgr.cxx",
    "hunspell.cxx",
    "hunspell.hxx",
    "hunzip.cxx",
    "phonet.cxx",
    "replist.cxx",
    "suggestmgr.cxx",
)


def read_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n",
                    encoding="utf-8")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def hunspell_tree_sha256() -> str:
    """Hash the exact upstream source/license tree independently of mtimes."""
    digest = hashlib.sha256()
    source = HUNSPELL_ROOT / "src" / "hunspell"
    paths = sorted(path for path in source.rglob("*") if path.is_file())
    paths.extend(HUNSPELL_ROOT / name for name in LICENSE_FILES)
    for path in paths:
        relative = path.relative_to(HUNSPELL_ROOT).as_posix().encode("utf-8")
        digest.update(relative)
        digest.update(b"\0")
        digest.update(bytes.fromhex(sha256_file(path)))
        digest.update(b"\n")
    return digest.hexdigest()


def request_bytes(url: str, maximum: int) -> bytes:
    headers = {"User-Agent": "Emerald-spelling-maintenance/1"}
    token = os.environ.get("GITHUB_TOKEN")
    if token and url.startswith((GITHUB_API, "https://github.com/")):
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=60) as response:
        declared = response.headers.get("Content-Length")
        if declared and int(declared) > maximum:
            raise RuntimeError(f"download exceeds size limit: {url}")
        data = response.read(maximum + 1)
    if not data or len(data) > maximum:
        raise RuntimeError(f"download has invalid size: {url}")
    return data


def request_json(url: str) -> dict:
    return json.loads(request_bytes(url, 2 * 1024 * 1024))


def local_asset_path(language: dict, kind: str) -> Path:
    locale = language["locale"]
    if language["builtIn"]:
        root = ROOT / "resources" / "dictionaries" / locale
    else:
        root = PACK_ROOT / locale
    if kind == "affix":
        return root / f"{locale}.aff"
    if kind == "dictionary":
        return root / f"{locale}.dic"
    return root / "NOTICE.txt"


def validate_hash(value: object) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def verify() -> None:
    manifest = read_json(MANIFEST_PATH)
    if manifest.get("schema") != 1:
        raise RuntimeError("unsupported dictionary manifest schema")
    version = manifest.get("packVersion", "")
    expected_tag = f"spell-dictionaries-v{version}"
    if manifest.get("releaseTag") != expected_tag:
        raise RuntimeError("dictionary release tag does not match packVersion")
    if manifest.get("releaseBaseUrl") != f"{RELEASE_BASE}/{expected_tag}":
        raise RuntimeError("dictionary release URL does not match releaseTag")
    commit = manifest.get("upstream", {}).get("commit", "")
    if re.fullmatch(r"[0-9a-f]{40}", commit) is None:
        raise RuntimeError("invalid LibreOffice source revision")

    locales: set[str] = set()
    asset_names: set[str] = set()
    for language in manifest.get("languages", []):
        locale = language.get("locale", "")
        if re.fullmatch(r"[a-z]{2}_[A-Z]{2}", locale) is None or locale in locales:
            raise RuntimeError(f"invalid or duplicate locale: {locale!r}")
        locales.add(locale)
        for kind in ("affix", "dictionary", "notice"):
            part = language.get("files", {}).get(kind, {})
            maximum = part.get("maximumBytes", 0)
            name = part.get("name", "")
            if (re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", name) is None or
                    not validate_hash(part.get("sha256")) or
                    not isinstance(maximum, int) or maximum <= 0 or
                    maximum > 10 * 1024 * 1024):
                raise RuntimeError(f"invalid {locale} {kind} manifest entry")
            if not language["builtIn"] and name in asset_names:
                raise RuntimeError(f"duplicate release asset name: {name}")
            asset_names.add(name)
            path = local_asset_path(language, kind)
            if not path.is_file():
                raise RuntimeError(f"missing spelling asset: {path.relative_to(ROOT)}")
            if path.stat().st_size > maximum:
                raise RuntimeError(f"spelling asset exceeds cap: {path.relative_to(ROOT)}")
            if sha256_file(path) != part["sha256"]:
                raise RuntimeError(f"hash mismatch: {path.relative_to(ROOT)}")

    if locales != {"en_US", "it_IT", "de_DE", "fr_FR", "es_ES"}:
        raise RuntimeError("dictionary manifest has an unexpected language set")

    hunspell = read_json(HUNSPELL_METADATA)
    version = hunspell.get("version", "")
    if hunspell.get("tag") != f"v{version}":
        raise RuntimeError("Hunspell tag does not match version")
    if not validate_hash(hunspell.get("sourceArchiveSha256")):
        raise RuntimeError("invalid Hunspell archive hash")
    source = HUNSPELL_ROOT / "src" / "hunspell"
    for name in (*REQUIRED_HUNSPELL_FILES, *LICENSE_FILES):
        path = source / name if name in REQUIRED_HUNSPELL_FILES else HUNSPELL_ROOT / name
        if not path.is_file():
            raise RuntimeError(f"missing vendored Hunspell file: {path.relative_to(ROOT)}")
    if hunspell.get("vendoredTreeSha256") != hunspell_tree_sha256():
        raise RuntimeError("vendored Hunspell source tree hash does not match provenance")


def bump_patch(version: str) -> str:
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", version)
    if not match:
        raise RuntimeError(f"invalid pack version: {version}")
    major, minor, patch = (int(part) for part in match.groups())
    return f"{major}.{minor}.{patch + 1}"


def update_dictionaries() -> None:
    manifest = read_json(MANIFEST_PATH)
    latest = request_json(
        f"{GITHUB_API}/repos/LibreOffice/dictionaries/commits/master")["sha"]
    if re.fullmatch(r"[0-9a-f]{40}", latest) is None:
        raise RuntimeError("GitHub returned an invalid LibreOffice revision")

    dictionaries_changed = False
    optional_changed = False
    for language in manifest["languages"]:
        for kind in ("affix", "dictionary", "notice"):
            part = language["files"][kind]
            upstream_path = part["upstreamPath"]
            data = request_bytes(f"{LIBREOFFICE_RAW}/{latest}/{upstream_path}",
                                 part["maximumBytes"])
            path = local_asset_path(language, kind)
            previous = path.read_bytes() if path.exists() else b""
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            part["sha256"] = sha256(data)
            changed = data != previous
            dictionaries_changed |= changed
            optional_changed |= not language["builtIn"] and changed

    if dictionaries_changed:
        manifest["upstream"]["commit"] = latest
    if optional_changed:
        version = bump_patch(manifest["packVersion"])
        tag = f"spell-dictionaries-v{version}"
        manifest["packVersion"] = version
        manifest["releaseTag"] = tag
        manifest["releaseBaseUrl"] = f"{RELEASE_BASE}/{tag}"
    write_json(MANIFEST_PATH, manifest)


def safe_extract(archive: Path, destination: Path) -> Path:
    with tarfile.open(archive, "r:gz") as bundle:
        members = bundle.getmembers()
        for member in members:
            target = (destination / member.name).resolve()
            if destination.resolve() not in target.parents and target != destination.resolve():
                raise RuntimeError("Hunspell archive contains an unsafe path")
            if member.issym() or member.islnk():
                raise RuntimeError("Hunspell archive contains an unexpected link")
        bundle.extractall(destination, members=members, filter="data")
    roots = [path for path in destination.iterdir() if path.is_dir()]
    if len(roots) != 1:
        raise RuntimeError("Hunspell archive has an unexpected layout")
    return roots[0]


def update_hunspell() -> None:
    release = request_json(f"{GITHUB_API}/repos/hunspell/hunspell/releases/latest")
    tag = release.get("tag_name", "")
    if re.fullmatch(r"v\d+\.\d+\.\d+", tag) is None:
        raise RuntimeError("GitHub returned an invalid Hunspell release tag")
    current = read_json(HUNSPELL_METADATA)
    if current.get("tag") == tag:
        return

    archive_url = f"https://github.com/hunspell/hunspell/archive/refs/tags/{tag}.tar.gz"
    data = request_bytes(archive_url, MAX_ARCHIVE_BYTES)
    with tempfile.TemporaryDirectory(prefix="emerald-hunspell-") as temporary:
        temporary_path = Path(temporary)
        archive = temporary_path / "source.tar.gz"
        archive.write_bytes(data)
        source_root = safe_extract(archive, temporary_path / "source")
        source_dir = source_root / "src" / "hunspell"
        for name in REQUIRED_HUNSPELL_FILES:
            if not (source_dir / name).is_file():
                raise RuntimeError(f"Hunspell release is missing {name}")
        destination = HUNSPELL_ROOT / "src" / "hunspell"
        shutil.rmtree(destination)
        shutil.copytree(source_dir, destination)
        for name in LICENSE_FILES:
            shutil.copy2(source_root / name, HUNSPELL_ROOT / name)

    version = tag.removeprefix("v")
    current.update({
        "version": version,
        "tag": tag,
        "sourceArchive": archive_url,
        "sourceArchiveSha256": sha256(data),
        "vendoredTreeSha256": hunspell_tree_sha256(),
    })
    write_json(HUNSPELL_METADATA, current)


def update() -> None:
    update_hunspell()
    update_dictionaries()
    verify()


def package(output: Path) -> None:
    verify()
    if output.exists() and any(output.iterdir()):
        raise RuntimeError(f"output directory is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    manifest = read_json(MANIFEST_PATH)
    for language in manifest["languages"]:
        if language["builtIn"]:
            continue
        for kind in ("affix", "dictionary", "notice"):
            source = local_asset_path(language, kind)
            shutil.copyfile(source, output / language["files"][kind]["name"])
    shutil.copyfile(MANIFEST_PATH, output / "manifest.json")
    sums = []
    for path in sorted(output.iterdir()):
        if path.name != "SHA256SUMS":
            sums.append(f"{sha256_file(path)}  {path.name}")
    (output / "SHA256SUMS").write_text("\n".join(sums) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("verify", help="verify all committed spelling assets")
    subparsers.add_parser("update", help="download current upstream snapshots")
    package_parser = subparsers.add_parser("package", help="stage release assets")
    package_parser.add_argument("output", type=Path)
    args = parser.parse_args()

    try:
        if args.command == "verify":
            verify()
        elif args.command == "update":
            update()
        else:
            package(args.output.resolve())
    except (OSError, KeyError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"spelling assets: {error}", file=sys.stderr)
        return 1
    print(f"spelling assets: {args.command} succeeded")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
