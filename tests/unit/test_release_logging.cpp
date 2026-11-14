#include "debug_logger.h"
#include <cassert>

int main() {
    int evaluated = 0;
    DLOG_TRACE(Runtime, "%d", ++evaluated);
    DLOG_INFO(Runtime, "%d", ++evaluated);
    DLOG_WARN(Runtime, "%d", ++evaluated);
    DLOG_ERR(Runtime, "%d", ++evaluated);
    DLOG_FATAL(Runtime, "%d", ++evaluated);
    assert(evaluated == 0);
}
