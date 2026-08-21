# Poker Engine

A small foundation for a Texas Hold'em poker engine focused on equity calculation
and the mathematical decision primitives that sit on top of it.
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

## Decision Math

The `poker` C++ library now exposes a small EV layer in `cpp/include/poker/ev.hpp`.
It is independent from the equity engine and accepts an already-calculated equity
value.

The pot convention is:

- `pot_before_call` is the amount already in the pot before Hero calls.
- if Hero calls `X`, the final pot is `pot_before_call + X`.
- call EV uses the net incremental value of calling, not the total pot size.

Formulas:

```text
required_equity = call_amount / (pot_before_call + call_amount)
call_ev = equity * pot_before_call - (1 - equity) * call_amount
fold_ev = 0
```

If `call_amount` is `0`, the break-even equity is `0` and the CLI reports pot odds as
`N/A` because there is no price to pay.

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

## C++ CLI Examples

The `poker` executable now supports specific-hand equity, specific-hand vs range,
and range vs range through explicit range flags.

Specific hand vs specific hand:

```powershell
cpp\build\poker.exe equity As Ks --opponents 1 --board Qh 7c 2s --method exact
```

Specific hand vs range:

```powershell
cpp\build\poker.exe equity As Ks --villain-range "QQ+, AJs+, KQs" --board Qh 7c 2s --method exact
```

Range vs range:

```powershell
cpp\build\poker.exe equity --hero-range "QQ+, AKs" --villain-range "JJ+, AQs+" --board Qh 7c 2s --method montecarlo --simulations 1000000 --seed 12345
```

Run `cpp\build\poker.exe equity --help` to print the full usage summary.

Pot odds:

```powershell
cpp\build\poker.exe pot-odds --pot 100 --call 50
```

EV comparison:

```powershell
cpp\build\poker.exe ev --pot 100 --call 50 --equity 0.40
```

Example output:

```text
Pot before call: 100.00
Call amount:      50.00
Final pot:       150.00
Required equity: 33.33%
Pot odds:        2.00:1
Equity:          40.00%
Call EV:         +10.00
Fold EV:         +0.00
Decision:        CALL
```

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
