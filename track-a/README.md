# A: Ingest

Twelve workers parse disjoint line-aligned ranges of read-only mapped input.
The fixed UTC span maps into 72 hour buckets. Endpoints are exact string views,
so parsing creates no endpoint strings per record. Each worker appends latencies
directly to its groups. Three values fit inline; larger groups grow an exact
array. Sparse 64-group pages are allocated only for encountered hour/endpoint
combinations. This makes no assumption about input time order. Global endpoint
names are interned before sorting, and endpoint references are prepared in parallel.

Small groups are sorted; larger groups use exact selection at the three nearest
ranks. Each formatting worker reuses one group-sized scratch buffer and frees
the completed hour's input groups. Output hours are buffered independently,
then written in order to one file without a second joined copy.
Endpoints sort bytewise. Duplicate records count separately; all ranks and rates
use integer arithmetic.

No external dependencies. Build and run the Dockerfile; optional command line
arguments select the input and output directories for local verification.
Compiler profile guidance comes from a full-size generated seed-17 dataset;
only the executable enters the final image.

With another 24 hours: compare compact hash tables and vectorized parsing, then
reduce temporary buffer memory. Retain changes only when board K improves.
