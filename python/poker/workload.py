"""Shared integer workload for the initial Python benchmark."""

from __future__ import annotations

DEFAULT_ITERATIONS = 10_000_000
DEFAULT_SEED = 0x12345678
UINT32_MASK = 0xFFFFFFFF
UINT64_MASK = 0xFFFFFFFFFFFFFFFF
LCG_MULTIPLIER = 1_664_525
LCG_INCREMENT = 1_013_904_223
MIX_MULTIPLIER = 2_654_435_761


def run_integer_simulation(iterations: int, seed: int = DEFAULT_SEED) -> int:
    """Run a deterministic integer workload with simple pseudo-random mixing."""

    state = seed & UINT32_MASK
    accumulator = 0

    for _ in range(iterations):
        state = (state * LCG_MULTIPLIER + LCG_INCREMENT) & UINT32_MASK
        value = state ^ (state >> 16)
        mixed = (value * MIX_MULTIPLIER) & UINT32_MASK
        accumulator = (accumulator + mixed + (accumulator << 1)) & UINT64_MASK
        accumulator ^= (value << 13) & UINT64_MASK
        accumulator &= UINT64_MASK

    return accumulator
