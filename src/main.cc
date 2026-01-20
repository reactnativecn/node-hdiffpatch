/**
 * node-hdiffpatch - Node-API implementation
 * Refactored from NAN for Bun compatibility
 * Created by housisong on 2021.04.07, refactored 2026.01.20
 */
#include <napi.h>
#include <cstdlib>
#include <string>
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

    // ============ 异步 Diff Worker ============
    class DiffAsyncWorker : public Napi::AsyncWorker {
    public:
        DiffAsyncWorker(Napi::Function& callback,
                        const uint8_t* oldData, size_t oldLen,
                        const uint8_t* newData, size_t newLen)
            : Napi::AsyncWorker(callback) {
            // 复制数据到工作线程（因为原始 Buffer 可能在 JS 中被 GC）
            oldData_.assign(oldData, oldData + oldLen);
            newData_.assign(newData, newData + newLen);
        }

        void Execute() override {
            try {
                hdiff(oldData_.data(), oldData_.size(),
                      newData_.data(), newData_.size(), result_);
            } catch (const std::exception& e) {
                SetError(e.what());
            }
        }

        void OnOK() override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            
            // 创建 Buffer 并复制数据
            Napi::Buffer<uint8_t> resultBuf = Napi::Buffer<uint8_t>::Copy(
                env, result_.data(), result_.size()
            );
            
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
        std::vector<uint8_t> result_;
    };

    // ============ 异步 Patch Worker ============
    class PatchAsyncWorker : public Napi::AsyncWorker {
    public:
        PatchAsyncWorker(Napi::Function& callback,
                         const uint8_t* oldData, size_t oldLen,
                         const uint8_t* diffData, size_t diffLen)
            : Napi::AsyncWorker(callback) {
            oldData_.assign(oldData, oldData + oldLen);
            diffData_.assign(diffData, diffData + diffLen);
        }

        void Execute() override {
            try {
                hpatch(oldData_.data(), oldData_.size(),
                       diffData_.data(), diffData_.size(), result_);
            } catch (const std::exception& e) {
                SetError(e.what());
            }
        }

        void OnOK() override {
            Napi::Env env = Env();
            Napi::HandleScope scope(env);
            
            Napi::Buffer<uint8_t> resultBuf = Napi::Buffer<uint8_t>::Copy(
                env, result_.data(), result_.size()
            );
            
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
        std::vector<uint8_t> result_;
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
                callback, oldData, oldLength, newData, newLength
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

        return Napi::Buffer<uint8_t>::Copy(env, codeBuf.data(), codeBuf.size());
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
                callback, oldData, oldLength, diffData, diffLength
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

        return Napi::Buffer<uint8_t>::Copy(env, newBuf.data(), newBuf.size());
    }

    Napi::Object Init(Napi::Env env, Napi::Object exports) {
        exports.Set(Napi::String::New(env, "diff"), Napi::Function::New(env, diff));
        exports.Set(Napi::String::New(env, "patch"), Napi::Function::New(env, patch));
        return exports;
    }

    NODE_API_MODULE(hdiffpatch, Init)

} // namespace hdiffpatch
