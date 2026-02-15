// InvoiceRouter.h — Two-tier intelligent PDF routing
// Tier 1: Filename regex (0.0001s)
// Tier 2: Content analysis fallback (0.1s)

#pragma once

#include <string>
#include <vector>
#include <regex>

namespace Helium {

enum class RouteDecision {
    Invoice,        // Route to Transforma (open in our viewer)
    NotInvoice,     // Route to fallback PDF handler
    Unknown         // Could not determine — open in our viewer anyway
};

struct RouteResult {
    RouteDecision decision;
    std::string matchedPattern;  // Which pattern matched (for diagnostics)
    std::string clientHint;      // Detected client (GTBank, MTN, etc.)
    double confidenceScore;      // 0.0 - 1.0
};

struct RoutingPattern {
    std::string name;            // "GTBank", "MTN", "ExecuJet", "Generic"
    std::regex filenameRegex;
    std::string description;
};

class InvoiceRouter {
public:
    InvoiceRouter();

    // Main routing decision — called when user opens a PDF
    RouteResult Route(const std::wstring& pdfPath);

    // Load custom patterns from JSON config file
    bool LoadPatterns(const std::wstring& configPath);

    // Get the fallback PDF handler (user's original default)
    std::wstring GetFallbackHandler();

    // Open a PDF with the fallback handler
    bool OpenWithFallback(const std::wstring& pdfPath);

private:
    std::vector<RoutingPattern> m_patterns;

    // Tier 1: Filename-based routing (instant)
    RouteResult MatchFilename(const std::string& filename);

    // Tier 2: Content analysis (first 200 chars of page 1)
    RouteResult AnalyzeContent(const std::wstring& pdfPath);

    // Extract text from first page (using MuPDF — already linked via SumatraPDF)
    std::string ExtractFirstPageText(const std::wstring& pdfPath, int maxChars = 200);

    void InitDefaultPatterns();
    static std::string WideToUtf8(const std::wstring& wide);
};

} // namespace Helium
