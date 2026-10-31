# A: Ingest

The input is parsed one record at a time. The fixed UTC span maps into 72 hour
buckets, so parsing seconds and fractional seconds would do no useful work.
Endpoints are interned by an exact string map. Each endpoint owns 72 groups of
uint32 latencies and an error counter. Methods do not affect the answer.

Each group's latencies are sorted and the three nearest ranks are read using
integer arithmetic. Endpoints are sorted bytewise and output is emitted in hour
order. Duplicate records count separately. The memory used by latency vectors
is proportional to the input record count, rather than the latency range.

No external dependencies. Build and run the Dockerfile; optional command line
arguments select the input and output directories for local verification.

With another 24 hours: compare selection against sorting for large groups,
remove repeated endpoint allocations, and test independent parsing workers.
Retain changes only when organizer measurements improve K.
