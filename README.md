# Poker Engine

A small foundation for a Texas Hold'em poker engine focused on equity calculation.
The near-term plan is to start with exact enumeration when the game state is small,
switch to Monte Carlo simulation when the search space becomes too large, and later
combine both strategies in a hybrid engine. Python will handle orchestration,
analysis, experiments, and visualization. C++ will be used for the computationally
intensive parts if benchmarks show a meaningful advantage.

## Planned Architecture

- `python/poker/` will hold orchestration code, experiment helpers, and analysis tools.
- `cpp/` will contain the performance-sensitive engine and benchmarks.
- `benchmarks/` will compare Python and C++ implementations of representative workloads.
- `tests/` and `python/tests/` are reserved for future test coverage.
- `notebooks/` is reserved for analysis and visualization notebooks.

## Current Structure

```text
poker/
├── cpp/
│   ├── include/
│   └── src/
├── python/
│   ├── poker/
│   └── tests/
├── benchmarks/
├── notebooks/
├── tests/
├── .gitignore
├── README.md
└── requirements.txt
```

## Python Environment

Create a virtual environment from the repository root:

```powershell
python -m venv .venv
```

Activate it in PowerShell:

```powershell
.\.venv\Scripts\Activate.ps1
```

The initial benchmark uses only the Python standard library, so there is nothing to
install yet.

## C++ Build With CMake

Configure the C++ project from the repository root:

```powershell
cmake -S cpp -B cpp/build
```

Build the benchmark:

```powershell
cmake --build cpp/build
```

If you are using a multi-config generator such as Visual Studio, build the Release
configuration explicitly:

```powershell
cmake --build cpp/build --config Release
```

On this machine, validation used the MinGW toolchain installed under
`C:\Program Files\CodeBlocks\MinGW\bin`. If your compiler is already on PATH,
the shorter commands above are enough.

## Run The Benchmarks

Run the Python benchmark and point it at the compiled C++ executable:

```powershell
python benchmarks\benchmark.py --iterations 10000000 --cpp-executable cpp\build\poker_benchmark.exe
```

If your CMake generator places the executable in a configuration subfolder, adjust the
path accordingly, for example:

```powershell
python benchmarks\benchmark.py --iterations 10000000 --cpp-executable cpp\build\Release\poker_benchmark.exe
```

Run the C++ benchmark directly:

```powershell
cpp\build\poker_benchmark.exe --iterations 10000000
```

On some generators the executable may live under a configuration directory such as
`cpp\build\Release\poker_benchmark.exe`.

## Notes

- The first benchmark is intentionally simple: a repeated integer workload with
  pseudo-random number generation, arithmetic, and accumulation.
- The Python and C++ implementations use the same constants and update rules so the
  work is equivalent.
- When building with MinGW on Windows, the CMake target copies the required runtime
  DLLs next to the executable so it can run without extra PATH setup.
- The next major step is to implement the poker hand equity evaluator and then use
  this benchmark framework to decide where C++ acceleration is worth adding.
