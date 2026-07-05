/**
 * node-hdiffpatch - Node-API implementation
 * Refactored from NAN for Bun compatibility
 * Created by housisong on 2021.04.07, refactored 2026.01.20
 */
#include <napi.h>
#include <cerrno>
#include <cmath>
#include <cstdlib>
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

    inline bool parseUint64String(const std::string& value, uint64_t* out) {
        if (value.empty()) return false;
        errno = 0;
        char* end = nullptr;
        unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
        if (errno == ERANGE || end == value.c_str() || *end != '\0') return false;
        *out = static_cast<uint64_t>(parsed);
        return true;
    }

    inline bool getUint64Value(const Napi::Value& value, uint64_t* out) {
        if (value.IsString()) {
            return parseUint64String(value.As<Napi::String>().Utf8Value(), out);
        }
        if (value.IsNumber()) {
            const double number = value.As<Napi::Number>().DoubleValue();
            if (!std::isfinite(number) || number < 0 || std::floor(number) != number ||
                number > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
                return false;
            }
            *out = static_cast<uint64_t>(number);
            return true;
        }
        if (value.IsBigInt()) {
            bool lossless = false;
            uint64_t parsed = value.As<Napi::BigInt>().Uint64Value(&lossless);
            if (!lossless) return false;
            *out = parsed;
            return true;
        }
        return false;
    }

    inline bool getCoverField(const Napi::Object& cover, const char* primary,
                              const char* fallback, uint64_t* out) {
        if (cover.Has(primary) && getUint64Value(cover.Get(primary), out)) {
            return true;
        }
        if (fallback && cover.Has(fallback) && getUint64Value(cover.Get(fallback), out)) {
            return true;
        }
        return false;
    }

    std::vector<HdiffCover> getCoverList(const Napi::Value& arg) {
        if (!arg.IsArray()) {
            throw std::runtime_error("Invalid arguments: expected covers array.");
        }

        Napi::Array array = arg.As<Napi::Array>();
        std::vector<HdiffCover> covers;
        covers.reserve(array.Length());

        for (uint32_t i = 0; i < array.Length(); ++i) {
            Napi::Value item = array.Get(i);
            if (!item.IsObject()) {
                throw std::runtime_error("Invalid cover: expected object.");
            }
            Napi::Object coverObject = item.As<Napi::Object>();
            HdiffCover cover = {0, 0, 0};
            if (!getCoverField(coverObject, "oldPos", "old_pos", &cover.oldPos) ||
                !getCoverField(coverObject, "newPos", "new_pos", &cover.newPos) ||
                !getCoverField(coverObject, "len", "length", &cover.length)) {
                throw std::runtime_error("Invalid cover: expected oldPos, newPos, and len.");
            }
            covers.push_back(cover);
        }

        return covers;
    }

    HdiffCoverMode getCoverMode(const Napi::CallbackInfo& info, size_t optionsIndex) {
        if (info.Length() <= optionsIndex || info[optionsIndex].IsUndefined() || info[optionsIndex].IsNull()) {
            return HdiffCoverMode::Replace;
        }
        if (!info[optionsIndex].IsObject()) {
            throw std::runtime_error("Invalid options: expected object.");
        }

        Napi::Object options = info[optionsIndex].As<Napi::Object>();
        if (!options.Has("mode") || options.Get("mode").IsUndefined() || options.Get("mode").IsNull()) {
            return HdiffCoverMode::Replace;
        }
        if (!options.Get("mode").IsString()) {
            throw std::runtime_error("Invalid options.mode: expected 'replace', 'merge', or 'native-coalesce'.");
        }

        const std::string mode = options.Get("mode").As<Napi::String>().Utf8Value();
        if (mode == "replace") {
            return HdiffCoverMode::Replace;
        }
        if (mode == "merge") {
            return HdiffCoverMode::Merge;
        }
        if (mode == "native-coalesce" || mode == "native_coalesce") {
            return HdiffCoverMode::NativeCoalesce;
        }
        throw std::runtime_error("Invalid options.mode: expected 'replace', 'merge', or 'native-coalesce'.");
    }

    bool getDebugCoversOption(const Napi::CallbackInfo& info, size_t optionsIndex) {
        if (info.Length() <= optionsIndex || info[optionsIndex].IsUndefined() || info[optionsIndex].IsNull()) {
            return false;
        }
        if (!info[optionsIndex].IsObject()) {
            throw std::runtime_error("Invalid options: expected object.");
        }

        Napi::Object options = info[optionsIndex].As<Napi::Object>();
        if (!options.Has("debugCovers")) {
            return false;
        }
        Napi::Value debugCovers = options.Get("debugCovers");
        return debugCovers.IsBoolean() && debugCovers.As<Napi::Boolean>().Value();
    }

    const char* coverModeName(HdiffCoverMode mode) {
        switch (mode) {
            case HdiffCoverMode::Merge:
                return "merge";
            case HdiffCoverMode::NativeCoalesce:
                return "native-coalesce";
            case HdiffCoverMode::Replace:
            default:
                return "replace";
        }
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

    Napi::Object coverToObject(Napi::Env env, const HdiffCover& cover) {
        Napi::Object object = Napi::Object::New(env);
        object.Set(Napi::String::New(env, "oldPos"), Napi::String::New(env, std::to_string(cover.oldPos)));
        object.Set(Napi::String::New(env, "newPos"), Napi::String::New(env, std::to_string(cover.newPos)));
        object.Set(Napi::String::New(env, "len"), Napi::String::New(env, std::to_string(cover.length)));
        return object;
    }

    Napi::Array coversToArray(Napi::Env env, const std::vector<HdiffCover>& covers) {
        Napi::Array array = Napi::Array::New(env, covers.size());
        for (uint32_t i = 0; i < covers.size(); ++i) {
            array.Set(i, coverToObject(env, covers[i]));
        }
        return array;
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

    // ============ 同步 diffWithCovers ============
    Napi::Value diffWithCovers(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        const uint8_t* oldData = nullptr;
        size_t oldLength = 0;
        const uint8_t* newData = nullptr;
        size_t newLength = 0;

        if (info.Length() < 3 ||
            !getBufferData(info[0], &oldData, &oldLength) ||
            !getBufferData(info[1], &newData, &newLength)) {
            Napi::TypeError::New(env, "Invalid arguments: expected Buffer or TypedArray and covers array.")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        std::vector<HdiffCover> covers;
        HdiffCoverMode coverMode = HdiffCoverMode::Replace;
        bool debugCovers = false;
        try {
            covers = getCoverList(info[2]);
            coverMode = getCoverMode(info, 3);
            debugCovers = getDebugCoversOption(info, 3);
        } catch (const std::exception& e) {
            Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
            return env.Undefined();
        }

        std::vector<uint8_t> codeBuf;
        HdiffWithCoversResult coverResult = {false, 0, 0, 0, {}, {}};
        try {
            coverResult = hdiff_with_covers(oldData, oldLength, newData, newLength,
                                            covers.empty() ? nullptr : covers.data(),
                                            covers.size(), coverMode, codeBuf);
        } catch (const std::exception& e) {
            Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            return env.Undefined();
        }

        Napi::Object result = Napi::Object::New(env);
        result.Set(Napi::String::New(env, "diff"), bufferFromVector(env, std::move(codeBuf)));
        result.Set(Napi::String::New(env, "usedCovers"), Napi::Boolean::New(env, coverResult.usedCovers));
        result.Set(Napi::String::New(env, "requestedCoverCount"),
                   Napi::Number::New(env, static_cast<double>(coverResult.requestedCoverCount)));
        result.Set(Napi::String::New(env, "nativeCoverCapacity"),
                   Napi::Number::New(env, static_cast<double>(coverResult.nativeCoverCapacity)));
        result.Set(Napi::String::New(env, "finalCoverCount"),
                   Napi::Number::New(env, static_cast<double>(coverResult.finalCoverCount)));
        result.Set(Napi::String::New(env, "coverMode"),
                   Napi::String::New(env, coverModeName(coverMode)));
        if (debugCovers) {
            result.Set(Napi::String::New(env, "nativeCovers"), coversToArray(env, coverResult.nativeCovers));
            result.Set(Napi::String::New(env, "finalCovers"), coversToArray(env, coverResult.finalCovers));
        }
        return result;
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
        exports.Set(Napi::String::New(env, "diffWithCovers"), Napi::Function::New(env, diffWithCovers));
        exports.Set(Napi::String::New(env, "patch"), Napi::Function::New(env, patch));
        exports.Set(Napi::String::New(env, "diffStream"), Napi::Function::New(env, diffStream));
        exports.Set(Napi::String::New(env, "patchStream"), Napi::Function::New(env, patchStream));
        exports.Set(Napi::String::New(env, "patchSingleStream"), Napi::Function::New(env, patchSingleStream));
        return exports;
    }

} // namespace hdiffpatchNode

using hdiffpatchNode::Init;
NODE_API_MODULE(hdiffpatch, Init)
