// HeliumController.cpp — Main integration controller

#include "HeliumController.h"
#include <thread>
#include <shlobj.h>

namespace Helium {

HeliumController::HeliumController() {
    m_buttonState.state = SubmitButtonState::Checking;
    m_buttonState.label = "Checking...";
    m_buttonState.tooltip = "Verifying session and connection";
}

HeliumController::~HeliumController() {
    m_cache.StopBackgroundSync();
}

bool HeliumController::Initialize() {
    // Load duplicate cache
    std::wstring cachePath = GetCachePath();
    m_cache.Load(cachePath);

    // Check session
    if (!SessionToken::HasValidSession()) {
        SetState(SubmitButtonState::NoSession,
                 "Sign In Required",
                 "Open Float to sign in to your Helium account");
        return true; // Non-fatal — user just can't submit yet
    }

    // Check Relay connectivity
    if (!m_relay.IsRelayAvailable()) {
        SetState(SubmitButtonState::FloatNotRunning,
                 "Start Float",
                 "Helium Float must be running to submit invoices");
        return true; // Non-fatal
    }

    SetState(SubmitButtonState::Ready,
             "Submit to FIRS",
             "Send this invoice to FIRS for processing");

    return true;
}

RouteResult HeliumController::OnPdfOpened(const std::wstring& pdfPath) {
    m_currentPdf = pdfPath;
    RouteResult result = m_router.Route(pdfPath);

    if (result.decision == RouteDecision::Invoice ||
        result.decision == RouteDecision::Unknown) {
        // Show in Transforma — refresh button state for this file
        RefreshButtonState(pdfPath);
    }

    return result;
}

void HeliumController::OnSubmitClicked(const std::wstring& currentPdfPath) {
    // Run submission on background thread to keep UI responsive
    std::thread([this, currentPdfPath]() {
        // 1. Check session
        SessionInfo session = SessionToken::Load();
        if (!session.valid) {
            SetState(SubmitButtonState::NoSession,
                     "Sign In Required",
                     session.error);
            return;
        }

        // 2. Check duplicate
        std::string filename = ExtractFilename(currentPdfPath);
        auto dupCheck = m_cache.Check(filename);
        if (dupCheck.status == DuplicateStatus::AlreadySubmitted) {
            SetState(SubmitButtonState::AlreadySubmitted,
                     "Already Submitted",
                     "Submitted by " + dupCheck.submittedBy +
                     " (Ref: " + dupCheck.firsReference + ")");
            return;
        }

        // 3. Submit via Relay
        SetState(SubmitButtonState::Submitting,
                 "Submitting...",
                 "Sending to Helium Relay for FIRS processing");

        SubmitResult result = m_relay.SubmitInvoice(
            currentPdfPath, session.username, session.token
        );

        if (result.success) {
            // 4. Record in cache
            m_cache.AddEntry(filename, result.firsReference, session.username);

            SetState(SubmitButtonState::Success,
                     "Submitted!",
                     "FIRS Reference: " + result.firsReference);

            // Revert to "Already Submitted" after 3 seconds
            Sleep(3000);
            SetState(SubmitButtonState::AlreadySubmitted,
                     "Already Submitted",
                     "FIRS Reference: " + result.firsReference);
        } else {
            SetState(SubmitButtonState::Error,
                     "Submit Failed",
                     result.error);

            // Revert to Ready after 5 seconds
            Sleep(5000);
            SetState(SubmitButtonState::Ready,
                     "Submit to FIRS",
                     "Click to retry submission");
        }
    }).detach();
}

ButtonStateInfo HeliumController::GetButtonState() {
    return m_buttonState;
}

void HeliumController::SetButtonStateCallback(ButtonStateCallback callback) {
    m_onStateChange = callback;
}

bool HeliumController::CheckRelayConnection() {
    return m_relay.IsRelayAvailable();
}

void HeliumController::SetState(SubmitButtonState state, const std::string& label,
                                const std::string& tooltip) {
    m_buttonState.state = state;
    m_buttonState.label = label;
    m_buttonState.tooltip = tooltip;

    if (m_onStateChange) {
        m_onStateChange(m_buttonState);
    }
}

void HeliumController::RefreshButtonState(const std::wstring& pdfPath) {
    std::string filename = ExtractFilename(pdfPath);

    // Check duplicate cache first (instant)
    auto dupCheck = m_cache.Check(filename);
    if (dupCheck.status == DuplicateStatus::AlreadySubmitted) {
        SetState(SubmitButtonState::AlreadySubmitted,
                 "Already Submitted",
                 "Submitted by " + dupCheck.submittedBy +
                 " (Ref: " + dupCheck.firsReference + ")");
        return;
    }

    // Check session
    if (!SessionToken::HasValidSession()) {
        SetState(SubmitButtonState::NoSession,
                 "Sign In Required",
                 "Open Float to sign in");
        return;
    }

    SetState(SubmitButtonState::Ready,
             "Submit to FIRS",
             "Send this invoice to FIRS for processing");
}

std::wstring HeliumController::GetCachePath() {
    wchar_t programData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, programData))) {
        return L"submitted-invoices.cache"; // Fallback to current directory
    }
    return std::wstring(programData) + L"\\Helium\\cache\\submitted-invoices.cache";
}

std::string HeliumController::ExtractFilename(const std::wstring& path) {
    // Convert to UTF-8 and extract filename
    int size = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), (int)path.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), (int)path.size(), &utf8[0], size, nullptr, nullptr);

    size_t lastSlash = utf8.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return utf8.substr(lastSlash + 1);
    }
    return utf8;
}

} // namespace Helium
