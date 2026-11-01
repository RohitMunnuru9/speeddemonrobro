# D: Traverse

Edges are parsed once in parallel from mapped input into coarse vertex buckets.
Bucket ownership supplies high vertex bits; local vertex, full uint32 weight and
opposite vertex fit losslessly in temporary uint64 records. Estimated bucket
capacity has an exact growing overflow, so skewed inputs retain every edge.
Each bucket owner counts degrees and fills forward/reverse adjacency without atomics
or global edge sorting. Temporary directions are released as construction finishes.
No route index is stored in the image.

Arcs normally occupy four bytes, with node/weight bit widths derived from
the current graph. A high-bit tag sends large weights or nodes to an exact
uint64 exception array. This preserves all weights, including UINT32_MAX, while
reducing adjacency traffic on ordinary roads. The first three encoded arcs and
overflow range fit in one 16-byte node record. Higher degrees use exact compact
overflow; degrees are unrestricted. Overflow data is prefetched before processing
the inline arcs, avoiding a dependent adjacency read for most low-degree nodes.

Queries use bidirectional Dijkstra with uint64 distances and monotone radix heaps.
Twelve workers claim queries from an atomic counter. Each worker keeps an exact
flat hash table of visited nodes in each direction. An 8-KiB bit filter rejects
definite opposite-direction misses; every possible hit still checks the exact
node and uint64 distance. Generation tags clear a
query logically, and the tables grow when needed. State therefore follows the
largest search handled by a worker rather than the full graph. Collisions compare
node IDs. Ordinary labels pack a uint32 distance, node ID and generation into
one eight-byte entry. Distances above UINT32_MAX use an exact uint64 side array;
no path length is truncated. Generation wrap physically clears old entries.
The smaller frontier is expanded; termination
requires the two heap minima to reach the best complete path found. Results
are written after joining workers, in original query order.

A 64-bit occupancy mask identifies the next positive radix bucket. Redistributing
keys updates the mask; the monotone-heap ordering and full-width keys are unchanged.

Redistribution prefetches graph nodes and label slots for the next minimum key.
Workers then process up to sixteen entries already sharing that key, giving
memory reads time to arrive. Batches never advance the heap to a future key
before relaxing the current vertices. Distances, stale-entry checks, zero-weight
handling and the bidirectional stopping bound remain exact.

A second prefetch stage reads the current direction's label slots for inline
neighbors after the batch's graph records arrive. It changes memory timing,
not the set or order of relaxations.

A query whose target has no incoming edges, or whose source has no outgoing
edges, is unreachable unless source equals target. These graph facts are computed
from the current input. Parallel edges, zero weights and duplicate queries need
no special assumptions. SD_THREADS may select 1 to 12 workers for experiments.

No external dependencies. Optional arguments select input and output directories.
The compiler profile uses full-size generated seed-17 data at build time; the
final image contains only the executable.

With another 24 hours: reduce graph-construction memory and improve query-state
cache locality. Measure complete time and memory, including preprocessing.
