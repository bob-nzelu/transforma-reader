// DuplicateCache.h — Memory-mapped binary cache for duplicate invoice detection
// Syncs from Float's sync.db every 60 seconds in background

#pragma once

#include <windows.h>
#include <string>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <atomic>

namespace Helium {

#pragma pack(push, 1)
struct CacheHeader {
    uint32_t version = 1;
    uint32_t entryCount = 0;
    uint64_t lastSyncTimestamp = 0;
};

struct CacheEntry {
    char filename[256];
    uint64_t submitTimestamp;
    char firsReference[32];
    char submittedBy[64];
};
#pragma pack(pop)

enum class DuplicateStatus {
    NotSubmitted,       // Safe to submit
    AlreadySubmitted,   // Duplicate — block submission
    CacheUnavailable    // Cache not loaded — allow with warning
};

struct DuplicateCheckResult {
    DuplicateStatus status;
    std::string firsReference;      // If duplicate, the original reference
    std::string submittedBy;
    uint64_t submitTimestamp = 0;
};

class DuplicateCache {
public:
    DuplicateCache();
    ~DuplicateCache();

    // Initialize cache from disk file
    bool Load(const std::wstring& cachePath);

    // Check if a filename has been submitted before
    DuplicateCheckResult Check(const std::string& filename);

    // Add entry after successful submission
    void AddEntry(const std::string& filename, const std::string& firsRef, const std::string& user);

    // Start background sync thread (syncs from Float's sync.db every 60s)
    void StartBackgroundSync(const std::wstring& syncDbPath);

    // Stop background sync
    void StopBackgroundSync();

    // Persist cache to disk
    bool Save();

private:
    std::wstring m_cachePath;
    std::vector<CacheEntry> m_entries;
    std::unordered_set<std::string> m_filenameIndex; // Fast lookup
    std::mutex m_mutex;

    // Background sync
    std::thread m_syncThread;
    std::atomic<bool> m_running{false};
    std::wstring m_syncDbPath;

    void SyncLoop();
    void SyncFromDatabase();
};

} // namespace Helium
