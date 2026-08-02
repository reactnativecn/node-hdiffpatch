/**
 * TempOutputFile - 文件型输出的事务化落盘
 *
 * 所有文件型 diff/patch 先写入目标目录内的随机临时文件(排他创建),
 * 全部生成与回放验证通过后再原子 rename 到最终路径;任何失败都删除
 * 临时文件、保留既有目标文件。这同时使"输出路径即输入路径"(含相对
 * 路径别名、硬链接、符号链接指向同一 inode)成为安全操作:输入在
 * commit 前不会被打开写或截断。
 */

#ifndef HDIFFPATCH_TEMP_OUTPUT_H
#define HDIFFPATCH_TEMP_OUTPUT_H

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace hdiffpatchNode {

#ifdef _WIN32
inline std::wstring pathUtf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wlen <= 0) throw std::runtime_error("invalid utf8 path.");
    std::wstring wide(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], wlen);
    wide.resize(static_cast<size_t>(wlen) - 1);  // drop the trailing NUL
    return wide;
}
#endif

class TempOutputFile {
public:
    TempOutputFile() = default;
    TempOutputFile(const TempOutputFile&) = delete;
    TempOutputFile& operator=(const TempOutputFile&) = delete;

    ~TempOutputFile() {
        if (!committed_ && !tempPath_.empty()) {
            removeQuietly(tempPath_);
        }
    }

    // 在 finalPath 同目录排他创建随机命名的临时文件(同目录保证 rename 原子)
    void create(const std::string& finalPath) {
        if (finalPath.empty()) throw std::runtime_error("Invalid file path.");
        finalPath_ = finalPath;
        std::random_device rd;
        for (int attempt = 0; attempt < 16; ++attempt) {
            uint64_t r = (static_cast<uint64_t>(rd()) << 32) ^ rd();
            char suffix[32];
            std::snprintf(suffix, sizeof(suffix), ".%016llx.hdptmp",
                          static_cast<unsigned long long>(r));
            std::string candidate = finalPath_ + suffix;
            if (createExclusive(candidate)) {
                tempPath_ = candidate;
                return;
            }
        }
        throw std::runtime_error("create temp output file failed (too many collisions).");
    }

    const char* path() const {
        if (tempPath_.empty()) throw std::runtime_error("temp output file not created.");
        return tempPath_.c_str();
    }

    // flush 已由文件关闭完成;这里 fsync 后原子替换到最终路径
    void commit() {
        if (tempPath_.empty()) throw std::runtime_error("temp output file not created.");
        syncToDisk();
#ifdef _WIN32
        std::wstring tempW = pathUtf8ToWide(tempPath_);
        std::wstring finalW = pathUtf8ToWide(finalPath_);
        if (!MoveFileExW(tempW.c_str(), finalW.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("commit output file failed (replace).");
        }
#else
        if (0 != std::rename(tempPath_.c_str(), finalPath_.c_str())) {
            throw std::runtime_error(std::string("commit output file failed (rename): ") +
                                     std::strerror(errno));
        }
#endif
        committed_ = true;
    }

private:
    bool createExclusive(const std::string& path) {
#ifdef _WIN32
        std::wstring pathW = pathUtf8ToWide(path);
        HANDLE h = CreateFileW(pathW.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS) return false;
            throw std::runtime_error("create temp output file failed.");
        }
        CloseHandle(h);
        return true;
#else
        int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0644);
        if (fd < 0) {
            if (errno == EEXIST) return false;
            throw std::runtime_error(std::string("create temp output file failed: ") +
                                     std::strerror(errno));
        }
        ::close(fd);
        return true;
#endif
    }

    void syncToDisk() {
#ifdef _WIN32
        std::wstring pathW = pathUtf8ToWide(tempPath_);
        HANDLE h = CreateFileW(pathW.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("sync temp output file failed (open).");
        }
        BOOL ok = FlushFileBuffers(h);
        CloseHandle(h);
        if (!ok) throw std::runtime_error("sync temp output file failed (flush).");
#else
        int fd = ::open(tempPath_.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            throw std::runtime_error("sync temp output file failed (open).");
        }
        int rc = ::fsync(fd);
        ::close(fd);
        // 个别文件系统对 fsync 返回 EINVAL/ENOTSUP,视为尽力而为
        if (rc != 0 && errno != EINVAL && errno != ENOTSUP) {
            throw std::runtime_error(std::string("sync temp output file failed: ") +
                                     std::strerror(errno));
        }
#endif
    }

    static void removeQuietly(const std::string& path) {
#ifdef _WIN32
        try {
            std::wstring pathW = pathUtf8ToWide(path);
            DeleteFileW(pathW.c_str());
        } catch (...) {
        }
#else
        std::remove(path.c_str());
#endif
    }

    std::string finalPath_;
    std::string tempPath_;
    bool committed_ = false;
};

} // namespace hdiffpatchNode

#endif
