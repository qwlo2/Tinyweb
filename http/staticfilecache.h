#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

enum class StaticFileStatus {
    Ok,
    NotFound,
    Forbidden,
    Error
};

class MappedFile {
public:
    MappedFile(void* data, size_t size) noexcept;
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    const char* Data() const noexcept;
    size_t Size() const noexcept;

private:
    void* data_;
    size_t size_;
};

struct StaticFileLookup {
    std::shared_ptr<const MappedFile> file;
    StaticFileStatus status{StaticFileStatus::NotFound};

    explicit operator bool() const noexcept {
        return status == StaticFileStatus::Ok && file != nullptr;
    }
};

class StaticFileCache {
public:
    static StaticFileCache& Instance();

    StaticFileLookup Get(const std::string& fullPath);
    size_t Preload(const std::string& rootDir,
                   const std::vector<std::string>& relativePaths);
    size_t Size() const;

private:
    enum class CacheEntryState : uint8_t {
        Preloaded,
        AccessedOnce,
        AccessedAtLeastTwice
    };

    struct CacheEntry {
        std::shared_ptr<const MappedFile> file;
        size_t size{0};
        CacheEntryState state{CacheEntryState::Preloaded};
        std::list<std::string>::iterator accessListPosition;
    };

    static constexpr size_t MAX_CACHE_BYTES = 64 * 1024 * 1024;

    StaticFileCache() = default;

    StaticFileLookup Load_(const std::string& fullPath) const;
    void MarkAsPreloaded_(CacheEntry& entry);
    void RecordCacheHit_(const std::string& fullPath, CacheEntry& entry);
    bool EvictLeastRecentlyUsedFile_(
        std::shared_ptr<const MappedFile>& evictedFile);

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, CacheEntry> cachedFiles_;
    std::list<std::string> filesAccessedOnce_;
    std::list<std::string> filesAccessedAtLeastTwice_;
    size_t cachedBytes_{0};
};
