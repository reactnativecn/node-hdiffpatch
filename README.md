# node-hdiffpatch

Create patch buffer with origin buffer with [HDiffPatch](https://github.com/sisong/HDiffPatch)

Patch compatible with HDiffPatch -SD

## Installation

```bash
npm install --save node-hdiffpatch
```

## Usage

### diff(originBuf, newBuf)

Compare two buffers and return a new hdiffpatch patch as return value.

### diffStream(oldPath, newPath, outDiffPath[, cb])

Create diff file by streaming file paths (low memory). In sync mode returns
`outDiffPath`. In async mode, callback signature is `(err, outDiffPath)`.
The diff format is the streaming compressed format; use `patchStream` to apply it.

### patchStream(oldPath, diffPath, outNewPath[, cb])

Apply diff file to old file and write new file by streaming. In sync mode
returns `outNewPath`. In async mode, callback signature is `(err, outNewPath)`.
