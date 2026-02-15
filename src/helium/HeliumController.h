// HeliumController.h — Main integration controller for Transforma Reader
// Coordinates: routing, session, relay submission, duplicate detection
// This is the single entry point that SumatraPDF's UI code calls.

#pragma once

#include "RelayClient.h"
#include "SessionToken.h"
#include "InvoiceRouter.h"
#include "DuplicateCache.h"
#include <string>
#include <functional>

namespace Helium {

enum class SubmitButtonState {
    Ready,              // Blue — "Submit to FIRS"
    AlreadySubmitted,   // Grey — "Already Submitted"
    Checking,           // Blue spinner — "Checking..."
    Submitting,         // Blue spinner — "Submitting..."
    Success,            // Green — "Submitted!" (reverts after 3s)
    Error,              // Red — error message
    NoSession,          // Orange — "Sign In Required"
    FloatNotRunning     // Orange — "Start Float"
};

struct ButtonStateInfo {
    SubmitButtonState state;
    std::string label;
    std::string tooltip;
};

// Callback for UI updates (SumatraPDF toolbar repaints)
using ButtonStateCallback = std::function<void(const ButtonStateInfo&)>;

class HeliumController {
public:
    HeliumController();
    ~HeliumController();

    // Initialize all subsystems
    bool Initialize();

    // Called when user opens a PDF — decides whether to show in Transforma or fallback
    RouteResult OnPdfOpened(const std::wstring& pdfPath);

    // Called when user clicks "Submit to FIRS" button
    void OnSubmitClicked(const std::wstring& currentPdfPath);

    // Get current button state (for UI rendering)
    ButtonStateInfo GetButtonState();

    // Register callback for button state changes
    void SetButtonStateCallback(ButtonStateCallback callback);

    // Check if Relay is available (called periodically)
    bool CheckRelayConnection();

private:
    RelayClient m_relay;
    DuplicateCache m_cache;
    InvoiceRouter m_router;

    ButtonStateInfo m_buttonState;
    ButtonStateCallback m_onStateChange;
    std::wstring m_currentPdf;

    void SetState(SubmitButtonState state, const std::string& label,
                  const std::string& tooltip);

    // Update button state based on current PDF
    void RefreshButtonState(const std::wstring& pdfPath);

    static std::wstring GetCachePath();
    static std::string ExtractFilename(const std::wstring& path);
};

} // namespace Helium
