// RelayClient.h — HTTP client for submitting invoices to Helium Relay
// Part of Transforma Reader (Helium Suite)
// License: GPL v3 (SumatraPDF fork)

#pragma once

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <functional>

#pragma comment(lib, "winhttp.lib")

namespace Helium {

struct RelayResponse {
    int statusCode = 0;
    std::string body;
    std::string error;
    bool success = false;
};

struct SubmitResult {
    bool success = false;
    std::string firsReference;   // e.g. "FIRS-2024-00847392"
    std::string fileUuid;
    std::string error;
    int httpStatus = 0;
};

class RelayClient {
public:
    RelayClient();
    ~RelayClient();

    // Configure relay endpoint (default: localhost:8082)
    void SetEndpoint(const std::wstring& host, int port);

    // Submit a PDF invoice to Relay for FIRS processing
    // Calls: POST /api/ingest
    // Content-Type: multipart/form-data
    // Fields: file (PDF binary), source ("transforma_reader"), user (email)
    SubmitResult SubmitInvoice(
        const std::wstring& pdfPath,
        const std::string& userEmail,
        const std::string& sessionToken
    );

    // Check if Relay is reachable (GET /health)
    bool IsRelayAvailable();

private:
    std::wstring m_host = L"localhost";
    int m_port = 8082;
    HINTERNET m_hSession = nullptr;

    RelayResponse SendRequest(
        const std::wstring& method,
        const std::wstring& path,
        const std::string& body,
        const std::string& contentType,
        const std::string& authToken
    );

    std::string BuildMultipartBody(
        const std::wstring& pdfPath,
        const std::string& userEmail,
        std::string& boundary
    );

    static std::string WideToUtf8(const std::wstring& wide);
    static std::wstring Utf8ToWide(const std::string& utf8);
};

} // namespace Helium
