// SumatraIntegration.cpp — Wires Helium components into SumatraPDF's UI
//
// This file provides the hook points that connect to SumatraPDF's existing
// toolbar and event system. It adds:
//   1. "Submit to FIRS" toolbar button (right side of toolbar)
//   2. OnDocumentLoaded hook (triggers routing + button state refresh)
//   3. Status bar text for submission feedback
//
// HOW TO INTEGRATE:
//   In SumatraPDF's Toolbar.cpp (CreateToolbar function), add after toolbar creation:
//     Helium::SumatraIntegration::AddSubmitButton(hwndToolbar);
//
//   In SumatraPDF's SumatraStartup.cpp (WinMain), add early init:
//     Helium::SumatraIntegration::Initialize();
//
//   In SumatraPDF's Canvas.cpp (OnDocumentLoaded), add:
//     Helium::SumatraIntegration::OnDocumentLoaded(filePath);

#include "helium/HeliumController.h"
#include <windows.h>
#include <commctrl.h>
#include <string>

#pragma comment(lib, "comctl32.lib")

namespace Helium {

// Command ID for our custom toolbar button (must not clash with Sumatra's IDs)
// SumatraPDF uses IDs in the range 300-500; we use 9000+
static const int IDC_SUBMIT_FIRS = 9001;

// Global controller instance
static HeliumController* g_controller = nullptr;
static HWND g_hwndToolbar = nullptr;
static HWND g_hwndSubmitButton = nullptr;  // Custom child window for our button
static int g_submitButtonIndex = -1;

// Colors matching the architecture doc
static const COLORREF COLOR_BLUE    = RGB(0, 120, 215);
static const COLORREF COLOR_GREEN   = RGB(16, 124, 16);
static const COLORREF COLOR_GREY    = RGB(150, 150, 150);
static const COLORREF COLOR_ORANGE  = RGB(255, 140, 0);
static const COLORREF COLOR_RED     = RGB(220, 50, 50);
static const COLORREF COLOR_WHITE   = RGB(255, 255, 255);

static COLORREF GetButtonColor(SubmitButtonState state) {
    switch (state) {
        case SubmitButtonState::Ready:           return COLOR_BLUE;
        case SubmitButtonState::AlreadySubmitted: return COLOR_GREY;
        case SubmitButtonState::Checking:        return COLOR_BLUE;
        case SubmitButtonState::Submitting:      return COLOR_BLUE;
        case SubmitButtonState::Success:         return COLOR_GREEN;
        case SubmitButtonState::Error:           return COLOR_RED;
        case SubmitButtonState::NoSession:       return COLOR_ORANGE;
        case SubmitButtonState::FloatNotRunning:  return COLOR_ORANGE;
        default:                                 return COLOR_GREY;
    }
}

static bool IsButtonClickable(SubmitButtonState state) {
    return state == SubmitButtonState::Ready ||
           state == SubmitButtonState::NoSession ||
           state == SubmitButtonState::FloatNotRunning;
}

// Custom button window procedure
static LRESULT CALLBACK SubmitButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);

            auto state = g_controller ? g_controller->GetButtonState() : ButtonStateInfo{};
            COLORREF bgColor = GetButtonColor(state.state);
            bool clickable = IsButtonClickable(state.state);

            // Fill background
            HBRUSH brush = CreateSolidBrush(bgColor);
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);

            // Draw rounded border
            HPEN pen = CreatePen(PS_SOLID, 1, bgColor);
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);

            // Draw text
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, COLOR_WHITE);

            HFONT font = CreateFontW(14, 0, 0, 0,
                clickable ? FW_SEMIBOLD : FW_NORMAL,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            HFONT oldFont = (HFONT)SelectObject(hdc, font);

            std::wstring label(state.label.begin(), state.label.end());
            DrawTextW(hdc, label.c_str(), -1, &rc,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, oldFont);
            DeleteObject(font);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONUP: {
            if (!g_controller) break;
            auto state = g_controller->GetButtonState();

            if (state.state == SubmitButtonState::Ready) {
                // Get current document path from SumatraPDF
                // NOTE: In the actual fork, this reads from SumatraPDF's WindowInfo
                // For now, we use the path stored in the controller
                // g_controller->OnSubmitClicked(GetCurrentDocPath());
            } else if (state.state == SubmitButtonState::NoSession) {
                // Launch Float for login
                ShellExecuteW(nullptr, L"open", L"float.exe", nullptr, nullptr, SW_SHOWNORMAL);
            } else if (state.state == SubmitButtonState::FloatNotRunning) {
                ShellExecuteW(nullptr, L"open", L"float.exe", nullptr, nullptr, SW_SHOWNORMAL);
            }
            return 0;
        }

        case WM_SETCURSOR: {
            if (!g_controller) break;
            auto state = g_controller->GetButtonState();
            if (IsButtonClickable(state.state)) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

class SumatraIntegration {
public:
    // Call once at startup (WinMain)
    static void Initialize() {
        g_controller = new HeliumController();
        g_controller->Initialize();

        // Register button state change callback
        g_controller->SetButtonStateCallback([](const ButtonStateInfo& info) {
            // Repaint the button when state changes
            if (g_hwndSubmitButton) {
                InvalidateRect(g_hwndSubmitButton, nullptr, TRUE);
            }
        });

        // Register the custom button window class
        WNDCLASSW wc = {};
        wc.lpfnWndProc = SubmitButtonProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"HeliumSubmitButton";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
    }

    // Call after SumatraPDF creates its toolbar
    static void AddSubmitButton(HWND hwndParent) {
        g_hwndToolbar = hwndParent;

        // Get toolbar dimensions to position our button on the right
        RECT tbRect;
        GetClientRect(hwndParent, &tbRect);

        int btnWidth = 140;
        int btnHeight = 28;
        int btnX = tbRect.right - btnWidth - 10;  // 10px from right edge
        int btnY = (tbRect.bottom - btnHeight) / 2;

        g_hwndSubmitButton = CreateWindowExW(
            0, L"HeliumSubmitButton", L"Submit to FIRS",
            WS_CHILD | WS_VISIBLE,
            btnX, btnY, btnWidth, btnHeight,
            hwndParent, (HMENU)(INT_PTR)IDC_SUBMIT_FIRS,
            GetModuleHandle(nullptr), nullptr
        );
    }

    // Call when a document is loaded/tab is switched
    static void OnDocumentLoaded(const std::wstring& filePath) {
        if (!g_controller) return;
        g_controller->OnPdfOpened(filePath);

        // Repaint button
        if (g_hwndSubmitButton) {
            InvalidateRect(g_hwndSubmitButton, nullptr, TRUE);
        }
    }

    // Call on application exit
    static void Shutdown() {
        if (g_controller) {
            delete g_controller;
            g_controller = nullptr;
        }
    }
};

} // namespace Helium
