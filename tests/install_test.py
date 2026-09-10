#!/usr/bin/env python3
"""Install to a fresh prefix, move it, and build a host without source includes."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def check_sdk(cmake, build, config, compiler, dependency_prefix):
    with tempfile.TemporaryDirectory(prefix="sdk-relocation-", dir=build) as directory:
        work = Path(directory)
        original = work / "initial-prefix"
        relocated = work / "relocated prefix"
        subprocess.run([cmake, "--install", str(build), "--config", config,
                        "--prefix", str(original)], check=True)
        original.rename(relocated)
        for path in relocated.rglob("*.cmake"):
            content = path.read_text()
            if str(build) in content or str(original) in content:
                raise AssertionError("producer path leaked into installed CMake config")
        example = relocated / "share/sovkit-stun/examples/sdk"
        consumer = work / "consumer-build"
        prefixes = str(relocated) + (";" + dependency_prefix if dependency_prefix else "")
        subprocess.run([cmake, "-S", str(example), "-B", str(consumer),
                        "-DCMAKE_BUILD_TYPE=" + config, "-DCMAKE_CXX_COMPILER=" + compiler,
                        "-DCMAKE_PREFIX_PATH=" + prefixes,
                        "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
                        "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF"], check=True)
        subprocess.run([cmake, "--build", str(consumer), "--config", config], check=True)
        executable = consumer / "stun_host"
        subprocess.run([str(executable)], check=True)
        print("SDK relocation and independent consumer passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--dependency-prefix", default="")
    args = parser.parse_args()
    check_sdk(args.cmake, args.build.resolve(), args.config, args.compiler, args.dependency_prefix)
