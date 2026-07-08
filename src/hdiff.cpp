#include "hdiff.h"
#include "../HDiffPatch/libHDiffPatch/HDiff/diff.h"
#include "../HDiffPatch/file_for_patch.h"
#include <stdexcept>

#define _CompressPlugin_lzma2
#define _IsNeedIncludeDefaultCompressHead 0
#define IS_NOTICE_compress_canceled 0
#include "../lzma/C/Lzma2Dec.h"
#include "../lzma/C/Lzma2Enc.h"
#include "../lzma/C/MtCoder.h"
#include "../HDiffPatch/compress_plugin_demo.h"
#include "../HDiffPatch/decompress_plugin_demo.h"

namespace {
    // 与 v1.0.6 起的历史产物保持一致的压缩参数(patch 兼容性由 single
    // 格式规范保证,这里的一致性只为产物尺寸/确定性稳定)
    void configure_lzma2(TCompressPlugin_lzma2& compressPlugin) {
        const size_t myBestDictSize = (1 << 20) * 8;  // 固定 8MB
        compressPlugin = lzma2CompressPlugin;
        compressPlugin.compress_level = 9;
        compressPlugin.dict_size = myBestDictSize;
        compressPlugin.thread_num = 1;
    }

    // 升级到 HDiffPatch v5 前的历史参数:匹配分 3、步进内存 256KB。
    // v5 的 kMinSingleMatchScore_default=4/kDefaultPatchStepMemSize=256KB,
    // 显式固定为旧值,保证与既有版本产出行为连续。
    const int kSingleMatchScore = 3;
    const size_t kPatchStepMemSize = 1024 * 256;

    struct FileStreamGuard {
        hpatch_TFileStreamInput oldStream{};
        hpatch_TFileStreamInput newStream{};
        hpatch_TFileStreamOutput diffOutStream{};
        hpatch_TFileStreamInput diffInStream{};
        bool oldOpened = false;
        bool newOpened = false;
        bool diffOutOpened = false;
        bool diffInOpened = false;

        FileStreamGuard() {
            hpatch_TFileStreamInput_init(&oldStream);
            hpatch_TFileStreamInput_init(&newStream);
            hpatch_TFileStreamOutput_init(&diffOutStream);
            hpatch_TFileStreamInput_init(&diffInStream);
        }
        ~FileStreamGuard() {
            if (diffInOpened) hpatch_TFileStreamInput_close(&diffInStream);
            if (diffOutOpened) hpatch_TFileStreamOutput_close(&diffOutStream);
            if (newOpened) hpatch_TFileStreamInput_close(&newStream);
            if (oldOpened) hpatch_TFileStreamInput_close(&oldStream);
        }
        void openInputs(const char* oldPath, const char* newPath) {
            if (!hpatch_TFileStreamInput_open(&oldStream, oldPath)) {
                throw std::runtime_error("open old file failed.");
            }
            oldOpened = true;
            if (!hpatch_TFileStreamInput_open(&newStream, newPath)) {
                throw std::runtime_error("open new file failed.");
            }
            newOpened = true;
        }
        void openDiffOut(const char* outDiffPath) {
            if (!hpatch_TFileStreamOutput_open(&diffOutStream, outDiffPath,
                                               ~(hpatch_StreamPos_t)0)) {
                throw std::runtime_error("open diff file for write failed.");
            }
            diffOutOpened = true;
            // 两种流式生成都会回写文件头,必须允许随机写
            hpatch_TFileStreamOutput_setRandomOut(&diffOutStream, hpatch_TRUE);
        }
        void closeDiffOut() {
            diffOutOpened = false;
            if (!hpatch_TFileStreamOutput_close(&diffOutStream)) {
                throw std::runtime_error("close diff file failed.");
            }
        }
        void openDiffIn(const char* outDiffPath) {
            if (!hpatch_TFileStreamInput_open(&diffInStream, outDiffPath)) {
                throw std::runtime_error("open diff file for read failed.");
            }
            diffInOpened = true;
        }
        void closeAllOrThrow() {
            if (diffInOpened) {
                diffInOpened = false;
                if (!hpatch_TFileStreamInput_close(&diffInStream)) {
                    throw std::runtime_error("close diff file failed.");
                }
            }
            newOpened = false;
            if (!hpatch_TFileStreamInput_close(&newStream)) {
                throw std::runtime_error("close new file failed.");
            }
            oldOpened = false;
            if (!hpatch_TFileStreamInput_close(&oldStream)) {
                throw std::runtime_error("close old file failed.");
            }
        }
    };
}

void hdiff(const uint8_t* old, size_t oldsize, const uint8_t* _new, size_t newsize,
           std::vector<uint8_t>& out_codeBuf) {
    hpatch_TDecompress* decompressPlugin = &lzma2DecompressPlugin;
    TCompressPlugin_lzma2 compressPlugin;
    configure_lzma2(compressPlugin);

    create_single_compressed_diff(_new, _new + newsize, old, old + oldsize, out_codeBuf,
                                  &compressPlugin.base, kPatchStepMemSize,
                                  kSingleMatchScore);

    if (!check_single_compressed_diff(_new, _new + newsize, old, old + oldsize,
                                      out_codeBuf.data(),
                                      out_codeBuf.data() + out_codeBuf.size(),
                                      decompressPlugin)) {
        throw std::runtime_error("check_single_compressed_diff() failed, diff code error!");
    }
}

void hdiff_stream(const char* oldPath,const char* newPath,const char* outDiffPath){
    if (!oldPath || !newPath || !outDiffPath) {
        throw std::runtime_error("Invalid file path.");
    }

    hpatch_TDecompress* decompressPlugin = &lzma2DecompressPlugin;
    TCompressPlugin_lzma2 compressPlugin;
    configure_lzma2(compressPlugin);

    FileStreamGuard streams;
    streams.openInputs(oldPath, newPath);
    streams.openDiffOut(outDiffPath);

    create_compressed_diff_stream(&streams.newStream.base, &streams.oldStream.base,
                                  &streams.diffOutStream.base,
                                  &compressPlugin.base, kMatchBlockSize_default);

    streams.closeDiffOut();
    streams.openDiffIn(outDiffPath);
    if (!check_compressed_diff(&streams.newStream.base, &streams.oldStream.base,
                               &streams.diffInStream.base, decompressPlugin)) {
        throw std::runtime_error("check_compressed_diff() failed, diff code error!");
    }
    streams.closeAllOrThrow();
}

void hdiff_single_stream(const char* oldPath,const char* newPath,const char* outDiffPath){
    if (!oldPath || !newPath || !outDiffPath) {
        throw std::runtime_error("Invalid file path.");
    }

    hpatch_TDecompress* decompressPlugin = &lzma2DecompressPlugin;
    TCompressPlugin_lzma2 compressPlugin;
    configure_lzma2(compressPlugin);

    FileStreamGuard streams;
    streams.openInputs(oldPath, newPath);
    streams.openDiffOut(outDiffPath);

    create_single_compressed_diff_stream(&streams.newStream.base, &streams.oldStream.base,
                                         &streams.diffOutStream.base,
                                         &compressPlugin.base, kPatchStepMemSize,
                                         kMatchBlockSize_default);

    streams.closeDiffOut();
    streams.openDiffIn(outDiffPath);
    if (!check_single_compressed_diff(&streams.newStream.base, &streams.oldStream.base,
                                      &streams.diffInStream.base, decompressPlugin)) {
        throw std::runtime_error("check_single_compressed_diff() failed, diff code error!");
    }
    streams.closeAllOrThrow();
}
