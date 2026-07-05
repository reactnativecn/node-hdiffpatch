/// <reference types="node" />

export type BinaryLike = Buffer | ArrayBufferView;

export type DiffCallback = (err: Error | null, result?: Buffer) => void;
export type StreamCallback = (err: Error | null, outPath?: string) => void;
export type HpatchCover = {
  oldPos: number | string | bigint;
  newPos: number | string | bigint;
  len: number | string | bigint;
};
export type DiffWithCoversResult = {
  diff: Buffer;
  usedCovers: boolean;
  requestedCoverCount: number;
  nativeCoverCapacity: number;
  finalCoverCount: number;
  coverMode: 'replace' | 'merge' | 'native-coalesce';
  nativeCovers?: HpatchCover[];
  finalCovers?: HpatchCover[];
};
export type DiffWithCoversOptions = {
  mode?: 'replace' | 'merge' | 'native-coalesce' | 'native_coalesce';
  debugCovers?: boolean;
};

export interface NativeAddon {
  diff(oldBuf: BinaryLike, newBuf: BinaryLike): Buffer;
  diff(oldBuf: BinaryLike, newBuf: BinaryLike, cb: DiffCallback): void;
  diffWithCovers(
    oldBuf: BinaryLike,
    newBuf: BinaryLike,
    covers: HpatchCover[],
    options?: DiffWithCoversOptions
  ): DiffWithCoversResult;
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
export function diffWithCovers(
  oldBuf: BinaryLike,
  newBuf: BinaryLike,
  covers: HpatchCover[],
  options?: DiffWithCoversOptions
): DiffWithCoversResult;

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
  diffWithCovers: typeof diffWithCovers;
  patch: typeof patch;
  diffStream: typeof diffStream;
  patchStream: typeof patchStream;
  patchSingleStream: typeof patchSingleStream;
};

export default hdiffpatch;
