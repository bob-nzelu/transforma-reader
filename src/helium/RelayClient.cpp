// RelayClient.cpp — HTTP client for submitting invoices to Helium Relay
// Uses WinHTTP (no external dependencies)

#include "RelayClient.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <random>

namespace Helium {

RelayClient::RelayClient() {
    m_hSession = WinHttpOpen(
        L"TransformaReader/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
}

RelayClient::~RelayClient() {
    if (m_hSession) {
        WinHttpCloseHandle(m_hSession);
    }
}

void RelayClient::SetEndpoint(const std::wstring& host, int port) {
    m_host = host;
    m_port = port;
}

SubmitResult RelayClient::SubmitInvoice(
    const std::wstring& pdfPath,
    const std::string& userEmail,
    const std::string& sessionToken
) {
    SubmitResult result;

    std::string boundary;
    std::string body = BuildMultipartBody(pdfPath, userEmail, boundary);
    if (body.empty()) {
        result.error = "Failed to read PDF file";
        return result;
    }

    std::string contentType = "multipart/form-data; boundary=" + boundary;
    RelayResponse resp = SendRequest(L"POST", L"/api/ingest", body, contentType, sessionToken);

    result.httpStatus = resp.statusCode;

    if (!resp.success) {
        result.error = resp.error;
        return result;
    }

    if (resp.statusCode == 200 || resp.statusCode == 201) {
        result.success = true;
        // Parse JSON response for file_uuid and firs_reference
        // Minimal JSON parsing — look for known keys
        auto extractField = [&](const std::string& key) -> std::string {
            std::string search = "\"" + key + "\"";
            size_t pos = resp.body.find(search);
            if (pos == std::string::npos) return "";
            pos = resp.body.find("\"", pos + search.length() + 1);
            if (pos == std::string::npos) return "";
            size_t end = resp.body.find("\"", pos + 1);
            if (end == std::string::npos) return "";
            return resp.body.substr(pos + 1, end - pos - 1);
        };
        result.fileUuid = extractField("file_uuid");
        result.firsReference = extractField("firs_reference");
    } else if (resp.statusCode == 409) {
        result.error = "Invoice already submitted (duplicate)";
    } else if (resp.statusCode == 429) {
        result.error = "Daily submission limit exceeded";
    } else {
        result.error = "Relay returned HTTP " + std::to_string(resp.statusCode);
    }

    return result;
}

bool RelayClient::IsRelayAvailable() {
    RelayResponse resp = SendRequest(L"GET", L"/health", "", "", "");
    return resp.success && resp.statusCode == 200;
}

RelayResponse RelayClient::SendRequest(
    const std::wstring& method,
    const std::wstring& path,
    const std::string& body,
    const std::string& contentType,
    const std::string& authToken
) {
    RelayResponse result;

    if (!m_hSession) {
        result.error = "WinHTTP session not initialized";
        return result;
    }

    HINTERNET hConnect = WinHttpConnect(m_hSession, m_host.c_str(), (INTERNET_PORT)m_port, 0);
    if (!hConnect) {
        result.error = "Failed to connect to Relay";
        return result;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, method.c_str(), path.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, 0
    );
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        result.error = "Failed to create HTTP request";
        return result;
    }

    // Set timeout: 30 seconds for connect, send, receive
    WinHttpSetTimeouts(hRequest, 5000, 30000, 30000, 30000);

    // Add headers
    if (!contentType.empty()) {
        std::wstring ctHeader = L"Content-Type: " + Utf8ToWide(contentType);
        WinHttpAddRequestHeaders(hRequest, ctHeader.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    }

    if (!authToken.empty()) {
        std::wstring authHeader = L"Authorization: Bearer " + Utf8ToWide(authToken);
        WinHttpAddRequestHeaders(hRequest, authHeader.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    }

    // Send request
    BOOL sent = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        (LPVOID)body.c_str(), (DWORD)body.size(),
        (DWORD)body.size(), 0
    );

    if (!sent) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        result.error = "Failed to send request (is Relay running?)";
        return result;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        result.error = "No response from Relay";
        return result;
    }

    // Get status code
    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
    result.statusCode = (int)statusCode;

    // Read response body
    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead);
        responseBody.append(buffer.data(), bytesRead);
    }
    result.body = responseBody;
    result.success = true;

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return result;
}

std::string RelayClient::BuildMultipartBody(
    const std::wstring& pdfPath,
    const std::string& userEmail,
    std::string& boundary
) {
    // Generate random boundary
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    const char* hex = "0123456789abcdef";
    boundary = "----HeliumBoundary";
    for (int i = 0; i < 16; i++) {
        boundary += hex[dis(gen)];
    }

    // Read PDF file
    std::ifstream file(pdfPath, std::ios::binary);
    if (!file.is_open()) return "";

    std::vector<char> pdfData((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    file.close();

    // Extract filename from path
    std::string filename = WideToUtf8(pdfPath);
    size_t lastSlash = filename.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }

    std::ostringstream body;

    // Field: source
    body << "--" << boundary << "\r\n";
    body << "Content-Disposition: form-data; name=\"source\"\r\n\r\n";
    body << "transforma_reader\r\n";

    // Field: user
    body << "--" << boundary << "\r\n";
    body << "Content-Disposition: form-data; name=\"user\"\r\n\r\n";
    body << userEmail << "\r\n";

    // Field: file (PDF binary)
    body << "--" << boundary << "\r\n";
    body << "Content-Disposition: form-data; name=\"file\"; filename=\"" << filename << "\"\r\n";
    body << "Content-Type: application/pdf\r\n\r\n";
    body.write(pdfData.data(), pdfData.size());
    body << "\r\n";

    // End boundary
    body << "--" << boundary << "--\r\n";

    return body.str();
}

std::string RelayClient::WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &result[0], size, nullptr, nullptr);
    return result;
}

std::wstring RelayClient::Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &result[0], size);
    return result;
}

} // namespace Helium
