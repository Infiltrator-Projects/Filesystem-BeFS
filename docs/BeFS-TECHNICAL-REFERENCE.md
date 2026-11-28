# BeFS: technical reference

## Design context and scope

The Be File System, written for BeOS by Dominic Giampaolo and Cyril Meurillon and introduced in BeOS R3, uses a 64-bit extent-based design with indexed attributes, B+tree directories and metadata journalling. Haiku maintains an open implementation of the format. This is unrelated to SCO UnixWare BFS. The goal is not just storing files: typed metadata and indexes support database-like queries over the namespace. Distinguish baseline BeOS format behaviour from Haiku extensions, and distinguish on-disk capability from the current provider's qualified operations.

## Superblock structure

The superblock is normally at volume byte 512; historical variants require checking alternative placement. All offsets here are relative to its start. The 32-byte volume name begins at 0. Magic 0x42465331 at 32, byte-order marker at 36, block size at 40, block shift at 44. Total and used blocks are 64-bit values at 48 and 56. Inode size at 64, second magic 0xDD121031 at 68, blocks per allocation group at 72, group shift at 76, number of groups at 80, flags at 84. Log block run at 88, 64-bit log start and end at 96 and 104, third magic 0x15B6830E at 112, root and index inode runs at 116 and 124. Check all three magics using the detected byte order.

## Geometry and block runs

BeFS supports both little- and big-endian volumes. Decode integers according to the volume marker, not the host. Each eight-byte block run contains a 32-bit allocation group, 16-bit group-relative start and 16-bit length. Translate its first block as group times 2 to the power ag_shift plus start. The length must remain within the allocation group and volume; zero-length and reserved runs need context-specific handling. With 4096-byte blocks and 8192 blocks per group, group 3, start 100, length 8 maps to blocks 24676 through 24683, beginning at byte 101072896. Never confuse block and byte positions.

## Allocation groups and bitmap

A volume is divided into allocation groups to distribute allocation and reduce search cost. A persistent allocation bitmap follows the reserved superblock area; each bit identifies whether one filesystem block is occupied. Derive its size from validated group geometry, including any bitmap rounding and padding prescribed by the particular implementation. Mark the superblock, bitmap, journal, root/index inodes and other reserved blocks before data allocation. Prefer contiguous runs and locality near related metadata. On free, update bitmap and used-block accounting together under the transaction mechanism. Independently reconstruct ownership to detect false-free sectors or overlaps.

## Inode field inventory

The packed inode starts with magic 0x3BBE0AD9 at byte 0 and its own eight-byte block run at 4. User and group IDs at 12 and 16, mode at 20, flags at 24, 64-bit creation and modification times at 28 and 36, parent run at 44, attributes run at 52, type at 60, inode size at 64 and reserved word at 68. The data union starts at 72, occupying 144 bytes; a short symbolic link may use this area instead of a data stream. Small-data records follow the 16-byte padding at offset 232. Validate the inode run and mode before interpreting the union.

## File data stream

Relative to the data-stream start, twelve direct runs occupy bytes 0 through 95. A 64-bit maximum direct range follows at 96, indirect run at 104, maximum indirect range at 112, double-indirect run at 120, maximum double-indirect range at 128 and logical file size at 136. The range boundaries are logical byte positions; verify their monotonicity and correspondence with actual allocation. The direct region is a sequence of runs, so subtract each run's byte capacity until the requested offset falls within one. Indirect runs contain additional block-run descriptors. Double-indirect mapping uses an intermediate level and format-defined grouping constraints; check the exact version before assuming unrestricted descriptor lengths.

## Worked file lookup

At 1024 bytes per block, assume the first direct run holds 3 blocks at physical block 200 and the second holds 5 at block 900. Logical offset 4600 lies 1528 bytes into the second run. The mapped block is 901 with intra-block offset 504; physical byte position is 923128. Only bytes below the inode's logical size are visible. For an offset outside the maximum direct range, subtract the direct range and walk the indirect allocation descriptors. Reject a descriptor that crosses a group boundary or an indirect run pointing into unrelated metadata. Limit traversal by the stream's capacity and verified bounds, not by arbitrary nonzero bytes.

## B+tree stream header

Directories and indexes are stored as B+tree data streams. At stream byte 0 is the B+tree header: magic 0x69F6C2E8 at 0, node size at 4, maximum depth at 8, key type at 12, 64-bit root node offset at 16, free node pointer at 24, maximum stream size at 32. Node pointers are byte offsets in that stream, not physical disk blocks. Key type 0 represents strings; numeric key types include signed and unsigned 32/64-bit integers and floating-point values. Use the ordering rules for the specified key type; do not compare arbitrary numeric keys lexicographically.

## Tree nodes and directory lookup

A packed node header has left sibling, right sibling and overflow addresses at offsets 0, 8 and 16, followed by 16-bit key count and total key bytes at 24 and 26. The variable key area follows the 28-byte header, then alignment padding, cumulative 16-bit key-end positions and 64-bit values. Verify alignment using the original implementation and ensure the tables fit within the node. Internal values are child stream offsets; leaf values have type-dependent semantics, with directory leaves identifying inodes. Descend sorted keys, following the overflow child for the upper range. Validate sibling links, strictly bounded depth and cycles. Duplicate-key index encodings need separate handling.

## Attributes and indices

Small attributes may be stored in the inode, using a 32-bit type, 16-bit name size and 16-bit data size followed by the name and data. Larger attributes live in separate attribute objects and streams reached through the inode's attributes run. Their names and types are part of the on-disk semantics. Index trees associate typed attribute values with matching inode identities; duplicate values can refer to multiple objects. Namespace changes and indexed-attribute edits require matching updates to the corresponding indexes. Preserve unsupported attributes rather than silently dropping them. Query results cannot be trusted if the index and inode disagree.

Preserve named attributes, index traversal and ordinary data streams as distinct ownership and interpretation paths. A directory lookup must not treat an attribute index as an ordinary directory tree.

## Journalling, writes and recovery

The reserved log run is a circular metadata transaction log, with recorded start and end positions. Clean and dirty states are encoded in the volume flags. Mutation must reserve data and metadata, complete required data writes, publish the logged metadata transaction in the format's required order, then checkpoint to its final locations and advance the log. Mounting a dirty volume requires validating and replaying committed records according to the actual implementation; the mere presence of a log is not proof that arbitrary partial writes are safe. Recovery must never overwrite uncommitted or invalid targets. Full log record and commit encoding requires additional source-level verification for writable compatibility.

## Implementation and qualification boundary

The original on-disk specification is independent of the Infiltrator provider. Its sole public interface is Common V2 (https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/FILESYSTEM_COMMON_ABI_V2.md). Discover operation capabilities at runtime; a format signature, source function or export is not proof of qualification. Preserve distinct unsupported-feature, corrupt-media, I/O and stale-handle results. Check host geometry, transfer bounds and overlapping sector updates; require exclusive unmounted ownership and durable flush before raw mutation. Test Media and Defragmenter use the same provider operations, not separate format parsers. Hosted and freestanding profiles, release packages, BOOT and kernel integration require separate exact-source evidence. Current implementation and installation status belongs to this repository's README and qualification records, not the format's historical chronology.

## Verification and references

Test both byte orders, boundary group sizes, twelve direct extents plus indirect storage, double-indirect limits, inline symlinks, attribute expansion, B+tree splitting and duplicate-index keys. Check bitmap accounting against all file and metadata ownership; compare directory enumeration and file reads against independent BeOS or Haiku images. Exercise dirty-log recovery and interrupted allocation with fault injection. Original reference: Dominic Giampaolo, Practical File System Design: The Be File System, https://www.haiku-os.org/legacy-docs/practical-file-system-design.pdf . Cross-check Haiku src/add-ons/kernel/file_systems/bfs and Linux https://github.com/torvalds/linux/tree/v6.12/fs/befs .

The original Linux header provides all three magic values, the inode and B-tree magic and the twelve-run datastream layout; cross-check both byte orders and group-boundary arithmetic.

Retained original source provenance: https://github.com/Infiltrator-Projects/Filesystems/blob/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/befs/linux/befs_fs_types.h (blob prefix 8019fde814).

### Common V2 physical allocation queries

The provider decodes allocation-group bitmap words in the disk byte order. ALLOCATION.MAP returns consecutive partition-relative byte ranges from the requested byte. The geometry beyond the filesystem is reserved. The last range ends at the end of the host partition. A complete scan of the test image reconciles with the mounted free-byte count.
