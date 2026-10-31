# A: Ingest

Twelve workers parse disjoint line-aligned ranges of read-only mapped input.
The fixed UTC span maps into 72 hour buckets. Endpoints are exact string views,
so parsing creates no endpoint strings per record. Each worker stores compact
latency/group pairs and counts; the counts assign disjoint ranges in one shared
latency array. A scatter phase groups values without a second parsing pass.
Counter arrays are allocated lazily for each hour seen by a worker, with no
assumption about input time order. Global endpoint names are interned before
sorting, and endpoint references and prefix ranges are prepared in parallel.

Small groups are sorted; larger groups use exact selection at the three nearest
ranks. Output hours are formatted independently into buffers and joined in order.
Endpoints sort bytewise. Duplicate records count separately; all ranks and rates
use integer arithmetic.

No external dependencies. Build and run the Dockerfile; optional command line
arguments select the input and output directories for local verification.

With another 24 hours: compare compact hash tables and vectorized parsing, then
reduce temporary buffer memory. Retain changes only when board K improves.
