/**
 * hpatch - Apply patch to restore original data
 * Created based on HDiffPatch library
 */
#include "hpatch.h"
#include "../HDiffPatch/libHDiffPatch/HPatch/patch.h"
#include <stdexcept>

#define _CompressPlugin_lzma2
#define _IsNeedIncludeDefaultCompressHead 0
#include "../lzma/C/LzmaDec.h"
#include "../lzma/C/Lzma2Dec.h"
#include "../HDiffPatch/decompress_plugin_demo.h"

// Listener for patch_single_stream_by
struct PatchListener {
    hpatch_TDecompress* decompressPlugin;
    std::vector<uint8_t>* tempCache;
};

static hpatch_BOOL onDiffInfo(sspatch_listener_t* listener,
                              const hpatch_singleCompressedDiffInfo* info,
                              hpatch_TDecompress** out_decompressPlugin,
                              unsigned char** out_temp_cache,
                              unsigned char** out_temp_cacheEnd) {
    PatchListener* self = (PatchListener*)listener->import;
    
    // Allocate temp cache: stepMemSize + I/O cache
    size_t cacheSize = (size_t)info->stepMemSize + hpatch_kStreamCacheSize * 4;
    self->tempCache->resize(cacheSize);
    
    *out_decompressPlugin = self->decompressPlugin;
    *out_temp_cache = self->tempCache->data();
    *out_temp_cacheEnd = self->tempCache->data() + cacheSize;
    
    return hpatch_TRUE;
}

void hpatch(const uint8_t* old, size_t oldsize,
            const uint8_t* diff, size_t diffsize,
            std::vector<uint8_t>& out_newBuf) {
    
    // Get diff info to determine output size
    hpatch_singleCompressedDiffInfo diffInfo;
    if (!getSingleCompressedDiffInfo_mem(&diffInfo, diff, diff + diffsize)) {
        throw std::runtime_error("getSingleCompressedDiffInfo_mem() failed, invalid diff data!");
    }
    
    // Verify old data size matches
    if (diffInfo.oldDataSize != oldsize) {
        throw std::runtime_error("Old data size mismatch!");
    }
    
    // Allocate output buffer
    out_newBuf.resize((size_t)diffInfo.newDataSize);
    
    // Setup decompressor
    hpatch_TDecompress* decompressPlugin = &lzma2DecompressPlugin;
    
    // Setup listener
    std::vector<uint8_t> tempCache;
    PatchListener patchListener;
    patchListener.decompressPlugin = decompressPlugin;
    patchListener.tempCache = &tempCache;
    
    sspatch_listener_t listener;
    listener.import = &patchListener;
    listener.onDiffInfo = onDiffInfo;
    listener.onPatchFinish = nullptr;
    
    // Execute patch
    if (!patch_single_stream_by_mem(&listener,
                                    out_newBuf.data(), out_newBuf.data() + out_newBuf.size(),
                                    old, old + oldsize,
                                    diff, diff + diffsize)) {
        throw std::runtime_error("patch_single_stream_by_mem() failed!");
    }
}
