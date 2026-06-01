#pragma once

// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — Build Configuration Definitions         ║
// ║  SINGLE SOURCE OF TRUTH FOR MULTI-CONFIG BUILD PIPELINE             ║
// ╚══════════════════════════════════════════════════════════════════════╝

// ── 1. DEBUG ──
#if !defined(MARCO_PROFILE) && !defined(MARCO_RELEASE)
    #define MARCO_ENABLE_ETW 1
    #define MARCO_ENABLE_FORENSIC 1
    #define MARCO_ENABLE_UI 1
    #define MARCO_ENABLE_FORENSIC_UI 1
    #define MARCO_ENABLE_RELEASE_DASHBOARD 0

// ── 2. PROFILE ──
#elif defined(MARCO_PROFILE)
    #define MARCO_ENABLE_ETW 1
    #define MARCO_ENABLE_FORENSIC 1
    #define MARCO_ENABLE_UI 1
    #define MARCO_ENABLE_FORENSIC_UI 0
    #define MARCO_ENABLE_RELEASE_DASHBOARD 0

// ── 3. RELEASE ──
#elif defined(MARCO_RELEASE)
    #define MARCO_ENABLE_ETW 0
    #define MARCO_ENABLE_FORENSIC 0
    #define MARCO_ENABLE_UI 1
    #define MARCO_ENABLE_FORENSIC_UI 0
    #define MARCO_ENABLE_RELEASE_DASHBOARD 1

#else
    // Default to RELEASE if nothing specified, to prevent accidental debug builds in production
    #define MARCO_ENABLE_ETW 0
    #define MARCO_ENABLE_FORENSIC 0
    #define MARCO_ENABLE_UI 1
    #define MARCO_ENABLE_FORENSIC_UI 0
    #define MARCO_ENABLE_RELEASE_DASHBOARD 1
#endif
