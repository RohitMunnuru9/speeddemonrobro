# D: Traverse

Edges are parsed once in parallel from mapped input into coarse forward vertex buckets.
Bucket ownership supplies high vertex bits; local vertex, full uint32 weight and
opposite vertex fit losslessly in temporary uint64 records. Estimated bucket
capacity has an exact growing overflow, so skewed inputs retain every edge.
Each bucket owner counts degrees and fills forward CSR without atomics or global
edge sorting. Reverse degrees are counted during parsing. Forward temporary
buckets are released before parallel transposition fills reverse CSR using
atomic output cursors. This avoids retaining both directions' temporary edges.
No route index is stored in the image.

CSR arcs normally occupy four bytes, with node/weight bit widths derived from
the current graph. A high-bit tag sends large weights or nodes to an exact
uint64 exception array. This preserves all weights, including UINT32_MAX, while
reducing adjacency traffic on ordinary roads. Vertex degrees have no inline limit.

Queries use bidirectional Dijkstra with uint64 distances and monotone radix heaps.
Twelve workers claim queries from an atomic counter. Each worker keeps an exact
flat hash table of visited nodes in each direction. Generation tags clear a
query logically, and the tables grow when needed. State therefore follows the
largest search handled by a worker rather than the full graph. Collisions compare
node IDs, and distances remain uint64 values. The side with fewer discovered nodes is expanded; termination
requires the two heap minima to reach the best complete path found. Results
are written after joining workers, in original query order.

Meeting candidates are checked on settled vertices. Every edge joining the two
settled sets is covered when its second endpoint settles, so looking up the
opposite state after every individual relaxation is unnecessary.

A query whose target has no incoming edges, or whose source has no outgoing
edges, is unreachable unless source equals target. These graph facts are computed
from the current input. Parallel edges, zero weights and duplicate queries need
no special assumptions. SD_THREADS may select 1 to 12 workers for experiments.

No external dependencies. Optional arguments select input and output directories.
The compiler profile uses full-size generated seed-17 data at build time; the
final image contains only the executable.

With another 24 hours: reduce graph-construction memory and improve query-state
cache locality. Measure complete time and memory, including preprocessing.
