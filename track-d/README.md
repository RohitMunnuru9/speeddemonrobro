# D: Traverse

Edges are parsed into compact arrays, counted by source, and scattered into CSR
adjacency storage. The input and temporary construction arrays are released
before searches begin. No route index is stored in the submitted image.

Queries use Dijkstra with uint64 distances and a binary heap. Up to six workers
claim independent queries from an atomic counter. Each worker reuses distance
and generation arrays, avoiding an O(nodes) reset before every query. Results
are written only after joining workers, in original query order.

A query whose target has no incoming edges, or whose source has no outgoing
edges, is unreachable unless source equals target. These graph facts are computed
from the current input. Parallel edges, zero weights and duplicate queries need
no special assumptions. SD_THREADS may select 1 to 12 workers for experiments.

No external dependencies. Optional arguments select input and output directories.

With another 24 hours: compare bidirectional search, radix heaps, and worker
counts using complete end-to-end time and memory, including graph construction.
