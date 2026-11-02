# Vayu final attempt — November 2, 2026

The last experimental push was `17c357c`. Every official gate passed, but its
B and D changes failed to improve their retained best scores. They are reverted.

The final branch uses the exact runtime source from these verified versions:

| Track | Runtime source | Retained best machine points |
|---|---|---:|
| A | ee47856 / e71d607, Rust | 75.625792 |
| B | adb4712, C++ | 57.920065 |
| C | unchanged since 1589306, C++ | 72.076368 |
| D | 1589306 / d00f34a, C++ | 64.907190 |

The observed overall best-three machine score after the trial is 70.7270;
the board showed place 52 at 12:25 UTC. No 80-point overall result was achieved.
C's small new best came from another run of unchanged code and is not an
algorithmic gain. Official final judging is separate from the live board.

Rust D was implemented and measured, including an arena queue and a mirrored
SIMD-label variant. It did not establish a better optimized submission. Its
source is preserved under experiments/rust-d, with local measurements alongside
the official experimental receipt under experiments/measurements.

The final restore keeps proven code at the branch tip because the organizers'
final scoring snapshots the frozen branch tip, rather than automatically using
each track's historical best commit.
