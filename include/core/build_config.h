#pragma once

// Build configuration (single source of truth).
// Two configurations only: Release (MARCO_RELEASE) and Debug (default).

#if defined(MARCO_RELEASE)
    #define MARCO_ENABLE_ETW 0
    #define MARCO_ENABLE_FORENSIC 0
#else
    #define MARCO_ENABLE_ETW 1
    #define MARCO_ENABLE_FORENSIC 1
#endif
