# Engineering Philosophy

Filesystem-BeFS adopts the [Infiltrator Engineering Philosophy](https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/PHILOSOPHY.md). Engineering excellence, reliability, disciplined simplicity and postgraduate-level reasoning govern source and documentation.

## Ownership and ABI

One format, one canonical native C/C++ engine, one distributable BEFS.so. Common owns the [V2 ABI](https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/FILESYSTEM_COMMON_ABI_V2.md). BOOT, Kernel, Files, Disks, Setup, Test Media and Defragmenter consume it; no legacy ABI, FUSE, duplicate format parser or separate per-format driver. Hosted and freestanding profiles require independent validation.

## Integrity and failure

Bound every offset, length and allocation; validate media geometry, feature flags and metadata before use. Explicitly specify locking, transaction ordering, durability and recovery. Unsupported or unsafe operations fail closed without changing media. Preserve inherited source and attribution.

## Evidence and craftsmanship

Use independent media, malformed fixtures, fault injection and exact packaged-binary tests. Never equate compilation or ABI recognition with data integrity, write safety or bootability. Use single main branch; publish only generic amd64 .deb, source-downloading CPU-native .run and GitHub-generated source ZIP. No Python in new implementation or installer. Maintain concise Markdown and an accurate illustrated PDF manual as functionality becomes qualified.
