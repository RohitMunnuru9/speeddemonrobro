# A: Ingest

Workers parse line-aligned pieces in one pass. A fixed shared endpoint table
claims empty slots atomically, copies the complete endpoint, then publishes it.
Every hash match compares all endpoint bytes. Long keys and crowded probe chains
use an exact locked overflow map with stable owned storage. Names and IDs are
discovered from the current input; no catalogue or answer is stored in the image.

Records contain a full uint32 latency, exact endpoint ID and error bit. Workers
reserve disjoint chunks inside anonymous per-hour regions. Counts publish the
written rows. An hour's regions return to the OS after its counting scatter.
Percentiles use nearest-rank integer arithmetic, sorting for small groups and
exact selection for larger groups. Output names sort by unsigned byte order.

Completed pieces advance a contiguous input frontier. The first unparsed
piece's hour is a scheduling hint for finishing older hours. It is never a
correctness assumption: writers register before checking whether an hour is
closed, and a closer closes before waiting for registered writers. A later
record for a closed hour discards the attempt and retries with every hour held
open until parsing ends. The same output file is truncated and rewritten.
Arbitrarily reordered input remains exact, at the cost of the retry.

Completed-hour output is written in order. Workers join before mappings or
catalogues are destroyed, including after an output failure. The final image
contains only the executable. The offline C++20 build uses full-size generated
seed-17 compiler training; no training data or answers enter the final image.

The Rust version of this streaming architecture was also measured. The selected
C++ version had lower local runtime and fewer canonical source lines. Official
measurements, rejected trials and validation are in PERF.md.
