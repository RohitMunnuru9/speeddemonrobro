# D: Traverse

Edges are read from mapped input and counted in both directions, then scattered
into forward and reverse CSR arrays. No route index is stored in the image.

Queries use bidirectional Dijkstra with uint64 distances and monotone radix heaps.
Four workers claim queries from an atomic counter. Each worker reuses distance
and generation arrays. The smaller search frontier is expanded; termination
requires the two heap minima to reach the best complete path found. Results
are written after joining workers, in original query order.

A query whose target has no incoming edges, or whose source has no outgoing
edges, is unreachable unless source equals target. These graph facts are computed
from the current input. Parallel edges, zero weights and duplicate queries need
no special assumptions. SD_THREADS may select 1 to 12 workers for experiments.

No external dependencies. Optional arguments select input and output directories.

With another 24 hours: reorder vertices to improve cache locality and parallelize
graph construction. Measure complete time and memory, including preprocessing.
