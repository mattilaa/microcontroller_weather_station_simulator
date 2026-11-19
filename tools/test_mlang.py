#!/usr/bin/env python3
"""Run host-native MLang regression tests for the firmware's portable code."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    project = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler-root", type=Path, default=project / "subprojects/mlang")
    args = parser.parse_args()
    compiler_root = args.compiler_root.resolve()
    build = compiler_root / "build"
    subprocess.run(["cmake", "--build", str(build), "--target", "mlang_std", "--parallel", "4"], check=True)
    env = os.environ.copy()
    env["MLANG_MODULE_PATH"] = str(project / "src")
    with tempfile.TemporaryDirectory(prefix="weather-mlang-tests-") as temporary:
        subprocess.run([str(build / "mlang"), "--tests", str(project / "tests/firmware_unit_tests.mla"),
                        "-L", str(build), "-lmlang_std"], cwd=temporary, env=env, check=True)


if __name__ == "__main__":
    main()
