#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  CS2 Macro Suite — Config I/O (INI persistence)                     ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "runtime_config.h"

namespace config_io {

// Load config from INI file next to executable. Returns false if file not found.
bool Load(RuntimeConfig& cfg);

// Save config to INI file next to executable.
bool Save(const RuntimeConfig& cfg);

// Get path to config file
const wchar_t* GetConfigPath();

} // namespace config_io
