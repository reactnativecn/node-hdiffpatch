/**
 * hpatch - Apply patch to restore original data
 * Created based on HDiffPatch library
 */
#include "hpatch.h"
#include "temp_output.h"
#include "../HDiffPatch/libHDiffPatch/HPatch/patch.h"
#include "../HDiffPatch/file_for_patch.h"
#include <limits>
#include <stdexcept>
#include <string>

#define _CompressPlugin_lzma2
#define _IsNeedIncludeDefaultCompressHead 0
#include "../lzma/C/LzmaDec.h"
#include "../lzma/C/Lzma2Dec.h"
#include "../HDiffPatch/decompress_plugin_demo.h"

// Listener for patch_single_stream_by
struct PatchListener {
    hpatch_TDecompress* decompressPlugin;
    std::vector<uint8_t>* tempCache;
    HPatchLimits limits;
    // onDiffInfo 由 HDiffPatch 的 C 代码回调,任何 C++ 异常都不能穿过
    // C 栈帧;拒绝原因记录在这里,失败后由 C++ 侧转成异常消息。
    const char* errorMessage;
};

static hpatch_BOOL onDiffInfo(sspatch_listener_t* listener,
                              const hpatch_singleCompressedDiffInfo* info,
                              hpatch_TDecompress** out_decompressPlugin,
                              unsigned char** out_temp_cache,
                              unsigned char** out_temp_cacheEnd) {
    PatchListener* self = (PatchListener*)listener->import;

    // newDataSize/stepMemSize 均来自补丁数据,先按调用方上限拒绝,
    // 再拒绝超出 size_t 可表示范围的声明值
    if (info->newDataSize > self->limits.maxOutputBytes) {
        self->errorMessage = "declared new data size exceeds maxOutputBytes limit.";
        return hpatch_FALSE;
    }
    if (info->stepMemSize > self->limits.maxWorkingMemoryBytes) {
        self->errorMessage = "declared stepMemSize exceeds maxWorkingMemoryBytes limit.";
        return hpatch_FALSE;
    }
    const hpatch_StreamPos_t maxCacheSize =
        (hpatch_StreamPos_t)(std::numeric_limits<size_t>::max() - hpatch_kStreamCacheSize * 4);
    if (info->stepMemSize > maxCacheSize) {
        self->errorMessage = "declared stepMemSize is not representable.";
        return hpatch_FALSE;
    }

    // Allocate temp cache: stepMemSize + I/O cache
    size_t cacheSize = (size_t)info->stepMemSize + hpatch_kStreamCacheSize * 4;
    try {
        self->tempCache->resize(cacheSize);
    } catch (...) {
        self->errorMessage = "allocate patch working memory failed.";
        return hpatch_FALSE;
    }

    *out_decompressPlugin = self->decompressPlugin;
    *out_temp_cache = self->tempCache->data();
    *out_temp_cacheEnd = self->tempCache->data() + cacheSize;

    return hpatch_TRUE;
}

static void initPatchListener(PatchListener& patchListener,
                              hpatch_TDecompress* decompressPlugin,
                              std::vector<uint8_t>* tempCache,
                              const HPatchLimits& limits) {
    patchListener.decompressPlugin = decompressPlugin;
    patchListener.tempCache = tempCache;
    patchListener.limits = limits;
    patchListener.errorMessage = nullptr;
}

static void throwPatchFailure(const PatchListener& patchListener, const char* fallback) {
    if (patchListener.errorMessage) {
        throw std::runtime_error(patchListener.errorMessage);
    }
    throw std::runtime_error(fallback);
}

void hpatch(const uint8_t* old, size_t oldsize,
            const uint8_t* diff, size_t diffsize,
            std::vector<uint8_t>& out_newBuf,
            const HPatchLimits& limits) {

    // Get diff info to determine output size
    hpatch_singleCompressedDiffInfo diffInfo;
    if (!getSingleCompressedDiffInfo_mem(&diffInfo, diff, diff + diffsize)) {
        throw std::runtime_error("getSingleCompressedDiffInfo_mem() failed, invalid diff data!");
    }

    // Verify old data size matches
    if (diffInfo.oldDataSize != oldsize) {
        throw std::runtime_error("Old data size mismatch!");
    }

    // newDataSize 来自 diff 数据:先查调用方上限,再防 uint64 → size_t
    // 截断(32 位平台)。上限检查必须发生在任何分配之前。
    if (diffInfo.newDataSize > limits.maxOutputBytes) {
        throw std::runtime_error("declared new data size exceeds maxOutputBytes limit.");
    }
    if (diffInfo.newDataSize >
        (hpatch_StreamPos_t)std::numeric_limits<size_t>::max()) {
        throw std::runtime_error("Invalid diff data: declared new size is too large!");
    }

    // Allocate output buffer
    out_newBuf.resize((size_t)diffInfo.newDataSize);

    // Setup decompressor
    hpatch_TDecompress* decompressPlugin = &lzma2DecompressPlugin;

    // Setup listener
    std::vector<uint8_t> tempCache;
    PatchListener patchListener;
    initPatchListener(patchListener, decompressPlugin, &tempCache, limits);

    sspatch_listener_t listener;
    listener.import = &patchListener;
    listener.onDiffInfo = onDiffInfo;
    listener.onPatchFinish = nullptr;

    // Execute patch
    if (!patch_single_stream_mem(&listener,
                                 out_newBuf.data(), out_newBuf.data() + out_newBuf.size(),
                                 old, old + oldsize,
                                 diff, diff + diffsize,
                                 0 /*coversListener*/, 1 /*threadNum*/)) {
        throwPatchFailure(patchListener, "patch_single_stream_mem() failed!");
    }
}

void hpatch_single_stream(const char* oldPath,const char* diffPath,const char* outNewPath,
                          const HPatchLimits& limits){
    if (!oldPath || !diffPath || !outNewPath) {
        throw std::runtime_error("Invalid file path.");
    }

    hpatch_TDecompress* decompressPlugin = &lzma2DecompressPlugin;

    hpatch_TFileStreamInput oldStream;
    hpatch_TFileStreamInput diffStream;
    hpatch_TFileStreamOutput newStream;
    hpatch_TFileStreamInput_init(&oldStream);
    hpatch_TFileStreamInput_init(&diffStream);
    hpatch_TFileStreamOutput_init(&newStream);

    bool oldOpened = false;
    bool diffOpened = false;
    bool newOpened = false;

    // 声明先于流变量:异常路径先关流再删临时文件(见 temp_output.h)
    hdiffpatchNode::TempOutputFile tempOut;
    std::vector<uint8_t> tempCache;
    PatchListener patchListener;
    initPatchListener(patchListener, decompressPlugin, &tempCache, limits);

    try {
        if (!hpatch_TFileStreamInput_open(&oldStream, oldPath)) {
            throw std::runtime_error("open old file failed.");
        }
        oldOpened = true;
        if (!hpatch_TFileStreamInput_open(&diffStream, diffPath)) {
            throw std::runtime_error("open diff file failed.");
        }
        diffOpened = true;
        tempOut.create(outNewPath);
        if (!hpatch_TFileStreamOutput_open(&newStream, tempOut.path(),
                                           ~(hpatch_StreamPos_t)0)) {
            throw std::runtime_error("open new file for write failed.");
        }
        newOpened = true;

        sspatch_listener_t listener;
        listener.import = &patchListener;
        listener.onDiffInfo = onDiffInfo;
        listener.onPatchFinish = nullptr;

        if (!patch_single_stream(&listener, &newStream.base, &oldStream.base, &diffStream.base,
                                 0 /*diffInfo_pos*/, 0 /*coversListener*/, 1 /*threadNum*/)) {
            throwPatchFailure(patchListener, "patch_single_stream() failed!");
        }
    } catch (...) {
        if (newOpened) hpatch_TFileStreamOutput_close(&newStream);
        if (diffOpened) hpatch_TFileStreamInput_close(&diffStream);
        if (oldOpened) hpatch_TFileStreamInput_close(&oldStream);
        throw;
    }

    if (newOpened && !hpatch_TFileStreamOutput_close(&newStream)) {
        throw std::runtime_error("close new file failed.");
    }
    if (diffOpened && !hpatch_TFileStreamInput_close(&diffStream)) {
        throw std::runtime_error("close diff file failed.");
    }
    if (oldOpened && !hpatch_TFileStreamInput_close(&oldStream)) {
        throw std::runtime_error("close old file failed.");
    }
    tempOut.commit();
}

void hpatch_stream(const char* oldPath,const char* diffPath,const char* outNewPath,
                   const HPatchLimits& limits){
    if (!oldPath || !diffPath || !outNewPath) {
        throw std::runtime_error("Invalid file path.");
    }

    hpatch_TDecompress* decompressPlugin = &lzma2DecompressPlugin;

    hpatch_TFileStreamInput oldStream;
    hpatch_TFileStreamInput diffStream;
    hpatch_TFileStreamOutput newStream;
    hpatch_TFileStreamInput_init(&oldStream);
    hpatch_TFileStreamInput_init(&diffStream);
    hpatch_TFileStreamOutput_init(&newStream);

    bool oldOpened = false;
    bool diffOpened = false;
    bool newOpened = false;

    hdiffpatchNode::TempOutputFile tempOut;

    try {
        if (!hpatch_TFileStreamInput_open(&oldStream, oldPath)) {
            throw std::runtime_error("open old file failed.");
        }
        oldOpened = true;
        if (!hpatch_TFileStreamInput_open(&diffStream, diffPath)) {
            throw std::runtime_error("open diff file failed.");
        }
        diffOpened = true;

        hpatch_compressedDiffInfo diffInfo;
        if (!getCompressedDiffInfo(&diffInfo, &diffStream.base)) {
            throw std::runtime_error("getCompressedDiffInfo() failed, invalid diff data!");
        }
        if (diffInfo.oldDataSize != oldStream.base.streamSize) {
            throw std::runtime_error("Old data size mismatch!");
        }
        if (decompressPlugin && !decompressPlugin->is_can_open(diffInfo.compressType)) {
            throw std::runtime_error("Unsupported diff compress type.");
        }
        // newDataSize 来自补丁数据,在创建输出文件之前拒绝超限声明
        if (diffInfo.newDataSize > limits.maxOutputBytes) {
            throw std::runtime_error("declared new data size exceeds maxOutputBytes limit.");
        }

        tempOut.create(outNewPath);
        if (!hpatch_TFileStreamOutput_open(&newStream, tempOut.path(), diffInfo.newDataSize)) {
            throw std::runtime_error("open new file for write failed.");
        }
        newOpened = true;

        if (!patch_decompress(&newStream.base, &oldStream.base, &diffStream.base, decompressPlugin)) {
            throw std::runtime_error("patch_decompress() failed!");
        }
    } catch (...) {
        if (newOpened) hpatch_TFileStreamOutput_close(&newStream);
        if (diffOpened) hpatch_TFileStreamInput_close(&diffStream);
        if (oldOpened) hpatch_TFileStreamInput_close(&oldStream);
        throw;
    }

    if (newOpened && !hpatch_TFileStreamOutput_close(&newStream)) {
        throw std::runtime_error("close new file failed.");
    }
    if (diffOpened && !hpatch_TFileStreamInput_close(&diffStream)) {
        throw std::runtime_error("close diff file failed.");
    }
    if (oldOpened && !hpatch_TFileStreamInput_close(&oldStream)) {
        throw std::runtime_error("close old file failed.");
    }
    tempOut.commit();
}
