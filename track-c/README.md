# C: Join

A byte parser tracks quoted fields and doubled quote escapes. Only a BOM at a
record boundary is removed. Embedded newlines and trailing spaces remain data.
Regions map to compact country IDs through an integer array; unknown regions
go to the unmatched group. Money is parsed and formatted in signed integer cents.

Country aggregates hold 64-bit sums and exact sets of unescaped names. Notes
affect only their unescaped byte lengths. Empty quoted and unquoted names are
the same value. Countries without records are omitted; output keys sort bytewise.

No external dependencies. Optional arguments select input and output directories.

With another 24 hours: intern names globally and track their country memberships,
avoid allocating note strings, and compare mapped input against the current
buffered input. Correctness on the official CSV edge cases is a required gate.
