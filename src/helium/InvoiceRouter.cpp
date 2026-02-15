// InvoiceRouter.cpp — Two-tier intelligent PDF routing

#include "InvoiceRouter.h"
#include <algorithm>
#include <fstream>
#include <cctype>

namespace Helium {

InvoiceRouter::InvoiceRouter() {
    InitDefaultPatterns();
}

void InvoiceRouter::InitDefaultPatterns() {
    m_patterns.clear();

    // GTBank invoice patterns
    m_patterns.push_back({
        "GTBank",
        std::regex(R"(GT[_\-\s]?(Bank|B).*inv)", std::regex_constants::icase),
        "GTBank invoice filenames"
    });

    // MTN invoice patterns
    m_patterns.push_back({
        "MTN",
        std::regex(R"(MTN.*(?:invoice|bill|statement))", std::regex_constants::icase),
        "MTN billing documents"
    });

    // Airtel invoice patterns
    m_patterns.push_back({
        "Airtel",
        std::regex(R"(Airtel.*(?:invoice|bill|statement))", std::regex_constants::icase),
        "Airtel billing documents"
    });

    // ExecuJet patterns (e.g., WN42752.pdf)
    m_patterns.push_back({
        "ExecuJet",
        std::regex(R"(WN\d{4,6}\.pdf)", std::regex_constants::icase),
        "ExecuJet work order / invoice"
    });

    // Generic invoice filename patterns
    m_patterns.push_back({
        "Generic",
        std::regex(R"((?:INV|INVOICE|BILL|RECEIPT|TAX[_\-\s]?INV)[\-_\s]?\d)", std::regex_constants::icase),
        "Generic invoice filenames"
    });

    // FIRS-related documents
    m_patterns.push_back({
        "FIRS",
        std::regex(R"((?:FIRS|TIN|VAT)[\-_\s])", std::regex_constants::icase),
        "FIRS / tax-related documents"
    });
}

RouteResult InvoiceRouter::Route(const std::wstring& pdfPath) {
    // Extract filename from path
    std::string filename = WideToUtf8(pdfPath);
    size_t lastSlash = filename.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }

    // Tier 1: Filename regex (instant — 0.0001s)
    RouteResult result = MatchFilename(filename);
    if (result.decision == RouteDecision::Invoice) {
        return result;
    }

    // Tier 2: Content analysis (0.1s max)
    result = AnalyzeContent(pdfPath);
    return result;
}

RouteResult InvoiceRouter::MatchFilename(const std::string& filename) {
    RouteResult result;
    result.decision = RouteDecision::Unknown;
    result.confidenceScore = 0.0;

    for (const auto& pattern : m_patterns) {
        if (std::regex_search(filename, pattern.filenameRegex)) {
            result.decision = RouteDecision::Invoice;
            result.matchedPattern = pattern.description;
            result.clientHint = pattern.name;
            result.confidenceScore = 0.95;
            return result;
        }
    }

    return result;
}

RouteResult InvoiceRouter::AnalyzeContent(const std::wstring& pdfPath) {
    RouteResult result;
    result.decision = RouteDecision::Unknown;
    result.confidenceScore = 0.0;

    std::string text = ExtractFirstPageText(pdfPath, 500);
    if (text.empty()) {
        // Can't read content — treat as unknown, open in our viewer
        result.decision = RouteDecision::Unknown;
        return result;
    }

    // Convert to uppercase for matching
    std::string upper = text;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    // Invoice markers — weighted scoring
    struct Marker {
        const char* text;
        double weight;
    };

    static const Marker markers[] = {
        {"TAX INVOICE",    0.40},
        {"INVOICE",        0.25},
        {"BILL TO",        0.20},
        {"SHIP TO",        0.15},
        {"TIN:",           0.30},
        {"VAT:",           0.20},
        {"TOTAL AMOUNT",   0.15},
        {"SUBTOTAL",       0.15},
        {"DUE DATE",       0.15},
        {"INVOICE NO",     0.30},
        {"INVOICE NUMBER", 0.30},
        {"INV NO",         0.25},
        {"PURCHASE ORDER", 0.20},
        {"ACCOUNT NO",     0.10},
        {"FIRS",           0.25},
    };

    double score = 0.0;
    std::string bestMatch;

    for (const auto& m : markers) {
        if (upper.find(m.text) != std::string::npos) {
            score += m.weight;
            if (bestMatch.empty()) {
                bestMatch = m.text;
            }
        }
    }

    // Clamp to 1.0
    if (score > 1.0) score = 1.0;

    if (score >= 0.30) {
        result.decision = RouteDecision::Invoice;
        result.matchedPattern = "Content analysis: " + bestMatch;
        result.confidenceScore = score;
    } else {
        result.decision = RouteDecision::NotInvoice;
        result.confidenceScore = 1.0 - score;
    }

    return result;
}

std::string InvoiceRouter::ExtractFirstPageText(const std::wstring& pdfPath, int maxChars) {
    // NOTE: In the actual SumatraPDF fork, this will use MuPDF's fz_new_stext_page()
    // to extract text from page 1. MuPDF is already compiled into SumatraPDF.
    //
    // For now, this is a placeholder that reads raw bytes looking for text streams.
    // The real implementation will be wired during the Sumatra fork integration.
    //
    // MuPDF integration:
    //   fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    //   fz_document* doc = fz_open_document(ctx, path);
    //   fz_page* page = fz_load_page(ctx, doc, 0);
    //   fz_stext_page* stext = fz_new_stext_page_from_page(ctx, page, NULL);
    //   // Walk stext blocks → lines → chars to build string

    // Placeholder: look for ASCII text in the PDF binary
    std::ifstream file(pdfPath, std::ios::binary);
    if (!file.is_open()) return "";

    // Read first 8KB — enough to find text in most invoices
    std::vector<char> buffer(8192);
    file.read(buffer.data(), buffer.size());
    auto bytesRead = file.gcount();
    file.close();

    // Extract printable ASCII runs (crude but functional for spike)
    std::string text;
    std::string run;
    for (int i = 0; i < bytesRead && (int)text.size() < maxChars; i++) {
        char c = buffer[i];
        if (c >= 32 && c < 127) {
            run += c;
        } else {
            if (run.size() >= 4) {
                text += run + " ";
            }
            run.clear();
        }
    }
    if (run.size() >= 4) {
        text += run;
    }

    if ((int)text.size() > maxChars) {
        text.resize(maxChars);
    }

    return text;
}

bool InvoiceRouter::LoadPatterns(const std::wstring& configPath) {
    // Load routing-patterns.json for custom client patterns
    // Format: [{"name": "ClientX", "pattern": "CLX.*inv", "description": "ClientX invoices"}]
    // TODO: Implement JSON parsing when we add custom patterns
    return true;
}

std::wstring InvoiceRouter::GetFallbackHandler() {
    // Query registry for the user's original PDF handler
    // HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\UserChoice
    HKEY hKey;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.pdf\\UserChoice",
        0, KEY_READ, &hKey
    );

    if (result != ERROR_SUCCESS) {
        return L""; // No override set — use system default
    }

    wchar_t progId[256];
    DWORD size = sizeof(progId);
    result = RegQueryValueExW(hKey, L"ProgId", nullptr, nullptr, (BYTE*)progId, &size);
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS) {
        return L"";
    }

    return progId;
}

bool InvoiceRouter::OpenWithFallback(const std::wstring& pdfPath) {
    // Try to open with user's previous default handler
    // If that fails, use ShellExecute with "open" verb (system default)
    HINSTANCE result = ShellExecuteW(
        nullptr, L"open", pdfPath.c_str(),
        nullptr, nullptr, SW_SHOWNORMAL
    );

    return (intptr_t)result > 32; // ShellExecute returns > 32 on success
}

std::string InvoiceRouter::WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &result[0], size, nullptr, nullptr);
    return result;
}

} // namespace Helium
