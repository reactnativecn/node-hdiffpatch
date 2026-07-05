#include "hdiff.h"
#include "../HDiffPatch/libHDiffPatch/HDiff/diff.h"
#include "../HDiffPatch/file_for_patch.h"
#include <algorithm>
#include <limits>
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
    struct CoverLinesListener {
        ICoverLinesListener base;
        const HdiffCover* covers;
        size_t requestedCoverCount;
        size_t nativeCoverCapacity;
        size_t finalCoverCount;
        bool hasNativeCoverCapacity;
        bool usedCovers;
        HdiffCoverMode coverMode;
        bool hasPreparedCovers;
        std::vector<hpatch_TCover> nativeCovers;
        std::vector<hpatch_TCover> preparedCovers;
    };

    hpatch_TCover make_cover(uint64_t oldPos, uint64_t newPos, uint64_t length) {
        hpatch_TCover cover;
        cover.oldPos = static_cast<hpatch_StreamPos_t>(oldPos);
        cover.newPos = static_cast<hpatch_StreamPos_t>(newPos);
        cover.length = static_cast<hpatch_StreamPos_t>(length);
        return cover;
    }

    uint64_t cover_new_end(const hpatch_TCover& cover) {
        return static_cast<uint64_t>(cover.newPos) + static_cast<uint64_t>(cover.length);
    }

    uint64_t cover_old_end(const hpatch_TCover& cover) {
        return static_cast<uint64_t>(cover.oldPos) + static_cast<uint64_t>(cover.length);
    }

    HdiffCover to_hdiff_cover(const hpatch_TCover& cover) {
        return {
            static_cast<uint64_t>(cover.oldPos),
            static_cast<uint64_t>(cover.newPos),
            static_cast<uint64_t>(cover.length),
        };
    }

    std::vector<HdiffCover> to_hdiff_covers(const std::vector<hpatch_TCover>& covers) {
        std::vector<HdiffCover> result;
        result.reserve(covers.size());
        for (const hpatch_TCover& cover : covers) {
            result.push_back(to_hdiff_cover(cover));
        }
        return result;
    }

    void push_external_slice(std::vector<hpatch_TCover>& out,
                             const HdiffCover& cover,
                             uint64_t sliceNewStart,
                             uint64_t sliceNewEnd) {
        if (sliceNewEnd <= sliceNewStart) {
            return;
        }
        const uint64_t delta = sliceNewStart - cover.newPos;
        out.push_back(make_cover(cover.oldPos + delta, sliceNewStart, sliceNewEnd - sliceNewStart));
    }

    std::vector<hpatch_TCover> merge_covers(const hpatch_TCover* nativeCovers,
                                            size_t nativeCoverCount,
                                            const HdiffCover* externalCovers,
                                            size_t externalCoverCount) {
        std::vector<hpatch_TCover> merged;
        merged.reserve(nativeCoverCount + externalCoverCount);

        for (size_t i = 0; i < nativeCoverCount; ++i) {
            if (nativeCovers[i].length > 0) {
                merged.push_back(nativeCovers[i]);
            }
        }

        size_t nativeIndex = 0;
        for (size_t externalIndex = 0; externalIndex < externalCoverCount; ++externalIndex) {
            const HdiffCover& external = externalCovers[externalIndex];
            const uint64_t externalEnd = external.newPos + external.length;
            uint64_t cursor = external.newPos;

            while (nativeIndex < nativeCoverCount && cover_new_end(nativeCovers[nativeIndex]) <= cursor) {
                ++nativeIndex;
            }

            size_t scanIndex = nativeIndex;
            while (scanIndex < nativeCoverCount &&
                   static_cast<uint64_t>(nativeCovers[scanIndex].newPos) < externalEnd) {
                const uint64_t nativeStart = static_cast<uint64_t>(nativeCovers[scanIndex].newPos);
                const uint64_t nativeEnd = cover_new_end(nativeCovers[scanIndex]);
                if (nativeStart > cursor) {
                    push_external_slice(merged, external, cursor, std::min(nativeStart, externalEnd));
                }
                if (nativeEnd > cursor) {
                    cursor = nativeEnd;
                    if (cursor >= externalEnd) {
                        break;
                    }
                }
                ++scanIndex;
            }

            push_external_slice(merged, external, cursor, externalEnd);
        }

        std::sort(merged.begin(), merged.end(), [](const hpatch_TCover& left, const hpatch_TCover& right) {
            if (left.newPos != right.newPos) {
                return left.newPos < right.newPos;
            }
            return left.oldPos < right.oldPos;
        });

        return merged;
    }

    std::vector<hpatch_TCover> coalesce_native_covers(const hpatch_TCover* nativeCovers,
                                                      size_t nativeCoverCount) {
        const uint64_t maxGapBytes = 64;
        std::vector<hpatch_TCover> coalesced;
        coalesced.reserve(nativeCoverCount);

        for (size_t i = 0; i < nativeCoverCount; ++i) {
            const hpatch_TCover& cover = nativeCovers[i];
            if (cover.length == 0) {
                continue;
            }
            if (coalesced.empty()) {
                coalesced.push_back(cover);
                continue;
            }

            hpatch_TCover& previous = coalesced.back();
            const uint64_t previousNewEnd = cover_new_end(previous);
            const uint64_t previousOldEnd = cover_old_end(previous);
            const uint64_t coverNewPos = static_cast<uint64_t>(cover.newPos);
            const uint64_t coverOldPos = static_cast<uint64_t>(cover.oldPos);
            if (coverNewPos < previousNewEnd || coverOldPos < previousOldEnd) {
                coalesced.push_back(cover);
                continue;
            }

            const uint64_t newGap = coverNewPos - previousNewEnd;
            const uint64_t oldGap = coverOldPos - previousOldEnd;
            const int64_t previousDelta =
                static_cast<int64_t>(previous.oldPos) - static_cast<int64_t>(previous.newPos);
            const int64_t coverDelta =
                static_cast<int64_t>(cover.oldPos) - static_cast<int64_t>(cover.newPos);
            if (newGap <= maxGapBytes && oldGap == newGap && previousDelta == coverDelta) {
                previous.length = static_cast<hpatch_StreamPos_t>(cover_new_end(cover) - previous.newPos);
                continue;
            }

            coalesced.push_back(cover);
        }

        return coalesced;
    }

    void prepare_listener_covers(CoverLinesListener* self,
                                 hpatch_TCover* out_covers,
                                 size_t currentCoverCapacity) {
        if (self->hasPreparedCovers) {
            return;
        }

        if (self->coverMode == HdiffCoverMode::NativeCoalesce) {
            self->preparedCovers = coalesce_native_covers(out_covers, currentCoverCapacity);
        } else if (self->coverMode == HdiffCoverMode::Merge) {
            self->preparedCovers =
                merge_covers(out_covers, currentCoverCapacity, self->covers, self->requestedCoverCount);
        } else {
            self->preparedCovers.reserve(self->requestedCoverCount);
            for (size_t i = 0; i < self->requestedCoverCount; ++i) {
                self->preparedCovers.push_back(make_cover(
                    self->covers[i].oldPos,
                    self->covers[i].newPos,
                    self->covers[i].length));
            }
        }

        self->finalCoverCount = self->preparedCovers.size();
        self->hasPreparedCovers = true;
    }

    void coverLines(ICoverLinesListener* listener, hpatch_TCover* out_covers, size_t* coverCount,
                    hpatch_StreamPos_t* /*newSize*/, hpatch_StreamPos_t* /*oldSize*/) {
        CoverLinesListener* self = reinterpret_cast<CoverLinesListener*>(listener);
        const size_t currentCoverCapacity = *coverCount;
        if (!self->hasNativeCoverCapacity) {
            self->nativeCoverCapacity = currentCoverCapacity;
            self->nativeCovers.assign(out_covers, out_covers + currentCoverCapacity);
            self->hasNativeCoverCapacity = true;
        }
        self->usedCovers = false;
        prepare_listener_covers(self, out_covers, currentCoverCapacity);

        // HDiffPatch currently sizes out_covers to its native cover count and
        // callers may use the first pass to request a larger cover buffer.
        if (self->finalCoverCount > currentCoverCapacity) {
            *coverCount = self->finalCoverCount;
            return;
        }

        for (size_t i = 0; i < self->finalCoverCount; ++i) {
            out_covers[i] = self->preparedCovers[i];
        }
        *coverCount = self->finalCoverCount;
        self->usedCovers = true;
    }

    void validate_covers(const HdiffCover* covers, size_t coverCount, size_t oldsize, size_t newsize) {
        uint64_t previousNewEnd = 0;
        const uint64_t oldSize64 = static_cast<uint64_t>(oldsize);
        const uint64_t newSize64 = static_cast<uint64_t>(newsize);

        for (size_t i = 0; i < coverCount; ++i) {
            const HdiffCover& cover = covers[i];
            if (cover.length == 0) {
                throw std::runtime_error("Invalid cover: length must be greater than zero.");
            }
            if (cover.oldPos > oldSize64 || cover.length > oldSize64 - cover.oldPos) {
                throw std::runtime_error("Invalid cover: old range is out of bounds.");
            }
            if (cover.newPos > newSize64 || cover.length > newSize64 - cover.newPos) {
                throw std::runtime_error("Invalid cover: new range is out of bounds.");
            }
            if (cover.newPos < previousNewEnd) {
                throw std::runtime_error("Invalid cover: new ranges must be sorted and non-overlapping.");
            }
            previousNewEnd = cover.newPos + cover.length;

            if (cover.oldPos > std::numeric_limits<hpatch_StreamPos_t>::max() ||
                cover.newPos > std::numeric_limits<hpatch_StreamPos_t>::max() ||
                cover.length > std::numeric_limits<hpatch_StreamPos_t>::max()) {
                throw std::runtime_error("Invalid cover: range does not fit hpatch stream position.");
            }
        }
    }

    void configure_lzma2(TCompressPlugin_lzma2& compressPlugin) {
        const size_t myBestDictSize = (1 << 20) * 8;  // 固定 8MB，与 v1.0.6 一致
        compressPlugin = lzma2CompressPlugin;
        compressPlugin.compress_level = 9;
        compressPlugin.dict_size = myBestDictSize;
        compressPlugin.thread_num = 1;
    }
}

void hdiff(const uint8_t* old, size_t oldsize, const uint8_t* _new, size_t newsize,
           std::vector<uint8_t>& out_codeBuf) {
    const int myBestSingleMatchScore = 3;
    const size_t myBestStepMemSize = kDefaultStepMemSize;

    hpatch_TDecompress* decompressPlugin = &lzma2DecompressPlugin;
    TCompressPlugin_lzma2 compressPlugin;
    configure_lzma2(compressPlugin);

    create_single_compressed_diff(_new, _new + newsize, old, old + oldsize, out_codeBuf, 0,
                                  &compressPlugin.base, myBestSingleMatchScore, myBestStepMemSize);
    
    if (!check_single_compressed_diff(_new, _new + newsize, old, old + oldsize, out_codeBuf.data(),
                                      out_codeBuf.data() + out_codeBuf.size(), decompressPlugin)) {
        throw std::runtime_error("check_single_compressed_diff() failed, diff code error!");
    }
}

HdiffWithCoversResult hdiff_with_covers(const uint8_t* old, size_t oldsize,
                                        const uint8_t* _new, size_t newsize,
                                        const HdiffCover* covers, size_t coverCount,
                                        HdiffCoverMode coverMode,
                                        std::vector<uint8_t>& out_codeBuf) {
    const int myBestSingleMatchScore = 3;
    const size_t myBestStepMemSize = kDefaultStepMemSize;

    if (coverCount > 0 && covers == nullptr) {
        throw std::runtime_error("Invalid cover list.");
    }
    validate_covers(covers, coverCount, oldsize, newsize);

    hpatch_TDecompress* decompressPlugin = &lzma2DecompressPlugin;
    TCompressPlugin_lzma2 compressPlugin;
    configure_lzma2(compressPlugin);

    CoverLinesListener listener = {
        { coverLines },
        covers,
        coverCount,
        0,
        0,
        false,
        false,
        coverMode,
        false,
        {},
        {},
    };

    create_single_compressed_diff(_new, _new + newsize, old, old + oldsize, out_codeBuf,
                                  &listener.base, &compressPlugin.base, myBestSingleMatchScore,
                                  myBestStepMemSize);

    if (!check_single_compressed_diff(_new, _new + newsize, old, old + oldsize, out_codeBuf.data(),
                                      out_codeBuf.data() + out_codeBuf.size(), decompressPlugin)) {
        throw std::runtime_error("check_single_compressed_diff() failed, diff code error!");
    }

    return HdiffWithCoversResult{
        listener.usedCovers,
        coverCount,
        listener.nativeCoverCapacity,
        listener.finalCoverCount,
        to_hdiff_covers(listener.nativeCovers),
        to_hdiff_covers(listener.preparedCovers),
    };
}

void hdiff_stream(const char* oldPath,const char* newPath,const char* outDiffPath){
    if (!oldPath || !newPath || !outDiffPath) {
        throw std::runtime_error("Invalid file path.");
    }

    hpatch_TDecompress* decompressPlugin = &lzma2DecompressPlugin;
    TCompressPlugin_lzma2 compressPlugin;
    configure_lzma2(compressPlugin);

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
