#include "staticfilecache.h"

#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

StaticFileStatus StatusFromOpenError(int error) noexcept {
    if (error == EACCES || error == EPERM) {
        return StaticFileStatus::Forbidden;
    }
    if (error == ENOENT || error == ENOTDIR) {
        return StaticFileStatus::NotFound;
    }
    return StaticFileStatus::Error;
}

}  // namespace

MappedFile::MappedFile(void* data, size_t size) noexcept
    : data_(data), size_(size) {
}

MappedFile::~MappedFile() {
    if (data_ != nullptr && size_ > 0) {
        ::munmap(data_, size_);
    }
}

const char* MappedFile::Data() const noexcept {
    return static_cast<const char*>(data_);
}

size_t MappedFile::Size() const noexcept {
    return size_;
}

StaticFileCache& StaticFileCache::Instance() {
    static StaticFileCache cache;
    return cache;
}

StaticFileLookup StaticFileCache::Get(const std::string& fullPath) {
    // 无需更新访问顺序时走共享锁快速路径，允许多个读取请求并发命中。
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = cachedFiles_.find(fullPath);
        if (it != cachedFiles_.end()) {
            const CacheEntry& entry = it->second;
            const bool isPreloaded =
                entry.state == CacheEntryState::Preloaded;
            const bool isMostRecentFrequentFile =
                entry.state == CacheEntryState::AccessedAtLeastTwice &&
                entry.accessListPosition ==
                    filesAccessedAtLeastTwice_.begin();
            if (isPreloaded || isMostRecentFrequentFile) {
                return {entry.file, StaticFileStatus::Ok};
            }
        }
    }

    // 需要提升热度或刷新访问顺序时，使用独占锁修改缓存元数据。
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = cachedFiles_.find(fullPath);
        if (it != cachedFiles_.end()) {
            RecordCacheHit_(fullPath, it->second);
            return {it->second.file, StaticFileStatus::Ok};
        }
    }

    // 在不持有缓存锁的情况下完成文件 I/O 和 mmap，避免阻塞其他缓存请求。
    StaticFileLookup loaded = Load_(fullPath);
    if (!loaded) {
        return loaded;
    }

    const size_t fileSize = loaded.file->Size();
    // 超过缓存总容量的文件仅服务本次请求，不加入缓存。
    if (fileSize > MAX_CACHE_BYTES) {
        return loaded;
    }

    // 将淘汰文件保留到释放缓存锁之后再析构，避免持锁执行 munmap。
    std::vector<std::shared_ptr<const MappedFile>> evictedFiles;
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Load_ 执行期间其他线程可能已插入同一文件，因此需要再次检查。
    auto existing = cachedFiles_.find(fullPath);
    if (existing != cachedFiles_.end()) {
        RecordCacheHit_(fullPath, existing->second);
        evictedFiles.push_back(std::move(loaded.file));
        loaded.file = existing->second.file;
        return loaded;
    }

    // 空间不足时优先淘汰仅访问一次的文件，再淘汰热链表尾部文件。
    while (cachedBytes_ > MAX_CACHE_BYTES - fileSize) {
        std::shared_ptr<const MappedFile> evictedFile;
        if (!EvictLeastRecentlyUsedFile_(evictedFile)) {
            return loaded;
        }
        evictedFiles.push_back(std::move(evictedFile));
    }

    // 按需加载的新文件从“访问一次”队列开始记录。
    filesAccessedOnce_.push_front(fullPath);
    CacheEntry entry;
    entry.file = loaded.file;
    entry.size = fileSize;
    entry.state = CacheEntryState::AccessedOnce;
    entry.accessListPosition = filesAccessedOnce_.begin();
    cachedFiles_.emplace(fullPath, std::move(entry));
    cachedBytes_ += fileSize;
    return loaded;
}

size_t StaticFileCache::Preload(
    const std::string& rootDir,
    const std::vector<std::string>& relativePaths) {
    size_t loadedCount = 0;
    for (const std::string& relativePath : relativePaths) {
        std::string fullPath = rootDir;
        if (!relativePath.empty() && relativePath.front() != '/') {
            fullPath.push_back('/');
        }
        fullPath.append(relativePath);

        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            auto existing = cachedFiles_.find(fullPath);
            if (existing != cachedFiles_.end()) {
                MarkAsPreloaded_(existing->second);
                ++loadedCount;
                continue;
            }
        }

        StaticFileLookup loaded = Load_(fullPath);
        if (!loaded) {
            continue;
        }

        const size_t fileSize = loaded.file->Size();
        if (fileSize > MAX_CACHE_BYTES) {
            continue;
        }

        std::vector<std::shared_ptr<const MappedFile>> evictedFiles;
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = cachedFiles_.find(fullPath);
        if (it != cachedFiles_.end()) {
            MarkAsPreloaded_(it->second);
            ++loadedCount;
            continue;
        }

        while (cachedBytes_ > MAX_CACHE_BYTES - fileSize) {
            std::shared_ptr<const MappedFile> evictedFile;
            if (!EvictLeastRecentlyUsedFile_(evictedFile)) {
                break;
            }
            evictedFiles.push_back(std::move(evictedFile));
        }
        if (cachedBytes_ > MAX_CACHE_BYTES - fileSize) {
            continue;
        }

        CacheEntry entry;
        entry.file = loaded.file;
        entry.size = fileSize;
        entry.state = CacheEntryState::Preloaded;
        cachedFiles_.emplace(fullPath, std::move(entry));
        cachedBytes_ += fileSize;
        ++loadedCount;
    }
    return loadedCount;
}

size_t StaticFileCache::Size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return cachedFiles_.size();
}

void StaticFileCache::MarkAsPreloaded_(CacheEntry& entry) {
    if (entry.state == CacheEntryState::AccessedOnce) {
        filesAccessedOnce_.erase(entry.accessListPosition);
    } else if (entry.state == CacheEntryState::AccessedAtLeastTwice) {
        filesAccessedAtLeastTwice_.erase(entry.accessListPosition);
    }
    entry.state = CacheEntryState::Preloaded;
}

void StaticFileCache::RecordCacheHit_(
    const std::string& fullPath, CacheEntry& entry) {
    if (entry.state == CacheEntryState::Preloaded) {
        return;
    }

    if (entry.state == CacheEntryState::AccessedOnce) {
        filesAccessedOnce_.erase(entry.accessListPosition);
        filesAccessedAtLeastTwice_.push_front(fullPath);
        entry.accessListPosition = filesAccessedAtLeastTwice_.begin();
        entry.state = CacheEntryState::AccessedAtLeastTwice;
        return;
    }

    if (entry.accessListPosition != filesAccessedAtLeastTwice_.begin()) {
        filesAccessedAtLeastTwice_.splice(
            filesAccessedAtLeastTwice_.begin(),
            filesAccessedAtLeastTwice_, entry.accessListPosition);
        entry.accessListPosition = filesAccessedAtLeastTwice_.begin();
    }
}

bool StaticFileCache::EvictLeastRecentlyUsedFile_(
    std::shared_ptr<const MappedFile>& evictedFile) {
    std::string fileToEvict;
    if (!filesAccessedOnce_.empty()) {
        fileToEvict = std::move(filesAccessedOnce_.back());
        filesAccessedOnce_.pop_back();
    } else if (!filesAccessedAtLeastTwice_.empty()) {
        fileToEvict = std::move(filesAccessedAtLeastTwice_.back());
        filesAccessedAtLeastTwice_.pop_back();
    } else {
        return false;
    }

    auto entry = cachedFiles_.find(fileToEvict);
    if (entry == cachedFiles_.end()) {
        return false;
    }
    cachedBytes_ -= entry->second.size;
    evictedFile = std::move(entry->second.file);
    cachedFiles_.erase(entry);
    return true;
}

StaticFileLookup StaticFileCache::Load_(
    const std::string& fullPath) const {
    int fd = ::open(fullPath.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return {nullptr, StatusFromOpenError(errno)};
    }

    struct stat fileStat {};
    if (::fstat(fd, &fileStat) < 0) {
        ::close(fd);
        return {nullptr, StaticFileStatus::Error};
    }
    if (!S_ISREG(fileStat.st_mode)) {
        ::close(fd);
        return {nullptr, StaticFileStatus::NotFound};
    }
    if (fileStat.st_size < 0) {
        ::close(fd);
        return {nullptr, StaticFileStatus::Error};
    }

    const size_t fileSize = static_cast<size_t>(fileStat.st_size);
    void* data = nullptr;
    if (fileSize > 0) {
        data = ::mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) {
            ::close(fd);
            return {nullptr, StaticFileStatus::Error};
        }
    }
    ::close(fd);

    return {
        std::make_shared<MappedFile>(data, fileSize),
        StaticFileStatus::Ok
    };
}
