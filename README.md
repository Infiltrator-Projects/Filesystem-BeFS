# Filesystem-BeFS

## 1. Purpose and format scope

BeOS File System (BeFS), distinct from UnixWare BFS. This repository is the designated independent implementation home for InfiltratorOS. **Independent read-only Common V2 development provider. No installable release has been published.**

## 2. On-disk architecture and invariants

Interpret native byte order, inode records, B+trees, direct and indirect datastream block runs and extended attributes. Check run arithmetic against allocation geometry before reading. The initial reader implements the subset described below.

## 3. Common V2 implementation and capabilities

Native clean BeFS reader with both byte orders, validated allocation bitmap, direct, indirect and bounded double-indirect datastreams, native directory B+tree, label and metadata, and link reading. Hosted and import-free firmware targets share the engine.

## 4. Integrity, mutation, allocation and recovery

Mutation, formatting, check/repair and DEFRAG map/plan/execute/verify/recover require format-specific allocation and ordering invariants, exclusive writable host access and demonstrated interruption recovery. Until qualified, reject them before writing. Preserve native disk semantics and do not add a kernel or FUSE fallback as an alternative filesystem authority.

## 5. Build, installation and release

The target installation is `/System/Filesystems/BEFS.so` and `/System/Resources/Documentation/Filesystem-BeFS.pdf`. Neither file is currently distributed by this repository. Once the implementation qualifies, the intended release is a portable amd64 DEB and CPU-native source-first RUN, with GitHub's automatic source archive. With pinned Common headers under Libraries, use `make check` and `make carrier` for development. These are not installable releases.

## 6. Qualification and limitations

[Passing hosted, freestanding and sanitizer CI](https://github.com/Infiltrator-Projects/Filesystem-BeFS/actions/runs/45972578631) at `cc977bd41e14a90de1435140f8cfa674affa7a9d`. Constructed fixtures qualify the tested subset; real BeOS/Haiku media, journal replay, full cross-object ownership, indexed attributes, long-link oracle tests and signed boot remain outstanding. No mutation or production packaging qualified.

## 7. Technical references and provenance

- [Technical reference (PDF)](docs/BeFS.pdf) — original-format technical reference  and [PDF stream source](docs/BeFS.pdfstreams); not an operational manual.

- [Pinned central source baseline](https://github.com/Infiltrator-Projects/Filesystems/tree/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/befs)
- [Native design](native/filesystems/befs/DESIGN.md)
- [Migration and qualification plan](MIGRATION_STATUS.md)
- [Original-format reference source](docs/BeFS.pdfstreams) and [PDF](docs/BeFS.pdf)
- [Project philosophy](docs/PHILOSOPHY.md)
- [Common ABI](https://github.com/Infiltrator-Projects/Libraries)

Preserve inherited notices and licences on source extraction; new project-authored code uses GPL-3.0-or-later. The [technical reference](docs/BeFS-TECHNICAL-REFERENCE.md) expands original-format research. installation remains pending. Central code or test results are not evidence of an independent provider.
