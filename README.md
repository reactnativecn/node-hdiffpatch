# node-hdiffpatch

Create patch buffer with origin buffer with [HDiffPatch](https://github.com/sisong/HDiffPatch)

Patch compatible with HDiffPatch -SD

## Installation

```bash
npm install node-hdiffpatch
# or
bun add node-hdiffpatch
```

Prebuilt binaries are bundled for: `darwin-arm64`, `linux-x64`, and
`linux-arm64` (glibc). Other platforms (including `darwin-x64` and `win32-x64`)
are currently not supported by the published package.

## Development

Clone with submodules, then build and test:

```bash
git clone --recursive https://github.com/reactnativecn/node-hdiffpatch.git
cd node-hdiffpatch
bun install --ignore-scripts
bun run prebuild   # builds prebuilds/<platform>-<arch>/ for the current machine
bun run test       # run tests under Node
bun run test:bun   # run the same tests under the Bun runtime
```

## Usage

Binary inputs accept `Buffer`, any `TypedArray`, or `DataView`.

**Transactional file outputs.** Every file-based API (`diffStream`,
`diffSingleStream`, `diffWindow`, `patchStream`, `patchSingleStream`, and the
CLI) writes to a random temp file in the output directory, verifies the
result, then atomically renames it over the destination. On any failure the
temp file is removed and an existing destination file is left untouched.
This also makes in-place operation safe: the output path may be the same file
as an input (including via relative-path aliases, hard links, or symlinks) —
the input is only replaced after the operation fully succeeds.

**Patch resource limits.** A patch header declares its output size and
working-memory requirement; both come from untrusted data. All patch APIs
enforce finite caps and reject oversized declarations before allocating
memory or creating the output file. Defaults: `maxOutputBytes` is 2 GiB for
the in-memory `patch()` and 16 GiB for `patchStream()`/`patchSingleStream()`;
`maxWorkingMemoryBytes` is 256 MiB. Override per call via an options object:

```js
hdiffpatch.patch(oldBuf, diffBuf, { maxOutputBytes: 64 * 1024 * 1024 });
hdiffpatch.patchSingleStream(oldPath, diffPath, outPath, {
  maxOutputBytes: 512 * 1024 * 1024,
  maxWorkingMemoryBytes: 8 * 1024 * 1024,
});
```

These limits bound resource usage; they are not a substitute for verifying
patch provenance (signatures) and final-file hashes in your update system.

### diff(originBuf, newBuf[, options])

Compare two buffers and return a new hdiffpatch patch as return value.

All diff entry points accept an optional `options` object with
`compressionThreads: 1 | 2`. Two threads enable LZMA's internal parallel
match finder while preserving compression level 9 and the 8 MiB dictionary.
The default remains one thread. `diffWindow()` also accepts `windowSize` in
the options object; the legacy positional `windowSize` remains supported.

**Async behavior.** Callback-style calls run on Node's shared libuv thread
pool: one running task occupies one pool worker (plus up to two LZMA threads
with `compressionThreads: 2`), so many concurrent diffs can delay other
thread-pool consumers (`fs`, `zlib`, `crypto`, DNS). Queue or cap concurrency
for server-side batch workloads. The async buffer APIs copy their inputs
before returning, so mutating, transferring, or detaching the source buffers
after the call cannot affect the result.

### diffSingleStream(oldPath, newPath, outDiffPath[, cb])

Create a **single-format** (same wire format as `diff()`) patch by streaming
file paths with block matching — generation memory stays O(match block)
regardless of input size (100MB inputs use ~30MB RSS). The output applies with
`patch()`, `patchSingleStream()`, and any existing HDiffPatch single-format
apply side, so legacy clients need no changes. Patch size is larger than the
in-memory `diff()` for the same inputs; prefer `diff()` when memory allows.
In sync mode returns `outDiffPath`; async callback signature is
`(err, outDiffPath)`.

### diffWindow(oldPath, newPath, outDiffPath[, windowSize][, cb])

Create a **single-format** (same wire format as `diff()`) patch using window
mode: big covers come from streaming block matching, then residuals are
refined with suffix-string matching inside a sliding window (2MB) over the old
data. Match quality is close to the in-memory `diff()` while generation memory
stays at the streaming tier — usually a much smaller patch than
`diffSingleStream()` for the same inputs. The output applies with `patch()`,
`patchSingleStream()`, and any existing single-format apply side. In sync mode
returns `outDiffPath`; async callback signature is `(err, outDiffPath)`.
`windowSize` is the sliding-window byte size over the old data (default 2MB);
a larger window catches longer-distance content moves at roughly linear
additional memory.

### capabilities

`capabilities.diffStreamVerifiesOutput`,
`capabilities.diffSingleStreamVerifiesOutput`, and
`capabilities.diffWindowVerifiesOutput` are `true`. Each native diff function
applies and compares the generated patch before it returns, so orchestration
layers can avoid running a redundant second round-trip check.
`capabilities.maxCompressionThreads` is `2`.

### patchSingleStream(oldPath, diffPath, outNewPath[, options][, cb])

Apply a single-compressed hpatch payload created by `diff` or
`diffSingleStream` from files. This is the file-level apply path for the normal in-memory `diff`
format. In sync mode returns `outNewPath`. In async mode, callback signature is
`(err, outNewPath)`. `options` accepts `maxOutputBytes` and
`maxWorkingMemoryBytes` (see "Patch resource limits" above).

### diffStream(oldPath, newPath, outDiffPath[, cb])

Create diff file by streaming file paths (low memory). In sync mode returns
`outDiffPath`. In async mode, callback signature is `(err, outDiffPath)`.
The diff format is the streaming compressed format; use `patchStream` to apply it.

### patchStream(oldPath, diffPath, outNewPath[, options][, cb])

Apply diff file to old file and write new file by streaming. In sync mode
returns `outNewPath`. In async mode, callback signature is `(err, outNewPath)`.
`options` accepts `maxOutputBytes` and `maxWorkingMemoryBytes` (see "Patch
resource limits" above).

## CLI

After install, you can run:

```bash
hdp diff <oldFile> <newFile> <outDiff>
hdp patch <oldFile> <diffFile> <outNew>
```

Note: `hdp patch` auto-detects the diff format by its header, so it can apply
both streaming diffs created by `hdp diff` and single-compressed diffs created
by the in-memory `diff()` / streaming `diffSingleStream()` APIs.

## License

MIT. The prebuilt binaries statically include
[HDiffPatch](https://github.com/sisong/HDiffPatch) (MIT) and the
[LZMA SDK](https://github.com/sisong/lzma) (public domain).
