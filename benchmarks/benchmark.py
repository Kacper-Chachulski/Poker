"""Compare the initial Python and C++ benchmark workloads."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_SOURCE_DIR = REPO_ROOT / "python"
sys.path.insert(0, str(PYTHON_SOURCE_DIR))

from poker.workload import DEFAULT_ITERATIONS, run_integer_simulation

CPP_ELAPSED_RE = re.compile(r"^elapsed_seconds=(?P<value>[0-9]+(?:\.[0-9]+)?)$")
CPP_RESULT_RE = re.compile(r"^result=(?P<value>[0-9]+)$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the Python and C++ integer benchmarks.")
    parser.add_argument("--iterations", type=int, default=DEFAULT_ITERATIONS)
    parser.add_argument("--cpp-executable", type=Path, required=False)
    return parser.parse_args()


def resolve_cpp_executable(explicit_path: Path | None) -> Path:
    if explicit_path is not None:
        if explicit_path.is_absolute():
            return explicit_path
        return (REPO_ROOT / explicit_path).resolve()

    candidates = [
        REPO_ROOT / "cpp" / "build" / "Release" / "poker_benchmark.exe",
        REPO_ROOT / "cpp" / "build" / "poker_benchmark.exe",
        REPO_ROOT / "cpp" / "build" / "Release" / "poker_benchmark",
        REPO_ROOT / "cpp" / "build" / "poker_benchmark",
    ]

    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()

    raise FileNotFoundError(
        "Could not find the C++ benchmark executable. Pass --cpp-executable explicitly."
    )


def parse_cpp_output(output: str) -> tuple[int, float]:
    result_value: int | None = None
    elapsed_value: float | None = None

    for line in output.splitlines():
        if result_value is None:
            result_match = CPP_RESULT_RE.match(line.strip())
            if result_match is not None:
                result_value = int(result_match.group("value"))
                continue
        if elapsed_value is None:
            elapsed_match = CPP_ELAPSED_RE.match(line.strip())
            if elapsed_match is not None:
                elapsed_value = float(elapsed_match.group("value"))

    if result_value is None or elapsed_value is None:
        raise ValueError(f"Could not parse C++ benchmark output:\n{output}")

    return result_value, elapsed_value


def main() -> int:
    args = parse_args()
    cpp_executable = resolve_cpp_executable(args.cpp_executable)

    python_start = time.perf_counter()
    python_result = run_integer_simulation(args.iterations)
    python_elapsed = time.perf_counter() - python_start

    cpp_process = subprocess.run(
        [str(cpp_executable), "--iterations", str(args.iterations)],
        check=True,
        capture_output=True,
        text=True,
    )
    cpp_result, cpp_elapsed = parse_cpp_output(cpp_process.stdout)

    if python_result != cpp_result:
        print("Workload results differ between Python and C++.", file=sys.stderr)
        print(f"Python result: {python_result}", file=sys.stderr)
        print(f"C++ result: {cpp_result}", file=sys.stderr)
        return 1

    speedup = python_elapsed / cpp_elapsed if cpp_elapsed > 0 else float("inf")

    print(f"iterations={args.iterations}")
    print(f"python_seconds={python_elapsed:.6f}")
    print(f"cxx_seconds={cpp_elapsed:.6f}")
    print(f"speedup={speedup:.2f}x")
    print(f"result={python_result}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
