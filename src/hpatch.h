/**
 * hpatch - Apply patch to restore original data
 * Created based on HDiffPatch library
 */

#ifndef HDIFFPATCH_PATCH_H
#define HDIFFPATCH_PATCH_H
#include <stddef.h>
#include <stdint.h>
#include <vector>

// 补丁头里的 newDataSize/stepMemSize 是不可信输入:一个损坏或恶意补丁
// 可以声明任意大的输出或工作内存。所有 patch 入口都强制有限上限,
// 默认值可由调用方通过 options 覆盖。
struct HPatchLimits {
    uint64_t maxOutputBytes;
    uint64_t maxWorkingMemoryBytes;
};

// 内存版输出要落成 Node Buffer(上限 ~4GiB),默认收紧到 2GiB;
// 文件版面向大文件场景,默认 16GiB;工作内存(stepMemSize)本库自产
// 补丁固定 256KB,默认 256MiB 已远超正常值。
const uint64_t kDefaultMaxPatchOutputBytesMem  = (uint64_t)2  << 30;
const uint64_t kDefaultMaxPatchOutputBytesFile = (uint64_t)16 << 30;
const uint64_t kDefaultMaxPatchWorkingMemoryBytes = (uint64_t)256 << 20;

void hpatch(const uint8_t* old, size_t oldsize,
            const uint8_t* diff, size_t diffsize,
            std::vector<uint8_t>& out_newBuf,
            const HPatchLimits& limits);
void hpatch_single_stream(const char* oldPath,const char* diffPath,const char* outNewPath,
                          const HPatchLimits& limits);
void hpatch_stream(const char* oldPath,const char* diffPath,const char* outNewPath,
                   const HPatchLimits& limits);

#endif
