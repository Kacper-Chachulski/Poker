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
Exact enumeration is capped at 10,000,000 theoretical states; above that, the CLI automatically
falls back to Monte Carlo unless the user explicitly requests exact, in which case the request is
rejected with a clear error.

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
- `decision`
- `analyze`

Examples:

```powershell
cpp\build\poker.exe equity As Ks --villain-range "QQ+, AJs+, KQs" --board Qh 7c 2s --method exact
cpp\build\poker.exe equity --hero-range "QQ+, AKs" --villain-range "JJ+, AQs+" --board Qh 7c 2s --method montecarlo --simulations 1000000 --seed 12345
cpp\build\poker.exe pot-odds --pot 100 --call 50
cpp\build\poker.exe ev --pot 100 --call 50 --equity 0.40
cpp\build\poker.exe decision Ah Ts --board 4d 4h 7h Ks --opponents 1 --pot 5 --call 3 --stack 200
cpp\build\poker.exe analyze Ah Ts --board 4d 4h 7h Ks --opponents 1 --pot 5 --call 3 --stack 200 --method montecarlo --simulations 100000 --seed 12345
```

Current equity CLI modes:

- specific hand vs specific hand, using positional hero cards and `--opponents`
- specific hand vs range, using positional hero cards and `--villain-range`
- range vs range, using `--hero-range` and `--villain-range`

If `--method` is omitted, the CLI chooses exact when the theoretical state count is at or below
10,000,000 and Monte Carlo when it is above that limit. When the fallback happens, the output
includes a `Reason:` line explaining why exact was refused.

Run `cpp\build\poker.exe equity --help` for the full syntax.

The `decision` command combines equity, pot odds, EV, and the existing decision layer for a
current hand spot using the random-opponent model when no explicit ranges are supplied. It prints
the equity estimate, required equity, call/fold EV, and a recommendation.

The `analyze` command combines the same equity and decision data with pot odds in one report. It
prints the win/tie/loss split, equity, pot odds, call EV, fold EV, and the recommendation for the
current spot.

This is a heuristic v1 betting/raising strategy, not a GTO solver. Bet/raise recommendations use
simplified equity and sizing rules and do not yet model opponent folding probabilities, ranges
changing in response to bets, future betting streets, or stack-aware opponent strategy.

Example:

```powershell
cpp\build\poker.exe decision Ah Ts --board 4d 4h 7h Ks --opponents 1 --pot 5 --call 3 --method montecarlo --simulations 100000 --seed 12345
```

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