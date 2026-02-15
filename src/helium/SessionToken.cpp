// SessionToken.cpp — DPAPI-encrypted session token management

#include "SessionToken.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <ctime>
#include <shlobj.h>

namespace Helium {

SessionInfo SessionToken::Load() {
    SessionInfo info;

    std::wstring path = GetTokenPath();
    if (path.empty()) {
        info.error = "Cannot determine token path";
        return info;
    }

    // Read encrypted file
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        info.error = "No session found (not logged in)";
        return info;
    }

    std::string encrypted((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    file.close();

    if (encrypted.empty()) {
        info.error = "Empty session file";
        return info;
    }

    // Decrypt with DPAPI
    std::string json = Decrypt(encrypted);
    if (json.empty()) {
        info.error = "Failed to decrypt session (wrong user or corrupted)";
        return info;
    }

    // Parse JSON
    info.username = ExtractJsonString(json, "username");
    info.token = ExtractJsonString(json, "token");
    info.expiresAt = ExtractJsonString(json, "expires_at");
    info.userId = ExtractJsonString(json, "user_id");

    if (info.token.empty()) {
        info.error = "Invalid session data (no token)";
        return info;
    }

    if (IsExpired(info.expiresAt)) {
        info.error = "Session expired";
        return info;
    }

    info.valid = true;
    return info;
}

bool SessionToken::Save(const SessionInfo& session) {
    std::wstring path = GetTokenPath();
    if (path.empty()) return false;

    // Ensure directory exists
    std::wstring dir = path.substr(0, path.find_last_of(L"\\/"));
    CreateDirectoryW(dir.c_str(), nullptr);

    // Build JSON and encrypt
    std::string json = BuildJson(session);
    std::string encrypted = Encrypt(json);
    if (encrypted.empty()) return false;

    // Write encrypted file
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    file.write(encrypted.data(), encrypted.size());
    file.close();

    // Set file ACL to owner-only (defense in depth alongside DPAPI)
    // DPAPI already prevents other-user decryption, but ACLs stop casual reads
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesW(path.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
    }

    return true;
}

bool SessionToken::HasValidSession() {
    SessionInfo info = Load();
    return info.valid;
}

bool SessionToken::ClearSession() {
    std::wstring path = GetTokenPath();
    if (path.empty()) return false;
    return DeleteFileW(path.c_str()) != 0;
}

std::wstring SessionToken::GetTokenPath() {
    wchar_t programData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, programData))) {
        return L"";
    }

    std::string username = GetWindowsUsername();
    if (username.empty()) return L"";

    std::wstring path = programData;
    path += L"\\Helium\\sessions\\";

    // Convert username to wide
    int size = MultiByteToWideChar(CP_UTF8, 0, username.c_str(), (int)username.size(), nullptr, 0);
    std::wstring wideUser(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, username.c_str(), (int)username.size(), &wideUser[0], size);

    path += wideUser;
    path += L".token.enc";
    return path;
}

std::string SessionToken::GetWindowsUsername() {
    char buffer[256];
    DWORD size = sizeof(buffer);
    if (GetUserNameA(buffer, &size)) {
        return std::string(buffer);
    }
    return "";
}

std::string SessionToken::Encrypt(const std::string& plaintext) {
    DATA_BLOB input;
    input.pbData = (BYTE*)plaintext.data();
    input.cbData = (DWORD)plaintext.size();

    DATA_BLOB output;

    // DPAPI encrypts with current user's credentials
    // Only the same Windows user on the same machine can decrypt
    if (!CryptProtectData(&input, L"HeliumSession", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return "";
    }

    std::string result((char*)output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}

std::string SessionToken::Decrypt(const std::string& ciphertext) {
    DATA_BLOB input;
    input.pbData = (BYTE*)ciphertext.data();
    input.cbData = (DWORD)ciphertext.size();

    DATA_BLOB output;

    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return "";
    }

    std::string result((char*)output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}

std::string SessionToken::ExtractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    // Skip past key and colon
    pos = json.find(":", pos + search.length());
    if (pos == std::string::npos) return "";

    // Find opening quote of value
    pos = json.find("\"", pos + 1);
    if (pos == std::string::npos) return "";

    // Find closing quote
    size_t end = json.find("\"", pos + 1);
    if (end == std::string::npos) return "";

    return json.substr(pos + 1, end - pos - 1);
}

std::string SessionToken::BuildJson(const SessionInfo& session) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"username\": \"" << session.username << "\",\n";
    json << "  \"token\": \"" << session.token << "\",\n";
    json << "  \"expires_at\": \"" << session.expiresAt << "\",\n";
    json << "  \"user_id\": \"" << session.userId << "\"\n";
    json << "}";
    return json.str();
}

bool SessionToken::IsExpired(const std::string& expiresAt) {
    if (expiresAt.empty()) return true;

    // Parse ISO 8601: "2026-02-19T15:30:00Z"
    struct tm tm = {};
    if (sscanf_s(expiresAt.c_str(), "%d-%d-%dT%d:%d:%d",
        &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
        &tm.tm_hour, &tm.tm_min, &tm.tm_sec) < 6) {
        return true; // Can't parse = treat as expired
    }

    tm.tm_year -= 1900;
    tm.tm_mon -= 1;

    time_t expiry = _mkgmtime(&tm);
    time_t now;
    time(&now);

    return difftime(expiry, now) <= 0;
}

} // namespace Helium
