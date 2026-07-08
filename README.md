# node-hdiffpatch

Create patch buffer with origin buffer with [HDiffPatch](https://github.com/sisong/HDiffPatch)

Patch compatible with HDiffPatch -SD

## Installation

```bash
npm install node-hdiffpatch
# or
bun add node-hdiffpatch
```

Prebuilt binaries are bundled for: `darwin-arm64`, `darwin-x64`, `linux-x64`,
`linux-arm64` (glibc), and `win32-x64`. Other platforms are not supported by
the published package.

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

### diff(originBuf, newBuf)

Compare two buffers and return a new hdiffpatch patch as return value.

### diffSingleStream(oldPath, newPath, outDiffPath[, cb])

Create a **single-format** (same wire format as `diff()`) patch by streaming
file paths with block matching — generation memory stays O(match block)
regardless of input size (100MB inputs use ~30MB RSS). The output applies with
`patch()`, `patchSingleStream()`, and any existing HDiffPatch single-format
apply side, so legacy clients need no changes. Patch size is larger than the
in-memory `diff()` for the same inputs; prefer `diff()` when memory allows.
In sync mode returns `outDiffPath`; async callback signature is
`(err, outDiffPath)`.

### diffWindow(oldPath, newPath, outDiffPath[, cb])

Create a **single-format** (same wire format as `diff()`) patch using window
mode: big covers come from streaming block matching, then residuals are
refined with suffix-string matching inside a sliding window (2MB) over the old
data. Match quality is close to the in-memory `diff()` while generation memory
stays at the streaming tier — usually a much smaller patch than
`diffSingleStream()` for the same inputs. The output applies with `patch()`,
`patchSingleStream()`, and any existing single-format apply side. In sync mode
returns `outDiffPath`; async callback signature is `(err, outDiffPath)`.

### patchSingleStream(oldPath, diffPath, outNewPath[, cb])

Apply a single-compressed hpatch payload created by `diff` or
`diffSingleStream` from files. This is the file-level apply path for the normal in-memory `diff`
format. In sync mode returns `outNewPath`. In async mode, callback signature is
`(err, outNewPath)`.

### diffStream(oldPath, newPath, outDiffPath[, cb])

Create diff file by streaming file paths (low memory). In sync mode returns
`outDiffPath`. In async mode, callback signature is `(err, outDiffPath)`.
The diff format is the streaming compressed format; use `patchStream` to apply it.

### patchStream(oldPath, diffPath, outNewPath[, cb])

Apply diff file to old file and write new file by streaming. In sync mode
returns `outNewPath`. In async mode, callback signature is `(err, outNewPath)`.

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
