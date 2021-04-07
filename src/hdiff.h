/**
 * Created by housisong on 2021.04.07.
 */

#ifndef HDIFFPATCH_DIFF_H
#define HDIFFPATCH_DIFF_H
#include <stdint.h>
#include <vector>

void hdiff(const uint8_t* old,size_t oldsize,const uint8_t* _new,size_t newsize,
		   std::vector<uint8_t>& out_codeBuf);

#endif
