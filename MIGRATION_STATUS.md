# BeFS Common V2 migration status

## Source and ownership

Source remains at [the pinned central tree](https://github.com/Infiltrator-Projects/Filesystems/tree/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/befs); original Linux v6.12.107-derived notices and licences must be preserved. Extract and adapt existing code, not replace it with empty stubs. New project-authored code uses GPL-3.0-or-later; inherited licensing remains intact. No source was transferred by this documentation commit.

## Qualification plan

1. Establish exact supported on-disk variants and validate geometry and feature flags; implement bounded CORE probe.
2. Implement CORE mount, stat, list and read plus VOLUME query through the same native engine: byte order, inodes, B+trees, datastream direct/indirect blocks and attributes.
3. Independently qualify independently produced endian variants, malformed trees and out-of-range datastreams. Compare payloads and unchanged-image hashes; inject truncation, corruption and I/O faults.
4. Expose mutation, format, check/repair and DEFRAG map/plan/execute/verify/recover only after format-specific allocation, ordering and interruption/recovery tests. Reject unsupported operations before writing.
5. Validate exact packaged Common V2 ABI, hosted and import-free freestanding profiles, then actual BOOT and kernel consumer integration independently.

All stages are pending in this repository. No capabilities or release are claimed. The [design and qualification reference](docs/BeFS.pdf) is available; the operational manual and packaging remain pending until an actual provider is qualified.
