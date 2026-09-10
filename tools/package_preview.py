#!/usr/bin/env python3
"""Build local development-preview archives. Never commits, uploads or publishes."""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inventory(root):
    names = (root / "packaging/source-files.txt").read_text().splitlines()
    entries = {}
    for name in names:
        if not name or name.startswith("#"):
            continue
        relative = PurePosixPath(name)
        if (not re.fullmatch(r"[A-Za-z0-9_./-]+", name)
                or relative.is_absolute() or ".." in relative.parts
                or ".git" in relative.parts or str(relative) != name
                or name in entries):
            raise ValueError("invalid or duplicate source entry: " + name)
        path = root / name
        if (not path.is_file() or path.resolve() != root.resolve() / name
                or path.stat().st_size > 2 * 1024 * 1024):
            raise ValueError("missing, linked or oversized source entry: " + name)
        data = path.read_bytes()
        if re.search(rb"-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{20,}", data):
            raise ValueError("possible credential in source entry: " + name)
        entries[name] = digest(data)
    if not entries:
        raise ValueError("empty source inventory")
    return dict(sorted(entries.items()))


def snapshot(root, destination):
    entries = inventory(root)
    destination.mkdir()
    for name, expected in entries.items():
        data = (root / name).read_bytes()
        if digest(data) != expected:
            raise ValueError("source changed while copying: " + name)
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return entries


def tree_files(root):
    result = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError("symlink is not allowed in package: " + str(path))
        if path.is_file():
            result[path.relative_to(root).as_posix()] = path
    return result


def verify_snapshot(root, entries):
    files = tree_files(root)
    if set(files) != set(entries):
        raise ValueError("source snapshot gained or lost files during validation")
    for name, expected in entries.items():
        if digest(files[name].read_bytes()) != expected:
            raise ValueError("source snapshot changed during validation: " + name)


def describe(root, manifest):
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    rows = [f"{digest(path.read_bytes())}  {name}\n"
            for name, path in tree_files(root).items() if name != "SHA256SUMS"]
    (root / "SHA256SUMS").write_text("".join(rows))


def archive(root, destination, top):
    # Fixed archive metadata, including gzip header; no user IDs or local paths.
    with destination.open("xb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as zipped:
            with tarfile.open(fileobj=zipped, mode="w", format=tarfile.PAX_FORMAT) as tar:
                for name, path in tree_files(root).items():
                    data = path.read_bytes()
                    info = tarfile.TarInfo(top + "/" + name)
                    info.size = len(data)
                    info.mode = 0o755 if name == "bin/sovkit-stun" else 0o644
                    info.mtime = info.uid = info.gid = 0
                    tar.addfile(info, io.BytesIO(data))
    verify_archive(destination)


def verify_archive(path):
    """Read and verify an archive without extracting anything."""
    files = {}
    total = 0
    with tarfile.open(path, "r:gz") as tar:
        for member in tar:
            name = PurePosixPath(member.name)
            total += member.size
            if (not member.isfile() or name.is_absolute() or ".." in name.parts
                    or len(name.parts) < 2 or member.name in files
                    or "\\" in member.name or member.size > 32 * 1024 * 1024
                    or total > 100 * 1024 * 1024 or len(files) >= 512):
                raise ValueError("unsafe or oversized archive entry")
            files[member.name] = tar.extractfile(member).read()
    tops = {PurePosixPath(name).parts[0] for name in files}
    if len(tops) != 1:
        raise ValueError("expected a single package directory")
    top = tops.pop()
    checksum_name = top + "/SHA256SUMS"
    checks = {}
    for row in files[checksum_name].decode().splitlines():
        expected, relative = row.split("  ", 1)
        if relative in checks:
            raise ValueError("duplicate checksum entry")
        checks[relative] = expected
    expected_names = {top + "/" + name for name in checks}
    if expected_names != set(files) - {checksum_name}:
        raise ValueError("checksum inventory mismatch")
    for relative, expected in checks.items():
        if digest(files[top + "/" + relative]) != expected:
            raise ValueError("checksum mismatch: " + relative)
    manifest = json.loads(files[top + "/manifest.json"])
    if manifest["channel"] != "development-preview" or manifest["published"]:
        raise ValueError("this tool only handles unpublished development previews")
    return manifest


def run(command, log):
    print("+ " + " ".join(map(str, command)), flush=True)
    with log.open("a") as stream:
        stream.write("+ " + " ".join(map(str, command)) + "\n")
        stream.flush()
        subprocess.run(list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT,
                       check=True, timeout=300,
                       env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"))


def checked_libuv_notices(root, version, supplied):
    # Reviewed full notices, not just the top-level MIT license. A different
    # libuv version requires its own notice review before binary publication.
    if version != "1.52.1":
        raise ValueError("binary packaging needs reviewed notices for libuv " + version)
    reviewed = (root / "packaging/licenses/libuv-1.52.1.txt").read_bytes()
    if supplied.read_bytes() != reviewed:
        raise ValueError("use the complete reviewed packaging/licenses/libuv-1.52.1.txt")
    return reviewed


def build_preview(root, output, source_only, libuv_prefix, libuv_license):
    if not source_only:
        if platform.system() != "Darwin" or platform.machine() not in {"arm64", "x86_64"}:
            raise ValueError("binary preview currently supports native macOS only; Linux is deferred")
        if not libuv_prefix or not libuv_license or not libuv_license.is_file():
            raise ValueError("binary preview requires --libuv-prefix and --libuv-license")
    output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="preview-", dir=output))
    print("Work directory: " + str(work), flush=True)
    source = work / "source"
    entries = snapshot(root, source)
    source_hash = digest(json.dumps(entries, sort_keys=True, separators=(",", ":")).encode())
    text = (source / "CMakeLists.txt").read_text()
    version = re.search(r"project\(sovkit_stun VERSION ([0-9]+\.[0-9]+\.[0-9]+)", text).group(1)
    log = work / "validation.log"
    run([sys.executable, source / "tools/verify_import.py"], log)
    common = {
        "format_version": 1, "project": "sovkit-stun", "version": version,
        "channel": "development-preview", "published": False,
        "source_inventory_sha256": source_hash, "source_files": entries,
        "git_history_included": False,
        "linux_validation": "not-established-by-this-package",
        "windows_validation": "not-run",
    }
    outputs = []
    if not source_only:
        cmake = shutil.which("cmake")
        compiler = shutil.which("c++")
        if not cmake or not compiler:
            raise ValueError("cmake and c++ must be installed")
        build = work / "build"
        prefix = str(libuv_prefix.resolve())
        architecture = platform.machine()
        run([cmake, "-S", source, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
             "-DCMAKE_CXX_COMPILER=" + compiler, "-DCMAKE_PREFIX_PATH=" + prefix,
             "-DCMAKE_OSX_ARCHITECTURES=" + architecture, "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0",
             "-DCMAKE_INSTALL_LIBDIR=lib", "-DCMAKE_INSTALL_BINDIR=bin",
             "-DCMAKE_INSTALL_INCLUDEDIR=include", "-DCMAKE_INSTALL_DATADIR=share",
             "-DBUILD_TESTING=ON", "-DSTUN_SANITIZERS=OFF"], log)
        run([cmake, "--build", build, "--parallel", "2"], log)
        ctest = str(Path(cmake).with_name("ctest"))
        tests = json.loads(subprocess.check_output(
            [ctest, "--test-dir", str(build), "--show-only=json-v1"], text=True))
        names = {row["name"] for row in tests["tests"]}
        required = {"codec", "source_manifest", "udp_service", "installed_sdk", "package_checks"}
        if not required.issubset(names):
            raise ValueError("required tests are missing")
        run([ctest, "--test-dir", build, "--output-on-failure", "--no-tests=error"], log)
        stage = work / "stage"
        run([cmake, "--install", build, "--prefix", stage], log)
        executable = stage / "bin/sovkit-stun"
        version_line = subprocess.check_output([str(executable), "--version"], text=True).strip()
        match = re.fullmatch(r"sovkit-stun " + re.escape(version) + r" \(libuv ([0-9.]+)\)", version_line)
        if not match:
            raise ValueError("binary version does not match source")
        linked = subprocess.check_output(["otool", "-L", str(executable)], text=True)
        libraries = [row.strip().split(" (", 1)[0] for row in linked.splitlines()[1:]]
        if not libraries or any(not name.startswith(("/usr/lib/", "/System/Library/")) for name in libraries):
            raise ValueError("runtime has non-system dylibs; use a static libuv build")
        license_bytes = checked_libuv_notices(source, match[1], libuv_license)
        license_dir = stage / "share/sovkit-stun/licenses"
        license_dir.mkdir()
        (license_dir / "libuv-LICENSE.txt").write_bytes(license_bytes)
        (stage / "share/sovkit-stun/README.md").write_bytes((source / "README.md").read_bytes())
        manifest = dict(common, kind="native-runtime-and-static-sdk",
                        target={"os": "macos", "arch": architecture, "minimum_os": "13.0",
                                "older_os_runtime_tested": False},
                        build_type="Release", signature="not-notarized-development-artifact",
                        libuv={"version": match[1], "runtime_linkage": "static",
                               "sdk_requires_external_libuv": True,
                               "license_sha256": digest(license_bytes)},
                        validation={"ctest": "passed", "tests": sorted(names),
                                    "installed_sdk_relocation": "passed",
                                    "validation_log_sha256": digest(log.read_bytes())})
        describe(stage, manifest)
        label = f"sovkit-stun-{version}-macos-{architecture}-preview-{source_hash[:12]}"
        path = work / (label + ".tar.gz")
        archive(stage, path, label)
        outputs.append(path)
    verify_snapshot(source, entries)
    describe(source, dict(common, kind="source", validation={"import_manifest": "passed"}))
    label = f"sovkit-stun-{version}-source-preview-{source_hash[:12]}"
    path = work / (label + ".tar.gz")
    archive(source, path, label)
    outputs.append(path)
    (work / "SHA256SUMS").write_text("".join(
        f"{digest(path.read_bytes())}  {path.name}\n" for path in sorted(outputs)))
    for path in outputs:
        print("Created and verified: " + str(path), flush=True)
    return work


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-only", action="store_true")
    parser.add_argument("--libuv-prefix", type=Path)
    parser.add_argument("--libuv-license", type=Path)
    parser.add_argument("--output-dir", type=Path, default=root / ".build/packages")
    args = parser.parse_args()
    try:
        build_preview(root, args.output_dir.resolve(), args.source_only,
                      args.libuv_prefix, args.libuv_license)
    except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        print("Packaging failed; partial work retained for diagnosis: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
