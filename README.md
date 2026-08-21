# Poker Engine

A compact Texas Hold'em engine focused on exact equity, Monte Carlo equity, and the math-only
decision layers built on top of them.

The project is intentionally scoped around concrete card combinations and deterministic math.
It does not attempt GTO solving, CFR, opponent modeling, betting trees, or strategic raise EV.

## Architecture

```text
Cards / Deck
-> Hand evaluation
-> HandRange
-> Exact + Monte Carlo equity
-> GameState
-> BettingState
-> EV
-> Decision Engine
```

## What Is Implemented

- Card and deck primitives
- Hold'em hand evaluation
- Concrete 1326-combo hand ranges
- Range parsing and membership checks
- Specific-hand equity
- Specific-hand vs range equity
- Range-vs-range equity
- Mixed multi-opponent equity through `GameState`
- Exact enumeration and Monte Carlo simulation
- Pot odds and break-even equity
- Call EV and fold EV
- Basic decision evaluation for supported actions

## Range Syntax

Supported notation in `HandRange::parse`:

- Pocket pairs: `AA`, `KK`, `QQ`, `22`, `QQ+`, `22+`
- Suited hands: `AKs`, `A5s`, `QJs`, `A5s+`
- Offsuit hands: `AKo`, `KQo`
- Any-suit hands: `AK`, `KQ`, `76`
- Comma-separated unions with optional whitespace

Not supported yet:

- Bounded ranges using `-`
- Offsuit plus notation
- Any-suit plus notation

## Equity Model

Exact equity enumerates concrete legal hole-card combinations and remaining board runouts.
Monte Carlo samples from the same state space using a seedable RNG.

`EquityResult` reports:

- `win_probability`
- `tie_probability`
- `loss_probability`
- `equity`
- `simulations`
- `evaluated_states`

## GameState

`GameState` packages a real decision spot:

- `street`
- `hero`
- `betting`
- `opponents`
- `player_count`

`Opponent` is a variant of `HandCombo`, `HandRange`, or `RandomOpponent`.
Specific opponents are fixed cards. Range opponents are concrete ranges. Random opponents use
all currently legal two-card combinations.

Mixed multi-opponent equity is supported. The engine rejects states where a known opponent
collides with the hero or board, or where a range has no legal concrete combinations left.

## Betting, EV, and Decision

`BettingState` models the current decision structure:

- `current_pot`
- `call_amount`
- `hero_stack`
- `minimum_raise_amount`
- `check_allowed`

`EV` helpers compute:

- pot odds
- break-even equity
- call EV
- fold EV

The first decision layer only evaluates actions whose EV is mathematically known from the
current spot. Fold and call are supported. Check, bet, raise, and all-in are represented
explicitly when legal, but remain unsupported until fold-equity and future-state modeling are
added.

## Build

Configure and build from the repository root:

```powershell
cmake -S cpp -B cpp/build
cmake --build cpp/build
```

On this machine, MinGW validation used `C:\Program Files\CodeBlocks\MinGW\bin`. If your
compiler is already on `PATH`, the commands above are enough.

For MinGW on Windows, the CMake build copies the runtime DLLs next to the executables so they
can run without extra `PATH` setup.

## CLI

The main executable is `cpp\build\poker.exe`.

Supported commands:

- `equity`
- `pot-odds`
- `ev`

Examples:

```powershell
cpp\build\poker.exe equity As Ks --villain-range "QQ+, AJs+, KQs" --board Qh 7c 2s --method exact
cpp\build\poker.exe equity --hero-range "QQ+, AKs" --villain-range "JJ+, AQs+" --board Qh 7c 2s --method montecarlo --simulations 1000000 --seed 12345
cpp\build\poker.exe pot-odds --pot 100 --call 50
cpp\build\poker.exe ev --pot 100 --call 50 --equity 0.40
```

Current equity CLI modes:

- specific hand vs specific hand, using positional hero cards and `--opponents`
- specific hand vs range, using positional hero cards and `--villain-range`
- range vs range, using `--hero-range` and `--villain-range`

Run `cpp\build\poker.exe equity --help` for the full syntax.

## Benchmarks

Two benchmark executables are built with the engine:

- `cpp\build\poker_exact_benchmark.exe`
- `cpp\build\poker_equity_benchmark.exe`

They include a mixed multi-opponent scenario in addition to the existing fixed-opponent cases.

## Tests

Run the C++ test binary through CTest:

```powershell
ctest --test-dir cpp/build --output-on-failure
```

## Current Limitations

- No GTO solver
- No CFR
- No opponent behavioral model
- No fold-equity model for raises
- No full betting tree
- No weighted opponent ranges
- No advanced bet sizing strategy