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

### diffWithCovers(originBuf, newBuf, covers[, options])

Create a standard hdiffpatch patch while using caller-provided cover lines when
possible. By default, the supplied covers replace HDiffPatch's internal cover
selection.

Each cover is `{ oldPos, newPos, len }`. Values may be numbers, decimal strings,
or bigint values. The returned object is:

```js
{
  diff: Buffer,
  usedCovers: boolean,
  requestedCoverCount: number,
  nativeCoverCapacity: number,
  finalCoverCount: number,
  coverMode: 'replace' | 'merge' | 'native-coalesce',
  nativeCovers?: HpatchCover[],
  finalCovers?: HpatchCover[]
}
```

The `diff` buffer is still a normal hdiffpatch payload and can be applied with
`patch(originBuf, diff)` or an HDiffPatch-compatible apply side.

Set `options.mode` to `merge` to keep HDiffPatch's native covers and only add
caller covers in new-file ranges not already covered by native covers:

```js
const merged = hdiff.diffWithCovers(oldBuf, newBuf, covers, { mode: 'merge' });
```

Set `options.mode` to `native-coalesce` to keep HDiffPatch's native cover
selection but coalesce adjacent native covers that have the same old/new offset
delta across small gaps. This remains a standard hdiffpatch payload and is useful
as a costed post-processing experiment:

```js
const coalesced = hdiff.diffWithCovers(oldBuf, newBuf, [], {
  mode: 'native-coalesce',
});
```

Set `options.debugCovers` to `true` to include the native HDiffPatch cover list
and the final listener cover list in the return value. This is for diagnostics;
the default return shape avoids copying cover arrays.

### patchSingleStream(oldPath, diffPath, outNewPath[, cb])

Apply a single-compressed hpatch payload created by `diff` or `diffWithCovers`
from files. This is the file-level apply path for the normal in-memory `diff`
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
by the in-memory `diff()` / `diffWithCovers()` APIs.

## License

MIT. The prebuilt binaries statically include
[HDiffPatch](https://github.com/sisong/HDiffPatch) (MIT) and the
[LZMA SDK](https://github.com/sisong/lzma) (public domain).
