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
    // Helper: 从参数获取数据指针和长度（支持 Buffer 和 TypedArray）
    inline bool getBufferData(const Napi::Value& arg, const uint8_t** data, size_t* length) {
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

    inline bool getStringUtf8(const Napi::Value& arg, std::string& out) {
        if (!arg.IsString()) return false;
        out = arg.As<Napi::String>().Utf8Value();
        return true;
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
    class DiffAsyncWorker : public Napi::AsyncWorker {
    public:
        DiffAsyncWorker(Napi::Function& callback,
                        const Napi::Value& oldValue, const uint8_t* oldData, size_t oldLen,
                        const Napi::Value& newValue, const uint8_t* newData, size_t newLen)
            : Napi::AsyncWorker(callback),
              oldData_(oldData),
              oldLen_(oldLen),
              newData_(newData),
              newLen_(newLen),
              oldRef_(Napi::Persistent(oldValue)),
              newRef_(Napi::Persistent(newValue)) {
        }

        void Execute() override {
            try {
                hdiff(oldData_, oldLen_,
                      newData_, newLen_, result_);
            } catch (const std::exception& e) {
                SetError(e.what());
            }
        }

        void OnOK() override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Napi::Buffer<uint8_t> resultBuf = bufferFromVector(env, std::move(result_));
            Callback().Call({env.Null(), resultBuf});
            oldRef_.Reset();
            newRef_.Reset();
        }

        void OnError(const Napi::Error& e) override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Callback().Call({e.Value()});
            oldRef_.Reset();
            newRef_.Reset();
        }

    private:
        const uint8_t* oldData_;
        size_t oldLen_;
        const uint8_t* newData_;
        size_t newLen_;
        Napi::Reference<Napi::Value> oldRef_;
        Napi::Reference<Napi::Value> newRef_;
        std::vector<uint8_t> result_;
    };

    // ============ 异步 Patch Worker ============
    class PatchAsyncWorker : public Napi::AsyncWorker {
    public:
        PatchAsyncWorker(Napi::Function& callback,
                         const Napi::Value& oldValue, const uint8_t* oldData, size_t oldLen,
                         const Napi::Value& diffValue, const uint8_t* diffData, size_t diffLen)
            : Napi::AsyncWorker(callback),
              oldData_(oldData),
              oldLen_(oldLen),
              diffData_(diffData),
              diffLen_(diffLen),
              oldRef_(Napi::Persistent(oldValue)),
              diffRef_(Napi::Persistent(diffValue)) {
        }

        void Execute() override {
            try {
                hpatch(oldData_, oldLen_,
                       diffData_, diffLen_, result_);
            } catch (const std::exception& e) {
                SetError(e.what());
            }
        }

        void OnOK() override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Napi::Buffer<uint8_t> resultBuf = bufferFromVector(env, std::move(result_));
            Callback().Call({env.Null(), resultBuf});
            oldRef_.Reset();
            diffRef_.Reset();
        }

        void OnError(const Napi::Error& e) override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            Callback().Call({e.Value()});
            oldRef_.Reset();
            diffRef_.Reset();
        }

    private:
        const uint8_t* oldData_;
        size_t oldLen_;
        const uint8_t* diffData_;
        size_t diffLen_;
        Napi::Reference<Napi::Value> oldRef_;
        Napi::Reference<Napi::Value> diffRef_;
        std::vector<uint8_t> result_;
    };

    // ============ 异步 Stream Diff Worker ============
    class DiffStreamAsyncWorker : public Napi::AsyncWorker {
    public:
        DiffStreamAsyncWorker(Napi::Function& callback,
                              std::string oldPath,
                              std::string newPath,
                              std::string outDiffPath)
            : Napi::AsyncWorker(callback),
              oldPath_(std::move(oldPath)),
              newPath_(std::move(newPath)),
              outDiffPath_(std::move(outDiffPath)) {
        }

        void Execute() override {
            try {
                hdiff_stream(oldPath_.c_str(), newPath_.c_str(), outDiffPath_.c_str());
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
    };

    // ============ 异步 Stream Patch Worker ============
    class PatchStreamAsyncWorker : public Napi::AsyncWorker {
    public:
        PatchStreamAsyncWorker(Napi::Function& callback,
                               std::string oldPath,
                               std::string diffPath,
                               std::string outNewPath)
            : Napi::AsyncWorker(callback),
              oldPath_(std::move(oldPath)),
              diffPath_(std::move(diffPath)),
              outNewPath_(std::move(outNewPath)) {
        }

        void Execute() override {
            try {
                hpatch_stream(oldPath_.c_str(), diffPath_.c_str(), outNewPath_.c_str());
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
    };

    // ============ 异步 Single-compressed Patch Worker ============
    class PatchSingleStreamAsyncWorker : public Napi::AsyncWorker {
    public:
        PatchSingleStreamAsyncWorker(Napi::Function& callback,
                                     std::string oldPath,
                                     std::string diffPath,
                                     std::string outNewPath)
            : Napi::AsyncWorker(callback),
              oldPath_(std::move(oldPath)),
              diffPath_(std::move(diffPath)),
              outNewPath_(std::move(outNewPath)) {
        }

        void Execute() override {
            try {
                hpatch_single_stream(oldPath_.c_str(), diffPath_.c_str(), outNewPath_.c_str());
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
    };

    // ============ 异步 Single-compressed Stream Diff Worker ============
    class DiffSingleStreamAsyncWorker : public Napi::AsyncWorker {
    public:
        DiffSingleStreamAsyncWorker(Napi::Function& callback,
                                    std::string oldPath,
                                    std::string newPath,
                                    std::string outDiffPath)
            : Napi::AsyncWorker(callback),
              oldPath_(std::move(oldPath)),
              newPath_(std::move(newPath)),
              outDiffPath_(std::move(outDiffPath)) {
        }

        void Execute() override {
            try {
                hdiff_single_stream(oldPath_.c_str(), newPath_.c_str(), outDiffPath_.c_str());
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

        // 如果提供了回调函数，使用异步模式
        if (info.Length() > 2 && info[2].IsFunction()) {
            Napi::Function callback = info[2].As<Napi::Function>();
            DiffAsyncWorker* worker = new DiffAsyncWorker(
                callback, info[0], oldData, oldLength, info[1], newData, newLength
            );
            worker->Queue();
            return env.Undefined();
        }

        // 同步模式
        std::vector<uint8_t> codeBuf;
        try {
            hdiff(oldData, oldLength, newData, newLength, codeBuf);
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

        // 如果提供了回调函数，使用异步模式
        if (info.Length() > 2 && info[2].IsFunction()) {
            Napi::Function callback = info[2].As<Napi::Function>();
            PatchAsyncWorker* worker = new PatchAsyncWorker(
                callback, info[0], oldData, oldLength, info[1], diffData, diffLength
            );
            worker->Queue();
            return env.Undefined();
        }

        // 同步模式
        std::vector<uint8_t> newBuf;
        try {
            hpatch(oldData, oldLength, diffData, diffLength, newBuf);
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

        if (info.Length() > 3 && info[3].IsFunction()) {
            Napi::Function callback = info[3].As<Napi::Function>();
            DiffStreamAsyncWorker* worker = new DiffStreamAsyncWorker(
                callback, oldPath, newPath, outDiffPath
            );
            worker->Queue();
            return env.Undefined();
        }

        try {
            hdiff_stream(oldPath.c_str(), newPath.c_str(), outDiffPath.c_str());
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

        if (info.Length() > 3 && info[3].IsFunction()) {
            Napi::Function callback = info[3].As<Napi::Function>();
            PatchStreamAsyncWorker* worker = new PatchStreamAsyncWorker(
                callback, oldPath, diffPath, outNewPath
            );
            worker->Queue();
            return env.Undefined();
        }

        try {
            hpatch_stream(oldPath.c_str(), diffPath.c_str(), outNewPath.c_str());
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
                              size_t windowSize)
            : Napi::AsyncWorker(callback),
              oldPath_(std::move(oldPath)),
              newPath_(std::move(newPath)),
              outDiffPath_(std::move(outDiffPath)),
              windowSize_(windowSize) {
        }

        void Execute() override {
            try {
                hdiff_window(oldPath_.c_str(), newPath_.c_str(), outDiffPath_.c_str(),
                             windowSize_);
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

        if (info.Length() > 3 && info[3].IsFunction()) {
            Napi::Function callback = info[3].As<Napi::Function>();
            DiffSingleStreamAsyncWorker* worker = new DiffSingleStreamAsyncWorker(
                callback, oldPath, newPath, outDiffPath
            );
            worker->Queue();
            return env.Undefined();
        }

        try {
            hdiff_single_stream(oldPath.c_str(), newPath.c_str(), outDiffPath.c_str());
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

        size_t windowSize = 0;  // 0 = 使用默认窗口(2MB)
        size_t argIdx = 3;
        if (info.Length() > argIdx && info[argIdx].IsNumber()) {
            double raw = info[argIdx].As<Napi::Number>().DoubleValue();
            if (!std::isfinite(raw) || raw < 0 ||
                raw > static_cast<double>(std::numeric_limits<size_t>::max())) {
                Napi::TypeError::New(env, "Invalid windowSize: expected a non-negative integer.")
                    .ThrowAsJavaScriptException();
                return env.Undefined();
            }
            windowSize = static_cast<size_t>(raw);
            argIdx++;
        }

        if (info.Length() > argIdx && info[argIdx].IsFunction()) {
            Napi::Function callback = info[argIdx].As<Napi::Function>();
            DiffWindowAsyncWorker* worker = new DiffWindowAsyncWorker(
                callback, oldPath, newPath, outDiffPath, windowSize
            );
            worker->Queue();
            return env.Undefined();
        }

        try {
            hdiff_window(oldPath.c_str(), newPath.c_str(), outDiffPath.c_str(), windowSize);
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

        if (info.Length() > 3 && info[3].IsFunction()) {
            Napi::Function callback = info[3].As<Napi::Function>();
            PatchSingleStreamAsyncWorker* worker = new PatchSingleStreamAsyncWorker(
                callback, oldPath, diffPath, outNewPath
            );
            worker->Queue();
            return env.Undefined();
        }

        try {
            hpatch_single_stream(oldPath.c_str(), diffPath.c_str(), outNewPath.c_str());
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
