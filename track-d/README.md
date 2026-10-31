# D: Traverse

Edges are parsed in parallel from mapped input. Parallel stable radix ordering
by source/destination enables sequential writes into forward/reverse CSR arrays.
No route index is stored in the image.

Queries use bidirectional Dijkstra with uint64 distances and monotone radix heaps.
Twelve workers claim queries from an atomic counter. Each worker keeps an exact
flat hash table of visited nodes in each direction. Generation tags clear a
query logically, and the tables grow when needed. State therefore follows the
largest search handled by a worker rather than the full graph. Collisions compare
node IDs, and distances remain uint64 values. The smaller frontier is expanded; termination
requires the two heap minima to reach the best complete path found. Results
are written after joining workers, in original query order.

A query whose target has no incoming edges, or whose source has no outgoing
edges, is unreachable unless source equals target. These graph facts are computed
from the current input. Parallel edges, zero weights and duplicate queries need
no special assumptions. SD_THREADS may select 1 to 12 workers for experiments.

No external dependencies. Optional arguments select input and output directories.

With another 24 hours: reduce graph-construction memory and improve query-state
cache locality. Measure complete time and memory, including preprocessing.
