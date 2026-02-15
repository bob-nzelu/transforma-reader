// DuplicateCache.cpp — Memory-mapped binary cache for duplicate detection

#include "DuplicateCache.h"
#include <fstream>
#include <cstring>
#include <ctime>

namespace Helium {

DuplicateCache::DuplicateCache() {}

DuplicateCache::~DuplicateCache() {
    StopBackgroundSync();
}

bool DuplicateCache::Load(const std::wstring& cachePath) {
    m_cachePath = cachePath;
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ifstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        // No cache file yet — start fresh
        return true;
    }

    CacheHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (header.version != 1) {
        // Unknown version — start fresh
        return true;
    }

    m_entries.resize(header.entryCount);
    file.read(reinterpret_cast<char*>(m_entries.data()),
              header.entryCount * sizeof(CacheEntry));
    file.close();

    // Build index
    m_filenameIndex.clear();
    for (const auto& entry : m_entries) {
        m_filenameIndex.insert(std::string(entry.filename));
    }

    return true;
}

DuplicateCheckResult DuplicateCache::Check(const std::string& filename) {
    DuplicateCheckResult result;

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_filenameIndex.find(filename) == m_filenameIndex.end()) {
        result.status = DuplicateStatus::NotSubmitted;
        return result;
    }

    // Find the full entry for details
    for (const auto& entry : m_entries) {
        if (std::string(entry.filename) == filename) {
            result.status = DuplicateStatus::AlreadySubmitted;
            result.firsReference = entry.firsReference;
            result.submittedBy = entry.submittedBy;
            result.submitTimestamp = entry.submitTimestamp;
            return result;
        }
    }

    // Shouldn't reach here, but just in case
    result.status = DuplicateStatus::AlreadySubmitted;
    return result;
}

void DuplicateCache::AddEntry(const std::string& filename, const std::string& firsRef, const std::string& user) {
    std::lock_guard<std::mutex> lock(m_mutex);

    CacheEntry entry = {};
    strncpy_s(entry.filename, filename.c_str(), sizeof(entry.filename) - 1);
    strncpy_s(entry.firsReference, firsRef.c_str(), sizeof(entry.firsReference) - 1);
    strncpy_s(entry.submittedBy, user.c_str(), sizeof(entry.submittedBy) - 1);
    entry.submitTimestamp = (uint64_t)time(nullptr);

    m_entries.push_back(entry);
    m_filenameIndex.insert(filename);

    // Persist immediately
    Save();
}

bool DuplicateCache::Save() {
    if (m_cachePath.empty()) return false;

    std::ofstream file(m_cachePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    CacheHeader header;
    header.version = 1;
    header.entryCount = (uint32_t)m_entries.size();
    header.lastSyncTimestamp = (uint64_t)time(nullptr);

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(m_entries.data()),
               m_entries.size() * sizeof(CacheEntry));
    file.close();
    return true;
}

void DuplicateCache::StartBackgroundSync(const std::wstring& syncDbPath) {
    m_syncDbPath = syncDbPath;
    m_running = true;
    m_syncThread = std::thread(&DuplicateCache::SyncLoop, this);
}

void DuplicateCache::StopBackgroundSync() {
    m_running = false;
    if (m_syncThread.joinable()) {
        m_syncThread.join();
    }
}

void DuplicateCache::SyncLoop() {
    while (m_running) {
        // Sleep 60 seconds between syncs
        for (int i = 0; i < 600 && m_running; i++) {
            Sleep(100);
        }
        if (m_running) {
            SyncFromDatabase();
        }
    }
}

void DuplicateCache::SyncFromDatabase() {
    // NOTE: This will use SQLite to query Float's sync.db for submitted invoices.
    // For the spike, the binary cache file is the source of truth.
    //
    // Full implementation:
    //   sqlite3* db;
    //   sqlite3_open(syncDbPath, &db);
    //   sqlite3_exec(db, "SELECT filename, submit_time, firs_ref, user FROM submitted_invoices", ...);
    //   // Merge results into m_entries / m_filenameIndex
    //   sqlite3_close(db);
    //
    // SumatraPDF does NOT bundle SQLite, so we'd add it as a dependency (~500KB).
    // Alternative: Float writes the binary cache file directly, Transforma just reads it.
}

} // namespace Helium
