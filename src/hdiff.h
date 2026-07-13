/**
 * Created by housisong on 2021.04.07.
 */

#ifndef HDIFFPATCH_DIFF_H
#define HDIFFPATCH_DIFF_H
#include <stddef.h>
#include <stdint.h>
#include <vector>

void hdiff(const uint8_t* old,size_t oldsize,const uint8_t* _new,size_t newsize,
		   std::vector<uint8_t>& out_codeBuf,size_t compressionThreads=1);
// HDIFF13 流式(生成端低内存,产物需 patchStream 应用)
void hdiff_stream(const char* oldPath,const char* newPath,const char* outDiffPath,
                  size_t compressionThreads=1);
// HDIFFSF20 single 格式的流式生成(生成端低内存,产物与 diff() 同格式,
// 任何既有 single 应用端可直接使用)
void hdiff_single_stream(const char* oldPath,const char* newPath,const char* outDiffPath,
                         size_t compressionThreads=1);
// HDIFFSF20 single 格式的 window 模式生成:大块流式匹配 + 窗口内后缀串
// 精修,匹配质量接近内存版而内存占用保持流式档;产物与 diff() 同格式。
// windowSize 为 old 数据滑动窗口字节数,0 表示用默认值(2MB);窗口越大
// 能捕获越长距离的内容移动,内存占用近似随之线性增长。
void hdiff_window(const char* oldPath,const char* newPath,const char* outDiffPath,
                  size_t windowSize=0,size_t compressionThreads=1);

#endif
