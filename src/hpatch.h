/**
 * hpatch - Apply patch to restore original data
 * Created based on HDiffPatch library
 */

#ifndef HDIFFPATCH_PATCH_H
#define HDIFFPATCH_PATCH_H
#include <stddef.h>
#include <stdint.h>
#include <vector>

void hpatch(const uint8_t* old, size_t oldsize,
            const uint8_t* diff, size_t diffsize,
            std::vector<uint8_t>& out_newBuf);

#endif
