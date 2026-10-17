#!/usr/bin/env python3
"""Package existing, validated Linux binaries without installing or publishing."""

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def command(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def binary_info(path):
    versions = command("readelf", "--version-info", str(path))
    required = {}
    for family in ("GLIBC", "GLIBCXX", "CXXABI"):
        matches = re.findall(rf"\b{family}_([0-9.]+)", versions)
        if matches:
            required[family] = max(matches, key=lambda item: tuple(map(int, item.split("."))))
    dynamic = command("readelf", "-d", str(path))
    return {
        "sha256": digest(path),
        "requiredSymbols": required,
        "neededLibraries": re.findall(r"\(NEEDED\).*\[(.*?)\]", dynamic),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-dev")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--juce-dir", type=Path)
    args = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("This package recipe targets Linux x86-64 only.")
    build = args.build_dir.resolve()
    output = (args.output_dir or build / "dist").resolve()
    version = re.search(r"project\(KickDuck1 VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text())[1]
    artifacts = build / "KickDuck1_artefacts/Release"
    bundle = artifacts / "VST3/KickDuck1.vst3"
    plugin = bundle / "Contents/x86_64-linux/KickDuck1.so"
    standalone = artifacts / "Standalone/KickDuck1"
    if not plugin.is_file() or not standalone.is_file():
        parser.error("Build the plugin preset before packaging.")
    if not (bundle / "Contents/Resources/moduleinfo.json").is_file():
        parser.error("VST3 bundle has no moduleinfo.json.")
    candidates = [args.juce_dir] if args.juce_dir else [ROOT / "build/_deps/juce-src", build / "_deps/juce-src"]
    juce = next((path.resolve() for path in candidates if (path / "LICENSE.md").is_file()), None)
    if juce is None:
        parser.error("JUCE source notices missing; provide --juce-dir.")
    revision = command("git", "rev-parse", "HEAD")
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", command("git", "show", "-s", "--format=%ct", "HEAD")))
    package_name = f"KickDuck1-{version}-linux-x86_64"
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="kickduck-release-") as scratch:
        package = Path(scratch) / package_name
        package.mkdir()
        shutil.copytree(bundle, package / "KickDuck1.vst3")
        (package / "Standalone").mkdir()
        shutil.copy2(standalone, package / "Standalone/KickDuck1")
        for name in ("README.md", "CHANGELOG.md", "LICENSE", "THIRD_PARTY_NOTICES.md"):
            shutil.copy2(ROOT / name, package / name)
        shutil.copytree(ROOT / "LICENSES", package / "LICENSES")
        (package / "docs").mkdir()
        shutil.copytree(ROOT / "docs/images", package / "docs/images")
        for name in ("INSTALL.md", "USER_GUIDE.md", "DISTRIBUTION.md", "REALTIME_AUDIT.md", "CONTROL_SEMANTICS.md", "CONTEXT.md", "PLAN.md"):
            shutil.copy2(ROOT / "docs" / name, package / "docs" / name)
        notices = [juce / "LICENSE.md"]
        # Preserve upstream notices, including the selected GPLv3 SDK option.
        for directory in (juce / "modules").iterdir():
            if not directory.is_dir():
                continue
            for path in directory.rglob("*"):
                if path.is_file() and (path.name.upper() in {"LICENSE", "LICENSE.MD", "LICENSE.TXT", "COPYING"}
                                       or "licence" in path.name.lower()
                                       or (path.name == "README" and path.parent.name in {"jpglib", "zlib"})):
                    notices.append(path)
        for source in sorted(set(notices)):
            target = package / "THIRD_PARTY/JUCE" / source.relative_to(juce)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        source_name = f"KickDuck1-{version}-source"
        source = Path(scratch) / source_name
        source.mkdir()
        # Only tracked files belong in the source distribution. Stage new files first.
        files = command("git", "ls-files", "--cached", "-z").split("\0")
        for name in sorted(set(files)):
            if not name:
                continue
            target = source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, target)
        if not (source / "LICENSE").is_file() or not (source / "docs/images").is_dir():
            parser.error("Stage the licence and documentation assets before packaging.")
        shutil.copytree(juce, source / "build/_deps/juce-src",
                        ignore=shutil.ignore_patterns(".git"))
        info = {
            "product": "KickDuck1", "version": version, "platform": "linux-x86_64",
            "gitRevision": revision,
            "workingTreeDirty": bool(command("git", "status", "--porcelain", "--untracked-files=no")),
            "projectLicence": "AGPL-3.0-only", "publicReleasePublished": False,
            "correspondingSource": f"{source_name}.tar.gz",
            "binaries": {
                "KickDuck1.vst3/Contents/x86_64-linux/KickDuck1.so": binary_info(plugin),
                "Standalone/KickDuck1": binary_info(standalone),
            },
        }
        (package / "BUILD_INFO.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
        checksums = [f"{digest(path)}  {path.relative_to(package).as_posix()}"
                     for path in sorted(package.rglob("*")) if path.is_file()]
        (package / "SHA256SUMS").write_text("\n".join(checksums) + "\n", encoding="utf-8")

        def metadata(member):
            member.uid = member.gid = 0
            member.uname = member.gname = ""
            member.mtime = epoch
            return member

        for directory in (package, source):
            destination = output / f"{directory.name}.tar.gz"
            with destination.open("wb") as raw:
                with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=epoch) as compressed:
                    with tarfile.open(fileobj=compressed, mode="w") as tar:
                        tar.add(directory, arcname=directory.name, filter=metadata)
            destination.with_name(destination.name + ".sha256").write_text(
                f"{digest(destination)}  {destination.name}\n", encoding="utf-8")
            print(destination)
    print("AGPLv3 packages prepared locally. No GitHub release/tag was published.")


if __name__ == "__main__":
    main()
