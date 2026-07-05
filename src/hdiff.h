/**
 * Created by housisong on 2021.04.07.
 */

#ifndef HDIFFPATCH_DIFF_H
#define HDIFFPATCH_DIFF_H
#include <stddef.h>
#include <stdint.h>
#include <vector>

struct HdiffCover {
    uint64_t oldPos;
    uint64_t newPos;
    uint64_t length;
};

enum class HdiffCoverMode {
    Replace,
    Merge,
    NativeCoalesce,
};

struct HdiffWithCoversResult {
    bool usedCovers;
    size_t requestedCoverCount;
    size_t nativeCoverCapacity;
    size_t finalCoverCount;
    std::vector<HdiffCover> nativeCovers;
    std::vector<HdiffCover> finalCovers;
};

void hdiff(const uint8_t* old,size_t oldsize,const uint8_t* _new,size_t newsize,
		   std::vector<uint8_t>& out_codeBuf);
HdiffWithCoversResult hdiff_with_covers(const uint8_t* old, size_t oldsize,
                                        const uint8_t* _new, size_t newsize,
                                        const HdiffCover* covers, size_t coverCount,
                                        HdiffCoverMode coverMode,
                                        std::vector<uint8_t>& out_codeBuf);
void hdiff_stream(const char* oldPath,const char* newPath,const char* outDiffPath);

#endif
