/**
 * Created by housisong on 2021.04.07.
 * Optimized for node-hdiffpatch with async support for large files
 */
#include <nan.h>
#include <node.h>
#include <node_buffer.h>
#include <cstdlib>
#include <string>
#include "hdiff.h"
#include "hpatch.h"

namespace hdiffpatchNode
{
    using v8::FunctionCallbackInfo;
    using v8::HandleScope;
    using v8::Isolate;
    using v8::Local;
    using v8::Object;
    using v8::Value;
    using v8::ArrayBufferView;

    // Helper: 从参数获取数据指针和长度（支持 Buffer 和 TypedArray）
    inline bool getBufferData(const Local<Value>& arg, const uint8_t** data, size_t* length) {
        if (node::Buffer::HasInstance(arg)) {
            *data = (const uint8_t*)node::Buffer::Data(arg);
            *length = node::Buffer::Length(arg);
            return true;
        }
        if (arg->IsArrayBufferView()) {
            Local<ArrayBufferView> view = arg.As<ArrayBufferView>();
            *data = (const uint8_t*)view->Buffer()->Data() + view->ByteOffset();
            *length = view->ByteLength();
            return true;
        }
        return false;
    }

    // 自定义删除器，用于 Buffer::New 的外部内存管理
    static void vectorDeleter(char* data, void* hint) {
        std::vector<uint8_t>* vec = static_cast<std::vector<uint8_t>*>(hint);
        delete vec;
    }

    // ============ 异步 Diff Worker ============
    class DiffAsyncWorker : public Nan::AsyncWorker {
    public:
        DiffAsyncWorker(Nan::Callback* callback,
                        const uint8_t* oldData, size_t oldLen,
                        const uint8_t* newData, size_t newLen)
            : Nan::AsyncWorker(callback), result_(new std::vector<uint8_t>()) {
            // 复制数据到工作线程（因为原始 Buffer 可能在 JS 中被 GC）
            oldData_.assign(oldData, oldData + oldLen);
            newData_.assign(newData, newData + newLen);
        }

        void Execute() override {
            try {
                hdiff(oldData_.data(), oldData_.size(),
                      newData_.data(), newData_.size(), *result_);
            } catch (const std::exception& e) {
                SetErrorMessage(e.what());
            }
        }

        void HandleOKCallback() override {
            Nan::HandleScope scope;
            // 零拷贝返回
            Local<Object> resultBuf = Nan::NewBuffer(
                (char*)result_->data(),
                result_->size(),
                vectorDeleter,
                result_
            ).ToLocalChecked();
            result_ = nullptr; // 所有权已转移
            Local<Value> argv[] = { Nan::Null(), resultBuf };
            callback->Call(2, argv, async_resource);
        }

        void HandleErrorCallback() override {
            Nan::HandleScope scope;
            delete result_;
            Local<Value> argv[] = { Nan::Error(ErrorMessage()) };
            callback->Call(1, argv, async_resource);
        }

    private:
        std::vector<uint8_t> oldData_;
        std::vector<uint8_t> newData_;
        std::vector<uint8_t>* result_;
    };

    // ============ 异步 Patch Worker ============
    class PatchAsyncWorker : public Nan::AsyncWorker {
    public:
        PatchAsyncWorker(Nan::Callback* callback,
                         const uint8_t* oldData, size_t oldLen,
                         const uint8_t* diffData, size_t diffLen)
            : Nan::AsyncWorker(callback), result_(new std::vector<uint8_t>()) {
            oldData_.assign(oldData, oldData + oldLen);
            diffData_.assign(diffData, diffData + diffLen);
        }

        void Execute() override {
            try {
                hpatch(oldData_.data(), oldData_.size(),
                       diffData_.data(), diffData_.size(), *result_);
            } catch (const std::exception& e) {
                SetErrorMessage(e.what());
            }
        }

        void HandleOKCallback() override {
            Nan::HandleScope scope;
            Local<Object> resultBuf = Nan::NewBuffer(
                (char*)result_->data(),
                result_->size(),
                vectorDeleter,
                result_
            ).ToLocalChecked();
            result_ = nullptr;
            Local<Value> argv[] = { Nan::Null(), resultBuf };
            callback->Call(2, argv, async_resource);
        }

        void HandleErrorCallback() override {
            Nan::HandleScope scope;
            delete result_;
            Local<Value> argv[] = { Nan::Error(ErrorMessage()) };
            callback->Call(1, argv, async_resource);
        }

    private:
        std::vector<uint8_t> oldData_;
        std::vector<uint8_t> diffData_;
        std::vector<uint8_t>* result_;
    };

    // ============ 同步 diff ============
    void diff(const FunctionCallbackInfo<Value>& args) {
        Isolate* isolate = args.GetIsolate();
        HandleScope scope(isolate);

        const uint8_t* oldData = nullptr;
        size_t oldLength = 0;
        const uint8_t* newData = nullptr;
        size_t newLength = 0;

        if (!getBufferData(args[0], &oldData, &oldLength) ||
            !getBufferData(args[1], &newData, &newLength)) {
            Nan::ThrowTypeError("Invalid arguments: expected Buffer or TypedArray.");
            return;
        }

        // 如果提供了回调函数，使用异步模式
        if (args.Length() > 2 && args[2]->IsFunction()) {
            Nan::Callback* callback = new Nan::Callback(args[2].As<v8::Function>());
            Nan::AsyncQueueWorker(new DiffAsyncWorker(callback, oldData, oldLength, newData, newLength));
            return;
        }

        // 同步模式
        std::vector<uint8_t>* codeBuf = new std::vector<uint8_t>();
        try {
            hdiff(oldData, oldLength, newData, newLength, *codeBuf);
        } catch (const std::exception& e) {
            delete codeBuf;
            Nan::ThrowError(e.what());
            return;
        }

        Local<Object> result = Nan::NewBuffer(
            (char*)codeBuf->data(),
            codeBuf->size(),
            vectorDeleter,
            codeBuf
        ).ToLocalChecked();
        args.GetReturnValue().Set(result);
    }

    // ============ 同步 patch ============
    void patch(const FunctionCallbackInfo<Value>& args) {
        Isolate* isolate = args.GetIsolate();
        HandleScope scope(isolate);

        const uint8_t* oldData = nullptr;
        size_t oldLength = 0;
        const uint8_t* diffData = nullptr;
        size_t diffLength = 0;

        if (!getBufferData(args[0], &oldData, &oldLength) ||
            !getBufferData(args[1], &diffData, &diffLength)) {
            Nan::ThrowTypeError("Invalid arguments: expected Buffer or TypedArray (old, diff).");
            return;
        }

        if (diffLength < 4) {
            Nan::ThrowError("Invalid diff data: too short.");
            return;
        }

        // 如果提供了回调函数，使用异步模式
        if (args.Length() > 2 && args[2]->IsFunction()) {
            Nan::Callback* callback = new Nan::Callback(args[2].As<v8::Function>());
            Nan::AsyncQueueWorker(new PatchAsyncWorker(callback, oldData, oldLength, diffData, diffLength));
            return;
        }

        // 同步模式
        std::vector<uint8_t>* newBuf = new std::vector<uint8_t>();
        try {
            hpatch(oldData, oldLength, diffData, diffLength, *newBuf);
        } catch (const std::exception& e) {
            delete newBuf;
            Nan::ThrowError(e.what());
            return;
        }

        Local<Object> result = Nan::NewBuffer(
            (char*)newBuf->data(),
            newBuf->size(),
            vectorDeleter,
            newBuf
        ).ToLocalChecked();
        args.GetReturnValue().Set(result);
    }

    void init(Local<Object> exports)
    {
        Isolate* isolate = exports->GetIsolate();
        HandleScope scope(isolate);

        NODE_SET_METHOD(exports, "diff", diff);
        NODE_SET_METHOD(exports, "patch", patch);
    }

    NODE_MODULE(hdiffpatch, init)

} // namespace hdiffpatch
