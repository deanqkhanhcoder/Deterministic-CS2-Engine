#include "movement_reconstruction.h"
#include "timing.h"
#include "telemetry.h"

namespace movement {
void InitLUT() {}
}

namespace timing {
int64_t NowUs() { return 0; }
}

namespace telemetry {
ForensicRingBuffer g_forensicBuffer;
void RequestForensicFlush() {}
}
