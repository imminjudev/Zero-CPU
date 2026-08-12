# ZeroFS Core

ZeroFS is the small virtual filesystem used by the protected Zero-CPU platform.

Its purpose is to provide a deterministic storage model for later protected
filesystem syscalls, Zero Web assets, examples, tests, and Studio tooling without
turning Zero-CPU into an ext4/Linux compatibility project.

## Architectural Boundary

```text
Guest process memory (4 KiB address space)
        !=
ZeroFS storage
```

ZeroFS data is host-managed platform state. File contents are not required to fit
inside the guest's 4 KiB memory image. Later protected syscalls copy bounded
chunks between guest buffers and ZeroFS.

Current layering:

```text
ZeroFS core                    implemented in v1.8-C
        ↓
Protected filesystem service   implemented in v1.8-D
        ↓
INT 80 ABI                     services 30..32
        ↓
User process / Zero Web
```

## Default Layout

Every new `ZeroFS` instance contains:

```text
/
/bin
/www
/data
```

Intended roles:

```text
/bin   packaged Zero-CPU programs/runtime assets
/www   Zero Web static assets
/data  writable application data
```

These are conventions, not host operating-system directories.

## Path Rules

ZeroFS uses canonical POSIX-like virtual paths.

```text
absolute paths only
'/' separator only
repeated '/' is collapsed
'.' components are removed
'..' traversal is rejected
```

Examples:

```text
/www//site/./index.html  -> /www/site/index.html
www/index.html           -> InvalidPath
/www/../data             -> InvalidPath
```

Rejecting traversal here keeps later web-package and syscall boundaries easier to
verify.

## Core Operations

The v1.8-C core provides:

```text
exists
stat
createDirectory
createDirectories
putFile
readFile(offset, count)
writeFile(offset, data)
listDirectory
file/directory/byte counters
```

`putFile` creates or replaces a file when its parent directory exists.

`writeFile` grows an existing file and zero-fills a gap when the write offset is
past the current end of file.

Directory listing is deterministic and returns only immediate child names.

## Typed Errors

Core failures use `ZeroFSException` with `ZeroFSError`:

```text
InvalidPath
NotFound
AlreadyExists
NotDirectory
IsDirectory
InvalidOffset
```

The next protected-filesystem layer will translate these core errors into stable
protected ABI status values rather than exposing C++ exceptions to guest code.

## Explicit Non-Goals for v1.8-C

Not implemented yet:

```text
file descriptors / open handles
guest create/mkdir syscalls
permissions/users
symlinks
rename/delete
host directory mounting
persistence/journaling
network filesystems
```

This is deliberate. The goal is a small, testable storage substrate with clear
semantics, not a general-purpose operating-system filesystem.

## Verification

Focused test target:

```bat
.\build\Debug\zero_fs_test.exe
```

Coverage includes:

```text
default /bin /www /data layout
path normalization and traversal rejection
recursive directory creation
file replacement and ranged reads
sparse write growth/zero fill
deterministic immediate-child listing
typed error behavior
```

The full regression suite also includes this test.

<!-- Patch: v1.8-zero-fs-core-r1 -->

## Protected Filesystem Syscalls

```text
30  FS_STAT
31  FS_READ
32  FS_WRITE
```

`R2` points to a qword request block in User data memory. Common fields are
`+0 path pointer` and `+8 path length`. Read/write additionally use `+16 file
offset`, `+24 guest buffer pointer`, and `+32 transfer count`. `FS_STAT` writes
node type to `+16` and size to `+24`.

The Kernel validates the request block, path bytes, and transfer buffer against
the User data window before accessing them. ZeroFS core failures are translated
to stable protected ABI status values.

The ABI is intentionally minimal and path-based. Guest create/mkdir syscalls and
file descriptors are not part of v1.8-D; showcase files are preloaded through
the host-side ZeroFS core API.

<!-- Patch: v1.8-protected-filesystem-syscalls-r2 -->
