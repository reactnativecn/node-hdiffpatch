/**
 * node-hdiffpatch - Node-API implementation
 * Refactored from NAN for Bun compatibility
 * Created by housisong on 2021.04.07, refactored 2026.01.20
 */
#include <napi.h>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include "hdiff.h"
#include "hpatch.h"

namespace hdiffpatchNode
{
    // Helper: 从参数获取数据指针和长度（支持 Buffer、TypedArray 和 DataView）
    inline bool getBufferData(const Napi::Value& arg, const uint8_t** data, size_t* length) {
        // DataView 必须在 Buffer 之前判断:napi_is_buffer 对所有
        // ArrayBufferView 返回 true,但 napi_get_buffer_info 只接受 Uint8Array
        if (arg.IsDataView()) {
            Napi::DataView dataView = arg.As<Napi::DataView>();
            Napi::ArrayBuffer arrayBuffer = dataView.ArrayBuffer();
            *data = static_cast<const uint8_t*>(arrayBuffer.Data()) + dataView.ByteOffset();
            *length = dataView.ByteLength();
            return true;
        }
        if (arg.IsBuffer()) {
            Napi::Buffer<uint8_t> buf = arg.As<Napi::Buffer<uint8_t>>();
            *data = buf.Data();
            *length = buf.Length();
            return true;
        }
        if (arg.IsTypedArray()) {
            Napi::TypedArray typedArray = arg.As<Napi::TypedArray>();
            Napi::ArrayBuffer arrayBuffer = typedArray.ArrayBuffer();
            *data = static_cast<const uint8_t*>(arrayBuffer.Data()) + typedArray.ByteOffset();
            *length = typedArray.ByteLength();
            return true;
        }
        return false;
    }

    // 复制一份输入数据。异步 worker 在 libuv 线程上运行,期间 JS 侧仍可
    // 修改 Buffer 内容甚至 transfer/detach 底层 ArrayBuffer;持有副本是
    // 唯一不依赖调用方约定的安全做法。
    inline std::vector<uint8_t> copyBufferData(const uint8_t* data, size_t length) {
        return std::vector<uint8_t>(data, data + length);
    }

    inline bool getStringUtf8(const Napi::Value& arg, std::string& out) {
        if (!arg.IsString()) return false;
        out = arg.As<Napi::String>().Utf8Value();
        return true;
    }

    struct NativeDiffOptions {
        size_t compressionThreads = 1;
        size_t windowSize = 0;
    };

    // JS Number 只能精确表示 2^53-1 以内的整数;同时 double(SIZE_MAX) 在
    // 64 位平台会舍入为 2^64,直接比较会放过 2^64 并触发浮点转无符号的
    // 未定义行为。所有 Number → size_t 的解析统一收紧到两者较小值。
    const uint64_t kMaxSafeIntegerBound =
        (uint64_t)9007199254740991ull < (uint64_t)std::numeric_limits<size_t>::max()
            ? (uint64_t)9007199254740991ull
            : (uint64_t)std::numeric_limits<size_t>::max();

    inline bool parseIntegerOption(const Napi::Value& value,
                                   size_t minimum,
                                   size_t maximum,
                                   size_t& out) {
        if (!value.IsNumber()) return false;
        if ((uint64_t)maximum > kMaxSafeIntegerBound) {
            maximum = (size_t)kMaxSafeIntegerBound;
        }
        const double raw = value.As<Napi::Number>().DoubleValue();
        // minimum/maximum 均 <= 2^53-1,double 转换是精确的
        if (!std::isfinite(raw) || std::floor(raw) != raw ||
            raw < static_cast<double>(minimum) ||
            raw > static_cast<double>(maximum)) {
            return false;
        }
        out = static_cast<size_t>(raw);
        return true;
    }

    inline bool parseDiffOptions(Napi::Env env,
                                 const Napi::Value& value,
                                 bool allowWindowSize,
                                 NativeDiffOptions& out) {
        if (!value.IsObject() || value.IsFunction()) {
            Napi::TypeError::New(env, "Invalid diff options: expected an object.")
                .ThrowAsJavaScriptException();
            return false;
        }
        Napi::Object options = value.As<Napi::Object>();
        if (options.Has("compressionThreads")) {
            size_t threads = 0;
            if (!parseIntegerOption(options.Get("compressionThreads"), 1, 2, threads)) {
                Napi::TypeError::New(env, "Invalid compressionThreads: expected 1 or 2.")
                    .ThrowAsJavaScriptException();
                return false;
            }
            out.compressionThreads = threads;
        }
        if (options.Has("windowSize")) {
            if (!allowWindowSize) {
                Napi::TypeError::New(env, "windowSize is only supported by diffWindow().")
                    .ThrowAsJavaScriptException();
                return false;
            }
            size_t windowSize = 0;
            if (!parseIntegerOption(options.Get("windowSize"), 0,
                                    std::numeric_limits<size_t>::max(), windowSize)) {
                Napi::TypeError::New(env, "Invalid windowSize: expected a non-negative integer.")
                    .ThrowAsJavaScriptException();
                return false;
            }
            out.windowSize = windowSize;
        }
        return true;
    }

    // patch 侧资源上限,见 hpatch.h;maxOutputBytes 的默认值由调用点按
    // 内存版/文件版分别注入
    inline bool parsePatchOptions(Napi::Env env,
                                  const Napi::Value& value,
                                  HPatchLimits& out) {
        if (!value.IsObject() || value.IsFunction()) {
            Napi::TypeError::New(env, "Invalid patch options: expected an object.")
                .ThrowAsJavaScriptException();
            return false;
        }
        Napi::Object options = value.As<Napi::Object>();
        if (options.Has("maxOutputBytes")) {
            size_t maxOutput = 0;
            if (!parseIntegerOption(options.Get("maxOutputBytes"), 1,
                                    std::numeric_limits<size_t>::max(), maxOutput)) {
                Napi::TypeError::New(env,
                    "Invalid maxOutputBytes: expected a positive safe integer.")
                    .ThrowAsJavaScriptException();
                return false;
            }
            out.maxOutputBytes = maxOutput;
        }
        if (options.Has("maxWorkingMemoryBytes")) {
            size_t maxWorkMem = 0;
            if (!parseIntegerOption(options.Get("maxWorkingMemoryBytes"), 1,
                                    std::numeric_limits<size_t>::max(), maxWorkMem)) {
                Napi::TypeError::New(env,
                    "Invalid maxWorkingMemoryBytes: expected a positive safe integer.")
                    .ThrowAsJavaScriptException();
                return false;
            }
            out.maxWorkingMemoryBytes = maxWorkMem;
        }
        return true;
    }

    inline HPatchLimits defaultPatchLimitsMem() {
        return HPatchLimits{kDefaultMaxPatchOutputBytesMem,
                            kDefaultMaxPatchWorkingMemoryBytes};
    }

    inline HPatchLimits defaultPatchLimitsFile() {
        return HPatchLimits{kDefaultMaxPatchOutputBytesFile,
                            kDefaultMaxPatchWorkingMemoryBytes};
    }

    inline Napi::Buffer<uint8_t> bufferFromVector(Napi::Env env, std::vector<uint8_t>&& data) {
        if (data.empty()) {
            return Napi::Buffer<uint8_t>::New(env, 0);
        }
        auto* vec = new std::vector<uint8_t>(std::move(data));
        return Napi::Buffer<uint8_t>::New(
            env,
            vec->data(),
            vec->size(),
            [](Napi::Env /*env*/, uint8_t* /*data*/, std::vector<uint8_t>* vecPtr) {
                delete vecPtr;
            },
            vec
        );
    }

    // ============ 异步 Diff Worker ============
    // worker 持有输入数据的副本(排队前在主线程复制),因此调用方在回调
    // 完成前修改、transfer 或 detach 原 Buffer 都不影响结果。
    class DiffAsyncWorker : public Napi::AsyncWorker {
    public:
        DiffAsyncWorker(Napi::Function& callback,
                        std::vector<uint8_t>&& oldData,
                        std::vector<uint8_t>&& newData,
                        size_t compressionThreads)
            : Napi::AsyncWorker(callback),
              oldData_(std::move(oldData)),
              newData_(std::move(newData)),
              compressionThreads_(compressionThreads) {
        }

        void Execute() override {
            try {
                hdiff(oldData_.data(), oldData_.size(),
                      newData_.data(), newData_.size(), result_, compressionThreads_);
            } catch (const std::exception& e) {
                SetError(e.what());
            }
        }

        void OnOK() override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Napi::Buffer<uint8_t> resultBuf = bufferFromVector(env, std::move(result_));
            Callback().Call({env.Null(), resultBuf});
        }

        void OnError(const Napi::Error& e) override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Callback().Call({e.Value()});
        }

    private:
        std::vector<uint8_t> oldData_;
        std::vector<uint8_t> newData_;
        size_t compressionThreads_;
        std::vector<uint8_t> result_;
    };

    // ============ 异步 Patch Worker ============
    class PatchAsyncWorker : public Napi::AsyncWorker {
    public:
        PatchAsyncWorker(Napi::Function& callback,
                         std::vector<uint8_t>&& oldData,
                         std::vector<uint8_t>&& diffData,
                         const HPatchLimits& limits)
            : Napi::AsyncWorker(callback),
              oldData_(std::move(oldData)),
              diffData_(std::move(diffData)),
              limits_(limits) {
        }

        void Execute() override {
            try {
                hpatch(oldData_.data(), oldData_.size(),
                       diffData_.data(), diffData_.size(), result_, limits_);
            } catch (const std::exception& e) {
                SetError(e.what());
            }
        }

        void OnOK() override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Napi::Buffer<uint8_t> resultBuf = bufferFromVector(env, std::move(result_));
            Callback().Call({env.Null(), resultBuf});
        }

        void OnError(const Napi::Error& e) override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Callback().Call({e.Value()});
        }

    private:
        std::vector<uint8_t> oldData_;
        std::vector<uint8_t> diffData_;
        HPatchLimits limits_;
        std::vector<uint8_t> result_;
    };

    // ============ 异步 Stream Diff Worker ============
    class DiffStreamAsyncWorker : public Napi::AsyncWorker {
    public:
        DiffStreamAsyncWorker(Napi::Function& callback,
                              std::string oldPath,
                              std::string newPath,
                              std::string outDiffPath,
                              size_t compressionThreads)
            : Napi::AsyncWorker(callback),
              oldPath_(std::move(oldPath)),
              newPath_(std::move(newPath)),
              outDiffPath_(std::move(outDiffPath)),
              compressionThreads_(compressionThreads) {
        }

        void Execute() override {
            try {
                hdiff_stream(oldPath_.c_str(), newPath_.c_str(), outDiffPath_.c_str(),
                             compressionThreads_);
            } catch (const std::exception& e) {
                SetError(e.what());
            }
        }

        void OnOK() override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Callback().Call({env.Null(), Napi::String::New(env, outDiffPath_)});
        }

        void OnError(const Napi::Error& e) override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Callback().Call({e.Value()});
        }

    private:
        std::string oldPath_;
        std::string newPath_;
        std::string outDiffPath_;
        size_t compressionThreads_;
    };

    // ============ 异步 Stream Patch Worker ============
    class PatchStreamAsyncWorker : public Napi::AsyncWorker {
    public:
        PatchStreamAsyncWorker(Napi::Function& callback,
                               std::string oldPath,
                               std::string diffPath,
                               std::string outNewPath,
                               const HPatchLimits& limits)
            : Napi::AsyncWorker(callback),
              oldPath_(std::move(oldPath)),
              diffPath_(std::move(diffPath)),
              outNewPath_(std::move(outNewPath)),
              limits_(limits) {
        }

        void Execute() override {
            try {
                hpatch_stream(oldPath_.c_str(), diffPath_.c_str(), outNewPath_.c_str(),
                              limits_);
            } catch (const std::exception& e) {
                SetError(e.what());
            }
        }

        void OnOK() override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Callback().Call({env.Null(), Napi::String::New(env, outNewPath_)});
        }

        void OnError(const Napi::Error& e) override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Callback().Call({e.Value()});
        }

    private:
        std::string oldPath_;
        std::string diffPath_;
        std::string outNewPath_;
        HPatchLimits limits_;
    };

    // ============ 异步 Single-compressed Patch Worker ============
    class PatchSingleStreamAsyncWorker : public Napi::AsyncWorker {
    public:
        PatchSingleStreamAsyncWorker(Napi::Function& callback,
                                     std::string oldPath,
                                     std::string diffPath,
                                     std::string outNewPath,
                                     const HPatchLimits& limits)
            : Napi::AsyncWorker(callback),
              oldPath_(std::move(oldPath)),
              diffPath_(std::move(diffPath)),
              outNewPath_(std::move(outNewPath)),
              limits_(limits) {
        }

        void Execute() override {
            try {
                hpatch_single_stream(oldPath_.c_str(), diffPath_.c_str(), outNewPath_.c_str(),
                                     limits_);
            } catch (const std::exception& e) {
                SetError(e.what());
            }
        }

        void OnOK() override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Callback().Call({env.Null(), Napi::String::New(env, outNewPath_)});
        }

        void OnError(const Napi::Error& e) override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Callback().Call({e.Value()});
        }

    private:
        std::string oldPath_;
        std::string diffPath_;
        std::string outNewPath_;
        HPatchLimits limits_;
    };

    // ============ 异步 Single-compressed Stream Diff Worker ============
    class DiffSingleStreamAsyncWorker : public Napi::AsyncWorker {
    public:
        DiffSingleStreamAsyncWorker(Napi::Function& callback,
                                    std::string oldPath,
                                    std::string newPath,
                                    std::string outDiffPath,
                                    size_t compressionThreads)
            : Napi::AsyncWorker(callback),
              oldPath_(std::move(oldPath)),
              newPath_(std::move(newPath)),
              outDiffPath_(std::move(outDiffPath)),
              compressionThreads_(compressionThreads) {
        }

        void Execute() override {
            try {
                hdiff_single_stream(oldPath_.c_str(), newPath_.c_str(), outDiffPath_.c_str(),
                                    compressionThreads_);
            } catch (const std::exception& e) {
                SetError(e.what());
            }
        }

        void OnOK() override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Callback().Call({env.Null(), Napi::String::New(env, outDiffPath_)});
        }

        void OnError(const Napi::Error& e) override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Callback().Call({e.Value()});
        }

    private:
        std::string oldPath_;
        std::string newPath_;
        std::string outDiffPath_;
        size_t compressionThreads_;
    };

    // ============ 同步/异步 diff ============
    Napi::Value diff(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        const uint8_t* oldData = nullptr;
        size_t oldLength = 0;
        const uint8_t* newData = nullptr;
        size_t newLength = 0;

        if (info.Length() < 2 ||
            !getBufferData(info[0], &oldData, &oldLength) ||
            !getBufferData(info[1], &newData, &newLength)) {
            Napi::TypeError::New(env, "Invalid arguments: expected Buffer or TypedArray.")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        NativeDiffOptions options;
        size_t argIdx = 2;
        if (info.Length() > argIdx && !info[argIdx].IsFunction()) {
            if (!parseDiffOptions(env, info[argIdx], false, options)) {
                return env.Undefined();
            }
            argIdx++;
        }

        // 如果提供了回调函数，使用异步模式
        if (info.Length() > argIdx && info[argIdx].IsFunction()) {
            Napi::Function callback = info[argIdx].As<Napi::Function>();
            DiffAsyncWorker* worker = new DiffAsyncWorker(
                callback,
                copyBufferData(oldData, oldLength),
                copyBufferData(newData, newLength),
                options.compressionThreads
            );
            worker->Queue();
            return env.Undefined();
        }

        // 同步模式
        std::vector<uint8_t> codeBuf;
        try {
            hdiff(oldData, oldLength, newData, newLength, codeBuf,
                  options.compressionThreads);
        } catch (const std::exception& e) {
            Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            return env.Undefined();
        }

        return bufferFromVector(env, std::move(codeBuf));
    }

    // ============ 同步/异步 patch ============
    Napi::Value patch(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        const uint8_t* oldData = nullptr;
        size_t oldLength = 0;
        const uint8_t* diffData = nullptr;
        size_t diffLength = 0;

        if (info.Length() < 2 ||
            !getBufferData(info[0], &oldData, &oldLength) ||
            !getBufferData(info[1], &diffData, &diffLength)) {
            Napi::TypeError::New(env, "Invalid arguments: expected Buffer or TypedArray (old, diff).")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        if (diffLength < 4) {
            Napi::Error::New(env, "Invalid diff data: too short.")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        HPatchLimits limits = defaultPatchLimitsMem();
        size_t argIdx = 2;
        if (info.Length() > argIdx && !info[argIdx].IsFunction()) {
            if (!parsePatchOptions(env, info[argIdx], limits)) {
                return env.Undefined();
            }
            argIdx++;
        }

        // 如果提供了回调函数，使用异步模式
        if (info.Length() > argIdx && info[argIdx].IsFunction()) {
            Napi::Function callback = info[argIdx].As<Napi::Function>();
            PatchAsyncWorker* worker = new PatchAsyncWorker(
                callback,
                copyBufferData(oldData, oldLength),
                copyBufferData(diffData, diffLength),
                limits
            );
            worker->Queue();
            return env.Undefined();
        }

        // 同步模式
        std::vector<uint8_t> newBuf;
        try {
            hpatch(oldData, oldLength, diffData, diffLength, newBuf, limits);
        } catch (const std::exception& e) {
            Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            return env.Undefined();
        }

        return bufferFromVector(env, std::move(newBuf));
    }

    // ============ 同步/异步 diffStream ============
    Napi::Value diffStream(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        std::string oldPath;
        std::string newPath;
        std::string outDiffPath;
        if (info.Length() < 3 ||
            !getStringUtf8(info[0], oldPath) ||
            !getStringUtf8(info[1], newPath) ||
            !getStringUtf8(info[2], outDiffPath)) {
            Napi::TypeError::New(env, "Invalid arguments: expected (oldPath, newPath, outDiffPath).")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        NativeDiffOptions options;
        size_t argIdx = 3;
        if (info.Length() > argIdx && !info[argIdx].IsFunction()) {
            if (!parseDiffOptions(env, info[argIdx], false, options)) {
                return env.Undefined();
            }
            argIdx++;
        }

        if (info.Length() > argIdx && info[argIdx].IsFunction()) {
            Napi::Function callback = info[argIdx].As<Napi::Function>();
            DiffStreamAsyncWorker* worker = new DiffStreamAsyncWorker(
                callback, oldPath, newPath, outDiffPath, options.compressionThreads
            );
            worker->Queue();
            return env.Undefined();
        }

        try {
            hdiff_stream(oldPath.c_str(), newPath.c_str(), outDiffPath.c_str(),
                         options.compressionThreads);
        } catch (const std::exception& e) {
            Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            return env.Undefined();
        }

        return Napi::String::New(env, outDiffPath);
    }

    // ============ 同步/异步 patchStream ============
    Napi::Value patchStream(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        std::string oldPath;
        std::string diffPath;
        std::string outNewPath;
        if (info.Length() < 3 ||
            !getStringUtf8(info[0], oldPath) ||
            !getStringUtf8(info[1], diffPath) ||
            !getStringUtf8(info[2], outNewPath)) {
            Napi::TypeError::New(env, "Invalid arguments: expected (oldPath, diffPath, outNewPath).")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        HPatchLimits limits = defaultPatchLimitsFile();
        size_t argIdx = 3;
        if (info.Length() > argIdx && !info[argIdx].IsFunction()) {
            if (!parsePatchOptions(env, info[argIdx], limits)) {
                return env.Undefined();
            }
            argIdx++;
        }

        if (info.Length() > argIdx && info[argIdx].IsFunction()) {
            Napi::Function callback = info[argIdx].As<Napi::Function>();
            PatchStreamAsyncWorker* worker = new PatchStreamAsyncWorker(
                callback, oldPath, diffPath, outNewPath, limits
            );
            worker->Queue();
            return env.Undefined();
        }

        try {
            hpatch_stream(oldPath.c_str(), diffPath.c_str(), outNewPath.c_str(), limits);
        } catch (const std::exception& e) {
            Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            return env.Undefined();
        }

        return Napi::String::New(env, outNewPath);
    }

    // ============ 异步 Window Diff Worker ============
    class DiffWindowAsyncWorker : public Napi::AsyncWorker {
    public:
        DiffWindowAsyncWorker(Napi::Function& callback,
                              std::string oldPath,
                              std::string newPath,
                              std::string outDiffPath,
                              size_t windowSize,
                              size_t compressionThreads)
            : Napi::AsyncWorker(callback),
              oldPath_(std::move(oldPath)),
              newPath_(std::move(newPath)),
              outDiffPath_(std::move(outDiffPath)),
              windowSize_(windowSize),
              compressionThreads_(compressionThreads) {
        }

        void Execute() override {
            try {
                hdiff_window(oldPath_.c_str(), newPath_.c_str(), outDiffPath_.c_str(),
                             windowSize_, compressionThreads_);
            } catch (const std::exception& e) {
                SetError(e.what());
            }
        }

        void OnOK() override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Callback().Call({env.Null(), Napi::String::New(env, outDiffPath_)});
        }

        void OnError(const Napi::Error& e) override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Callback().Call({e.Value()});
        }

    private:
        std::string oldPath_;
        std::string newPath_;
        std::string outDiffPath_;
        size_t windowSize_;
        size_t compressionThreads_;
    };

    // ============ 同步/异步 diffSingleStream ============
    // single 格式(HDIFFSF20)的流式生成:低内存(块匹配),产物与 diff()
    // 同格式,所有既有 single 应用端(含历史客户端)可直接应用。
    Napi::Value diffSingleStream(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        std::string oldPath;
        std::string newPath;
        std::string outDiffPath;
        if (info.Length() < 3 ||
            !getStringUtf8(info[0], oldPath) ||
            !getStringUtf8(info[1], newPath) ||
            !getStringUtf8(info[2], outDiffPath)) {
            Napi::TypeError::New(env, "Invalid arguments: expected (oldPath, newPath, outDiffPath).")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        NativeDiffOptions options;
        size_t argIdx = 3;
        if (info.Length() > argIdx && !info[argIdx].IsFunction()) {
            if (!parseDiffOptions(env, info[argIdx], false, options)) {
                return env.Undefined();
            }
            argIdx++;
        }

        if (info.Length() > argIdx && info[argIdx].IsFunction()) {
            Napi::Function callback = info[argIdx].As<Napi::Function>();
            DiffSingleStreamAsyncWorker* worker = new DiffSingleStreamAsyncWorker(
                callback, oldPath, newPath, outDiffPath, options.compressionThreads
            );
            worker->Queue();
            return env.Undefined();
        }

        try {
            hdiff_single_stream(oldPath.c_str(), newPath.c_str(), outDiffPath.c_str(),
                                options.compressionThreads);
        } catch (const std::exception& e) {
            Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            return env.Undefined();
        }

        return Napi::String::New(env, outDiffPath);
    }

    // ============ 同步/异步 diffWindow ============
    // single 格式(HDIFFSF20)的 window 模式生成:大块流式匹配 + 窗口内
    // 后缀串精修,匹配质量接近内存版 diff() 而内存占用保持流式档。
    // 产物与 diff()/diffSingleStream() 同格式,既有应用端可直接应用。
    // 签名:(oldPath, newPath, outDiffPath[, windowSize][, cb])
    // windowSize 为 old 数据滑动窗口字节数(缺省 2MB),调大可捕获更长
    // 距离的内容移动,内存占用近似线性增长。
    Napi::Value diffWindow(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        std::string oldPath;
        std::string newPath;
        std::string outDiffPath;
        if (info.Length() < 3 ||
            !getStringUtf8(info[0], oldPath) ||
            !getStringUtf8(info[1], newPath) ||
            !getStringUtf8(info[2], outDiffPath)) {
            Napi::TypeError::New(env, "Invalid arguments: expected (oldPath, newPath, outDiffPath[, windowSize][, cb]).")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        NativeDiffOptions options;
        size_t argIdx = 3;
        if (info.Length() > argIdx && info[argIdx].IsNumber()) {
            size_t windowSize = 0;
            if (!parseIntegerOption(info[argIdx], 0,
                                    std::numeric_limits<size_t>::max(), windowSize)) {
                Napi::TypeError::New(env,
                    "Invalid windowSize: expected a non-negative safe integer.")
                    .ThrowAsJavaScriptException();
                return env.Undefined();
            }
            options.windowSize = windowSize;
            argIdx++;
        }

        if (info.Length() > argIdx && !info[argIdx].IsFunction()) {
            if (!parseDiffOptions(env, info[argIdx], true, options)) {
                return env.Undefined();
            }
            argIdx++;
        }

        if (info.Length() > argIdx && info[argIdx].IsFunction()) {
            Napi::Function callback = info[argIdx].As<Napi::Function>();
            DiffWindowAsyncWorker* worker = new DiffWindowAsyncWorker(
                callback, oldPath, newPath, outDiffPath, options.windowSize,
                options.compressionThreads
            );
            worker->Queue();
            return env.Undefined();
        }

        try {
            hdiff_window(oldPath.c_str(), newPath.c_str(), outDiffPath.c_str(),
                         options.windowSize, options.compressionThreads);
        } catch (const std::exception& e) {
            Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            return env.Undefined();
        }

        return Napi::String::New(env, outDiffPath);
    }

    // ============ 同步/异步 patchSingleStream ============
    Napi::Value patchSingleStream(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        std::string oldPath;
        std::string diffPath;
        std::string outNewPath;
        if (info.Length() < 3 ||
            !getStringUtf8(info[0], oldPath) ||
            !getStringUtf8(info[1], diffPath) ||
            !getStringUtf8(info[2], outNewPath)) {
            Napi::TypeError::New(env, "Invalid arguments: expected (oldPath, diffPath, outNewPath).")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        HPatchLimits limits = defaultPatchLimitsFile();
        size_t argIdx = 3;
        if (info.Length() > argIdx && !info[argIdx].IsFunction()) {
            if (!parsePatchOptions(env, info[argIdx], limits)) {
                return env.Undefined();
            }
            argIdx++;
        }

        if (info.Length() > argIdx && info[argIdx].IsFunction()) {
            Napi::Function callback = info[argIdx].As<Napi::Function>();
            PatchSingleStreamAsyncWorker* worker = new PatchSingleStreamAsyncWorker(
                callback, oldPath, diffPath, outNewPath, limits
            );
            worker->Queue();
            return env.Undefined();
        }

        try {
            hpatch_single_stream(oldPath.c_str(), diffPath.c_str(), outNewPath.c_str(),
                                 limits);
        } catch (const std::exception& e) {
            Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            return env.Undefined();
        }

        return Napi::String::New(env, outNewPath);
    }

    Napi::Object Init(Napi::Env env, Napi::Object exports) {
        exports.Set(Napi::String::New(env, "diff"), Napi::Function::New(env, diff));
        exports.Set(Napi::String::New(env, "patch"), Napi::Function::New(env, patch));
        exports.Set(Napi::String::New(env, "diffStream"), Napi::Function::New(env, diffStream));
        exports.Set(Napi::String::New(env, "patchStream"), Napi::Function::New(env, patchStream));
        exports.Set(Napi::String::New(env, "diffSingleStream"), Napi::Function::New(env, diffSingleStream));
        exports.Set(Napi::String::New(env, "diffWindow"), Napi::Function::New(env, diffWindow));
        exports.Set(Napi::String::New(env, "patchSingleStream"), Napi::Function::New(env, patchSingleStream));
        return exports;
    }

} // namespace hdiffpatchNode

using hdiffpatchNode::Init;
NODE_API_MODULE(hdiffpatch, Init)
