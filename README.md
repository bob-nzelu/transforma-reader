# Transforma Reader

Lightning-fast PDF viewer for Nigerian enterprise invoice workflows. Part of the Helium Suite.

**0.25s startup | 5.5MB executable | Submit to FIRS in one click**

## What It Does

1. **Opens invoice PDFs instantly** — SumatraPDF-based, native C++ (no Python, no .NET)
2. **Routes intelligently** — Invoices open in Transforma; everything else opens in your default PDF reader
3. **Submits to FIRS** — One-click submission through Helium Relay with duplicate detection
4. **Shared authentication** — Log in once via Float, Transforma picks up the session (DPAPI-encrypted)

## Architecture

```
PDF double-click
    │
    ├─ Filename regex match? ──→ Open in Transforma
    │   (GTBank, MTN, ExecuJet patterns — 0.0001s)
    │
    ├─ Content has invoice markers? ──→ Open in Transforma
    │   ("INVOICE", "TIN:", "VAT:" — 0.1s)
    │
    └─ Neither? ──→ Open with your default PDF reader
                     (Adobe, Chrome, Edge)

Transforma
    │
    └─ [Submit to FIRS] button
          │
          ├─ Check DPAPI session token
          ├─ Check duplicate cache
          └─ POST to Relay (localhost:8082/api/ingest)
                │
                └─ Relay handles: validation, malware scan,
                   HMAC, dedup, blob write, audit, Core notify
```

## Project Structure

```
Transforma/Lite/
├── .github/workflows/build.yml    ← GitHub Actions CI (compiles on push)
├── src/
│   ├── helium/
│   │   ├── RelayClient.h/.cpp      ← WinHTTP client → Relay API
│   │   ├── SessionToken.h/.cpp     ← DPAPI-encrypted session management
│   │   ├── InvoiceRouter.h/.cpp    ← Two-tier routing (regex + content)
│   │   ├── DuplicateCache.h/.cpp   ← Binary cache for dedup
│   │   └── HeliumController.h/.cpp ← Main controller (ties everything together)
│   └── patches/
│       └── SumatraIntegration.cpp  ← Toolbar button + Sumatra hooks
├── Documentation/
│   └── transforma-reader-architecture.md
└── README.md
```

## How to Build

### Option A: GitHub Actions (no local tools needed)

1. Push this repo to GitHub
2. GitHub Actions automatically compiles on every push
3. Download `TransformaReader.exe` from the Actions artifacts tab

### Option B: Local build (requires Visual Studio 2022)

```powershell
# 1. Clone SumatraPDF into this directory
git clone --depth 1 --branch 3.5.2 https://github.com/nicbenji/sumatrapdf.git sumatrapdf

# 2. Copy Helium source files into the Sumatra tree
Copy-Item -Recurse -Force src/helium sumatrapdf/src/helium
Copy-Item src/patches/SumatraIntegration.cpp sumatrapdf/src/SumatraIntegration.cpp

# 3. Open sumatrapdf/vs2022/SumatraPDF.sln in Visual Studio
# 4. Add the Helium .cpp files to the SumatraPDF project
# 5. Build Release x64
```

## Key Design Decisions

| Decision | Choice | Reason |
|----------|--------|--------|
| Submit path | Through Relay | Consistent with HELIUM_OVERVIEW. Gets validation, malware scan, HMAC, dedup, audit for free. |
| Token security | DPAPI mandatory | Zero-cost (<1ms). Closes enterprise procurement security questions. |
| PDF engine | SumatraPDF fork (MuPDF) | 0.25s startup. Same engine as PyMuPDF but without Python overhead. |
| HTTP client | WinHTTP | Built into Windows. No external dependencies. |
| Duplicate cache | Binary file + background sync | 0.0016s check overhead. Syncs from Float every 60s. |

## Dependencies

- **SumatraPDF v3.5+** — GPL v3 (free, must open-source this fork)
- **WinHTTP** — Built into Windows (no install)
- **DPAPI (CryptProtectData)** — Built into Windows (no install)
- **No other dependencies**

## License

GPL v3 (required by SumatraPDF fork). The Helium integration code is also GPL v3.
Source code must be made available per GPL v3 requirements.
