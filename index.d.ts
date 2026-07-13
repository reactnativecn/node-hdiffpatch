/// <reference types="node" />

export type BinaryLike = Buffer | ArrayBufferView;

export type DiffCallback = (err: Error | null, result?: Buffer) => void;
export type StreamCallback = (err: Error | null, outPath?: string) => void;

export interface CompressionOptions {
  /** LZMA2 compression workers. Level 9 and the 8 MiB dictionary are unchanged. */
  compressionThreads?: 1 | 2;
}

export interface DiffWindowOptions extends CompressionOptions {
  /** Old-data sliding window bytes; 0 uses the native 2 MiB default. */
  windowSize?: number;
}

export interface NativeAddon {
  diff(oldBuf: BinaryLike, newBuf: BinaryLike): Buffer;
  diff(oldBuf: BinaryLike, newBuf: BinaryLike, options: CompressionOptions): Buffer;
  diff(oldBuf: BinaryLike, newBuf: BinaryLike, cb: DiffCallback): void;
  diff(
    oldBuf: BinaryLike,
    newBuf: BinaryLike,
    options: CompressionOptions,
    cb: DiffCallback
  ): void;
  patch(oldBuf: BinaryLike, diffBuf: BinaryLike): Buffer;
  patch(oldBuf: BinaryLike, diffBuf: BinaryLike, cb: DiffCallback): void;
  diffStream(oldPath: string, newPath: string, outDiffPath: string): string;
  diffStream(
    oldPath: string,
    newPath: string,
    outDiffPath: string,
    options: CompressionOptions
  ): string;
  diffStream(
    oldPath: string,
    newPath: string,
    outDiffPath: string,
    cb: StreamCallback
  ): void;
  diffStream(
    oldPath: string,
    newPath: string,
    outDiffPath: string,
    options: CompressionOptions,
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
    options: CompressionOptions
  ): string;
  diffSingleStream(
    oldPath: string,
    newPath: string,
    outDiffPath: string,
    cb: StreamCallback,
  ): void;
  diffSingleStream(
    oldPath: string,
    newPath: string,
    outDiffPath: string,
    options: CompressionOptions,
    cb: StreamCallback,
  ): void;
  patchSingleStream(oldPath: string, diffPath: string, outNewPath: string): string;
  patchSingleStream(
    oldPath: string,
    diffPath: string,
    outNewPath: string,
    cb: StreamCallback
  ): void;
  diffWindow(
    oldPath: string,
    newPath: string,
    outDiffPath: string,
    windowSize?: number
  ): string;
  diffWindow(
    oldPath: string,
    newPath: string,
    outDiffPath: string,
    options: DiffWindowOptions
  ): string;
  diffWindow(
    oldPath: string,
    newPath: string,
    outDiffPath: string,
    cb: StreamCallback
  ): void;
  diffWindow(
    oldPath: string,
    newPath: string,
    outDiffPath: string,
    options: DiffWindowOptions,
    cb: StreamCallback
  ): void;
  diffWindow(
    oldPath: string,
    newPath: string,
    outDiffPath: string,
    windowSize: number,
    cb: StreamCallback
  ): void;
}

export const native: NativeAddon;

export interface HdiffpatchCapabilities {
  readonly diffStreamVerifiesOutput: true;
  readonly diffSingleStreamVerifiesOutput: true;
  readonly diffWindowVerifiesOutput: true;
  readonly maxCompressionThreads: 2;
}

/** Native diff functions apply and compare their output before returning. */
export const capabilities: HdiffpatchCapabilities;

export function diff(oldBuf: BinaryLike, newBuf: BinaryLike): Buffer;
export function diff(
  oldBuf: BinaryLike,
  newBuf: BinaryLike,
  options: CompressionOptions
): Buffer;
export function diff(
  oldBuf: BinaryLike,
  newBuf: BinaryLike,
  cb: DiffCallback
): void;
export function diff(
  oldBuf: BinaryLike,
  newBuf: BinaryLike,
  options: CompressionOptions,
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
  options: CompressionOptions
): string;
export function diffStream(
  oldPath: string,
  newPath: string,
  outDiffPath: string,
  cb: StreamCallback
): void;
export function diffStream(
  oldPath: string,
  newPath: string,
  outDiffPath: string,
  options: CompressionOptions,
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
  options: CompressionOptions,
): string;
export function diffSingleStream(
  oldPath: string,
  newPath: string,
  outDiffPath: string,
  cb: StreamCallback,
): void;
export function diffSingleStream(
  oldPath: string,
  newPath: string,
  outDiffPath: string,
  options: CompressionOptions,
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

// window 模式生成 HDIFFSF20 single 格式 patch:匹配质量接近内存版
// diff(),内存占用保持流式档;产物用 patch()/patchSingleStream() 应用。
// windowSize 为 old 数据滑动窗口字节数(缺省 2MB),调大可捕获更长距离
// 的内容移动,内存占用近似线性增长。
export function diffWindow(
  oldPath: string,
  newPath: string,
  outDiffPath: string,
  windowSize?: number
): string;
export function diffWindow(
  oldPath: string,
  newPath: string,
  outDiffPath: string,
  options: DiffWindowOptions
): string;
export function diffWindow(
  oldPath: string,
  newPath: string,
  outDiffPath: string,
  cb: StreamCallback
): void;
export function diffWindow(
  oldPath: string,
  newPath: string,
  outDiffPath: string,
  options: DiffWindowOptions,
  cb: StreamCallback
): void;
export function diffWindow(
  oldPath: string,
  newPath: string,
  outDiffPath: string,
  windowSize: number,
  cb: StreamCallback
): void;

declare const hdiffpatch: {
  native: NativeAddon;
  capabilities: HdiffpatchCapabilities;
  diff: typeof diff;
  patch: typeof patch;
  diffStream: typeof diffStream;
  patchStream: typeof patchStream;
  diffSingleStream: typeof diffSingleStream;
  patchSingleStream: typeof patchSingleStream;
  diffWindow: typeof diffWindow;
};

export default hdiffpatch;
