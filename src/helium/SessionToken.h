// SessionToken.h — DPAPI-encrypted session token management
// Shared authentication between Float and Transforma Reader

#pragma once

#include <windows.h>
#include <wincrypt.h>
#include <string>

#pragma comment(lib, "crypt32.lib")

namespace Helium {

struct SessionInfo {
    std::string username;
    std::string token;
    std::string expiresAt;
    std::string userId;
    bool valid = false;
    std::string error;
};

class SessionToken {
public:
    // Load and decrypt session token for current Windows user
    // Reads from: C:\ProgramData\Helium\sessions\{username}.token.enc
    static SessionInfo Load();

    // Save and encrypt session token (called by Float after login)
    static bool Save(const SessionInfo& session);

    // Check if a valid (non-expired) session exists
    static bool HasValidSession();

    // Delete session (logout)
    static bool ClearSession();

private:
    static std::wstring GetTokenPath();
    static std::string GetWindowsUsername();

    // DPAPI encryption (mandatory — protects against same-user malware)
    static std::string Encrypt(const std::string& plaintext);
    static std::string Decrypt(const std::string& ciphertext);

    // Minimal JSON helpers (no external dependency)
    static std::string ExtractJsonString(const std::string& json, const std::string& key);
    static std::string BuildJson(const SessionInfo& session);
    static bool IsExpired(const std::string& expiresAt);
};

} // namespace Helium
