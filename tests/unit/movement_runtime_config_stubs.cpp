#include "runtime_config.h"

namespace rcfg {
namespace {
RuntimeConfig g_config{};
}

RuntimeConfig Get() { return g_config; }
RuntimeConfig Sanitize(const RuntimeConfig& config) { return config; }
void Apply(const RuntimeConfig& config) { g_config = config; }
RuntimeConfig GetMutable() { return g_config; }
void Init() { g_config = RuntimeConfig{}; }
} // namespace rcfg
