#include "hdiff.h"
#include "../HDiffPatch/libHDiffPatch/HDiff/diff.h"
#include "../HDiffPatch/file_for_patch.h"
#include <stdexcept>

#define _CompressPlugin_lzma2
#define _IsNeedIncludeDefaultCompressHead 0
#include "../lzma/C/Lzma2Dec.h"
#include "../lzma/C/Lzma2Enc.h"
#include "../lzma/C/MtCoder.h"
#include "../HDiffPatch/compress_plugin_demo.h"
#include "../HDiffPatch/decompress_plugin_demo.h"

void hdiff(const uint8_t* old, size_t oldsize, const uint8_t* _new, size_t newsize,
           std::vector<uint8_t>& out_codeBuf) {
    const int myBestSingleMatchScore = 3;
    const size_t myBestStepMemSize = kDefaultStepMemSize;
    const size_t myBestDictSize = (1 << 20) * 8;  // 固定 8MB，与 v1.0.6 一致

    hpatch_TDecompress* decompressPlugin = &lzma2DecompressPlugin;
    TCompressPlugin_lzma2 compressPlugin = lzma2CompressPlugin;
    compressPlugin.compress_level = 9;
    compressPlugin.dict_size = myBestDictSize;
    compressPlugin.thread_num = 1;

    create_single_compressed_diff(_new, _new + newsize, old, old + oldsize, out_codeBuf, 0,
                                  &compressPlugin.base, myBestSingleMatchScore, myBestStepMemSize);
    
    if (!check_single_compressed_diff(_new, _new + newsize, old, old + oldsize, out_codeBuf.data(),
                                      out_codeBuf.data() + out_codeBuf.size(), decompressPlugin)) {
        throw std::runtime_error("check_single_compressed_diff() failed, diff code error!");
    }
}

void hdiff_stream(const char* oldPath,const char* newPath,const char* outDiffPath){
    if (!oldPath || !newPath || !outDiffPath) {
        throw std::runtime_error("Invalid file path.");
    }

    const size_t myBestDictSize = (1 << 20) * 8;  // 固定 8MB，与 v1.0.6 一致
    hpatch_TDecompress* decompressPlugin = &lzma2DecompressPlugin;
    TCompressPlugin_lzma2 compressPlugin = lzma2CompressPlugin;
    compressPlugin.compress_level = 9;
    compressPlugin.dict_size = myBestDictSize;
    compressPlugin.thread_num = 1;

    hpatch_TFileStreamInput oldStream;
    hpatch_TFileStreamInput newStream;
    hpatch_TFileStreamOutput diffOutStream;
    hpatch_TFileStreamInput diffInStream;
    hpatch_TFileStreamInput_init(&oldStream);
    hpatch_TFileStreamInput_init(&newStream);
    hpatch_TFileStreamOutput_init(&diffOutStream);
    hpatch_TFileStreamInput_init(&diffInStream);

    bool oldOpened = false;
    bool newOpened = false;
    bool diffOutOpened = false;
    bool diffInOpened = false;

    try {
        if (!hpatch_TFileStreamInput_open(&oldStream, oldPath)) {
            throw std::runtime_error("open old file failed.");
        }
        oldOpened = true;
        if (!hpatch_TFileStreamInput_open(&newStream, newPath)) {
            throw std::runtime_error("open new file failed.");
        }
        newOpened = true;
        if (!hpatch_TFileStreamOutput_open(&diffOutStream, outDiffPath, ~(hpatch_StreamPos_t)0)) {
            throw std::runtime_error("open diff file for write failed.");
        }
        diffOutOpened = true;
        hpatch_TFileStreamOutput_setRandomOut(&diffOutStream, hpatch_TRUE);

        create_compressed_diff_stream(&newStream.base, &oldStream.base, &diffOutStream.base,
                                      &compressPlugin.base, kMatchBlockSize_default);

        if (!hpatch_TFileStreamOutput_close(&diffOutStream)) {
            throw std::runtime_error("close diff file failed.");
        }
        diffOutOpened = false;

        if (!hpatch_TFileStreamInput_open(&diffInStream, outDiffPath)) {
            throw std::runtime_error("open diff file for read failed.");
        }
        diffInOpened = true;
        if (!check_compressed_diff_stream(&newStream.base, &oldStream.base,
                                          &diffInStream.base, decompressPlugin)) {
            throw std::runtime_error("check_compressed_diff_stream() failed, diff code error!");
        }
    } catch (...) {
        if (diffInOpened) hpatch_TFileStreamInput_close(&diffInStream);
        if (diffOutOpened) hpatch_TFileStreamOutput_close(&diffOutStream);
        if (newOpened) hpatch_TFileStreamInput_close(&newStream);
        if (oldOpened) hpatch_TFileStreamInput_close(&oldStream);
        throw;
    }

    if (diffInOpened && !hpatch_TFileStreamInput_close(&diffInStream)) {
        throw std::runtime_error("close diff file failed.");
    }
    if (newOpened && !hpatch_TFileStreamInput_close(&newStream)) {
        throw std::runtime_error("close new file failed.");
    }
    if (oldOpened && !hpatch_TFileStreamInput_close(&oldStream)) {
        throw std::runtime_error("close old file failed.");
    }
}
