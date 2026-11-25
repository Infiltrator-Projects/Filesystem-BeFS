# Filesystem-BeFS

Independent BeOS filesystem provider for InfiltratorOS. The target is one native C/C++ engine and one distributable `BEFS.so`.

**Status: documentation only; source extraction pending.** Editable upstream-derived Linux implementation remains in [Filesystems](https://github.com/Infiltrator-Projects/Filesystems/tree/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/befs). No independent driver, build or runtime qualification exists.

## Common V2

Use only `infiltrator_filesystem_api_v2` from [Libraries](https://github.com/Infiltrator-Projects/Libraries). The canonical engine owns byte order, inodes, B+trees, datastream direct/indirect blocks and attributes. Hosted and freestanding profiles share format semantics. No FUSE, separate per-format kernel/EFI driver or legacy ABI.

## Build and install

Planned: `/System/Filesystems/BEFS.so` and `/System/Resources/Documentation/Filesystem-BeFS.pdf`. Release assets: generic amd64 .deb, source-downloading CPU-native .run and GitHub-generated source ZIP. No installer or release exists yet.

See [migration status](MIGRATION_STATUS.md) and [engineering philosophy](docs/PHILOSOPHY.md).
