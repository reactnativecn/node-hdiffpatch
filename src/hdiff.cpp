#include "hdiff.h"
#include "../HDiffPatch/libHDiffPatch/HDiff/diff.h"
#include "../HDiffPatch/libHDiffPatch/HPatch/patch.h"
#include "../HDiffPatch/file_for_patch.h"
#include <cstring>
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
    const char kSingleDiffPrefix[] = "HDIFFSF20&";
    const size_t kSingleDiffPrefixSize = sizeof(kSingleDiffPrefix) - 1;

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

    struct FileRewriteGuard {
        hpatch_TFileStreamOutput stream{};
        bool opened = false;

        FileRewriteGuard() {
            hpatch_TFileStreamOutput_init(&stream);
        }
        ~FileRewriteGuard() {
            if (opened) hpatch_TFileStreamOutput_close(&stream);
        }
        void open(const char* path) {
            if (!hpatch_TFileStreamOutput_reopen(&stream, path, ~(hpatch_StreamPos_t)0)) {
                throw std::runtime_error("open diff file for rewrite failed.");
            }
            opened = true;
            hpatch_TFileStreamOutput_setRandomOut(&stream, hpatch_TRUE);
        }
        void close() {
            opened = false;
            if (!hpatch_TFileStreamOutput_close(&stream)) {
                throw std::runtime_error("close diff file failed.");
            }
        }
    };

    bool should_clear_single_raw_compress_type(const hpatch_singleCompressedDiffInfo& diffInfo,
                                               size_t* outTypeLen) {
        if ((diffInfo.compressedSize != 0) || (diffInfo.compressType[0] == '\0')) {
            return false;
        }
        *outTypeLen = std::strlen(diffInfo.compressType);
        if (*outTypeLen == 0) {
            return false;
        }
        return true;
    }

    struct SingleRawCompressTypeSplice {
        bool shouldSplice = false;
        hpatch_StreamPos_t readPos = 0;
        hpatch_StreamPos_t writePos = 0;
        hpatch_StreamPos_t newSize = 0;
    };

    SingleRawCompressTypeSplice get_single_raw_compress_type_splice(
            const hpatch_singleCompressedDiffInfo& diffInfo,
            hpatch_StreamPos_t diffSize,
            const uint8_t* prefixBytes) {
        size_t typeLen = 0;
        if (!should_clear_single_raw_compress_type(diffInfo, &typeLen)) {
            return {};
        }
        if ((diffSize < kSingleDiffPrefixSize + typeLen + 1) ||
            (0 != std::memcmp(prefixBytes, kSingleDiffPrefix, kSingleDiffPrefixSize))) {
            throw std::runtime_error("single diff header layout error.");
        }

        SingleRawCompressTypeSplice splice;
        splice.shouldSplice = true;
        splice.readPos = kSingleDiffPrefixSize + typeLen;
        splice.writePos = kSingleDiffPrefixSize;
        splice.newSize = diffSize - typeLen;
        return splice;
    }

    void normalize_single_raw_compress_type(std::vector<uint8_t>& diff) {
        hpatch_singleCompressedDiffInfo diffInfo;
        if (!getSingleCompressedDiffInfo_mem(&diffInfo, diff.data(), diff.data() + diff.size())) {
            throw std::runtime_error("getSingleCompressedDiffInfo_mem() failed, invalid diff data!");
        }

        SingleRawCompressTypeSplice splice = get_single_raw_compress_type_splice(
            diffInfo, diff.size(), diff.data());
        if (!splice.shouldSplice) {
            return;
        }
        diff.erase(diff.begin() + (size_t)splice.writePos,
                   diff.begin() + (size_t)splice.readPos);
    }

    void normalize_single_raw_compress_type(const char* diffPath) {
        hpatch_TFileStreamInput diffIn;
        hpatch_TFileStreamInput_init(&diffIn);
        bool diffInOpened = false;

        hpatch_singleCompressedDiffInfo diffInfo;
        hpatch_StreamPos_t oldDiffSize = 0;
        uint8_t prefixBytes[kSingleDiffPrefixSize];
        try {
            if (!hpatch_TFileStreamInput_open(&diffIn, diffPath)) {
                throw std::runtime_error("open diff file for read failed.");
            }
            diffInOpened = true;
            oldDiffSize = diffIn.base.streamSize;
            if ((oldDiffSize < kSingleDiffPrefixSize) ||
                !diffIn.base.read(&diffIn.base, 0, prefixBytes,
                                  prefixBytes + kSingleDiffPrefixSize)) {
                throw std::runtime_error("single diff header layout error.");
            }
            if (!getSingleCompressedDiffInfo(&diffInfo, &diffIn.base, 0)) {
                throw std::runtime_error("getSingleCompressedDiffInfo() failed, invalid diff data!");
            }
        } catch (...) {
            if (diffInOpened) hpatch_TFileStreamInput_close(&diffIn);
            throw;
        }
        diffInOpened = false;
        if (!hpatch_TFileStreamInput_close(&diffIn)) {
            throw std::runtime_error("close diff file failed.");
        }

        SingleRawCompressTypeSplice splice = get_single_raw_compress_type_splice(
            diffInfo, oldDiffSize, prefixBytes);
        if (!splice.shouldSplice) {
            return;
        }

        FileRewriteGuard diffOut;
        diffOut.open(diffPath);

        const size_t kBufSize = hpatch_kFileIOBufBetterSize;
        std::vector<uint8_t> buf(kBufSize);
        hpatch_StreamPos_t readPos = splice.readPos;
        hpatch_StreamPos_t writePos = splice.writePos;
        while (readPos < oldDiffSize) {
            hpatch_StreamPos_t readLen = oldDiffSize - readPos;
            if (readLen > (hpatch_StreamPos_t)buf.size()) {
                readLen = (hpatch_StreamPos_t)buf.size();
            }
            if (!diffOut.stream.base.read_writed(&diffOut.stream.base, readPos,
                                                 buf.data(), buf.data() + (size_t)readLen)) {
                throw std::runtime_error("read diff file for rewrite failed.");
            }
            if (!diffOut.stream.base.write(&diffOut.stream.base, writePos,
                                           buf.data(), buf.data() + (size_t)readLen)) {
                throw std::runtime_error("rewrite diff file failed.");
            }
            readPos += readLen;
            writePos += readLen;
        }
        if (!hpatch_TFileStreamOutput_flush(&diffOut.stream)) {
            throw std::runtime_error("flush diff file failed.");
        }
        if (!hpatch_TFileStreamOutput_truncate(&diffOut.stream, splice.newSize)) {
            throw std::runtime_error("truncate diff file failed.");
        }
        diffOut.close();
    }
}

void hdiff(const uint8_t* old, size_t oldsize, const uint8_t* _new, size_t newsize,
           std::vector<uint8_t>& out_codeBuf) {
    hpatch_TDecompress* decompressPlugin = &lzma2DecompressPlugin;
    TCompressPlugin_lzma2 compressPlugin;
    configure_lzma2(compressPlugin);

    create_single_compressed_diff(_new, _new + newsize, old, old + oldsize, out_codeBuf,
                                  &compressPlugin.base, kPatchStepMemSize,
                                  kSingleMatchScore);
    normalize_single_raw_compress_type(out_codeBuf);

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

void hdiff_window(const char* oldPath,const char* newPath,const char* outDiffPath,
                  size_t windowSize){
    if (!oldPath || !newPath || !outDiffPath) {
        throw std::runtime_error("Invalid file path.");
    }

    hpatch_TDecompress* decompressPlugin = &lzma2DecompressPlugin;
    TCompressPlugin_lzma2 compressPlugin;
    configure_lzma2(compressPlugin);

    FileStreamGuard streams;
    streams.openInputs(oldPath, newPath);
    streams.openDiffOut(outDiffPath);

    // window 模式:大块流式匹配拿大 cover,再在 old 数据的滑动窗口内做
    // 后缀串精修。窗口默认 2MB,可调大以捕获更长距离的内容移动;
    // kSegSize 传 0 由上游自动取 windowSize/64。除 patchStepMemSize/
    // 匹配分沿用本库固定值外,其余参数取 v5 默认。
    if (windowSize == 0) windowSize = kDefaultWindowOldSize;
    create_single_compressed_diff_window(&streams.newStream.base, &streams.oldStream.base,
                                         &streams.diffOutStream.base,
                                         &compressPlugin.base, kPatchStepMemSize,
                                         windowSize, 0,
                                         kDefaultBigCoverSize, kMatchWindowsBlockSize_default,
                                         kDefaultFastMatchBlockSize,
                                         kSingleMatchScore);

    streams.closeDiffOut();
    normalize_single_raw_compress_type(outDiffPath);
    streams.openDiffIn(outDiffPath);
    if (!check_single_compressed_diff(&streams.newStream.base, &streams.oldStream.base,
                                      &streams.diffInStream.base, decompressPlugin)) {
        throw std::runtime_error("check_single_compressed_diff() failed, diff code error!");
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
    normalize_single_raw_compress_type(outDiffPath);
    streams.openDiffIn(outDiffPath);
    if (!check_single_compressed_diff(&streams.newStream.base, &streams.oldStream.base,
                                      &streams.diffInStream.base, decompressPlugin)) {
        throw std::runtime_error("check_single_compressed_diff() failed, diff code error!");
    }
    streams.closeAllOrThrow();
}
