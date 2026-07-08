/// <reference types="node" />

export type BinaryLike = Buffer | ArrayBufferView;

export type DiffCallback = (err: Error | null, result?: Buffer) => void;
export type StreamCallback = (err: Error | null, outPath?: string) => void;

export interface NativeAddon {
  diff(oldBuf: BinaryLike, newBuf: BinaryLike): Buffer;
  diff(oldBuf: BinaryLike, newBuf: BinaryLike, cb: DiffCallback): void;
  patch(oldBuf: BinaryLike, diffBuf: BinaryLike): Buffer;
  patch(oldBuf: BinaryLike, diffBuf: BinaryLike, cb: DiffCallback): void;
  diffStream(oldPath: string, newPath: string, outDiffPath: string): string;
  diffStream(
    oldPath: string,
    newPath: string,
    outDiffPath: string,
    cb: StreamCallback
  ): void;
  patchStream(oldPath: string, diffPath: string, outNewPath: string): string;
  patchStream(
    oldPath: string,
    diffPath: string,
    outNewPath: string,
    cb: StreamCallback
  ): void;
  diffSingleStream(oldPath: string, newPath: string, outDiffPath: string): string;
  diffSingleStream(
    oldPath: string,
    newPath: string,
    outDiffPath: string,
    cb: StreamCallback,
  ): void;
  patchSingleStream(oldPath: string, diffPath: string, outNewPath: string): string;
  patchSingleStream(
    oldPath: string,
    diffPath: string,
    outNewPath: string,
    cb: StreamCallback
  ): void;
}

export const native: NativeAddon;

export function diff(oldBuf: BinaryLike, newBuf: BinaryLike): Buffer;
export function diff(
  oldBuf: BinaryLike,
  newBuf: BinaryLike,
  cb: DiffCallback
): void;

export function patch(oldBuf: BinaryLike, diffBuf: BinaryLike): Buffer;
export function patch(
  oldBuf: BinaryLike,
  diffBuf: BinaryLike,
  cb: DiffCallback
): void;

export function diffStream(
  oldPath: string,
  newPath: string,
  outDiffPath: string
): string;
export function diffStream(
  oldPath: string,
  newPath: string,
  outDiffPath: string,
  cb: StreamCallback
): void;

export function patchStream(
  oldPath: string,
  diffPath: string,
  outNewPath: string
): string;
export function patchStream(
  oldPath: string,
  diffPath: string,
  outNewPath: string,
  cb: StreamCallback
): void;
export function diffSingleStream(
  oldPath: string,
  newPath: string,
  outDiffPath: string,
): string;
export function diffSingleStream(
  oldPath: string,
  newPath: string,
  outDiffPath: string,
  cb: StreamCallback,
): void;
export function patchSingleStream(
  oldPath: string,
  diffPath: string,
  outNewPath: string
): string;
export function patchSingleStream(
  oldPath: string,
  diffPath: string,
  outNewPath: string,
  cb: StreamCallback
): void;

declare const hdiffpatch: {
  native: NativeAddon;
  diff: typeof diff;
  patch: typeof patch;
  diffStream: typeof diffStream;
  patchStream: typeof patchStream;
  diffSingleStream: typeof diffSingleStream;
  patchSingleStream: typeof patchSingleStream;
};

export default hdiffpatch;
