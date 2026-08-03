#include "debug_logger.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    dlog::Shutdown();
    const uint64_t droppedBeforeInit = dlog::DroppedCount();
    dlog::Init();

    const uint64_t acceptedBefore = dlog::AcceptedCount();
    dlog::Write(dlog::Subsystem::StressTest, dlog::Level::Info,
                __FILE__, __LINE__, "logger receipt %lld", 7LL);
    dlog::Write(dlog::Subsystem::StressTest, dlog::Level::Info,
                __FILE__, __LINE__,
                "typed logger movement=%s delay=%lld ratio=%.2f",
                "W", static_cast<long long>(42), 1.5);
    Expect(dlog::AcceptedCount() == acceptedBefore + 2,
           "accepted log entry receives a completion receipt");
    Expect(dlog::Flush(), "Flush reports completion of accepted work");
    Expect(dlog::CompletedCount() >= acceptedBefore + 2,
           "Flush waits for the worker to finish and flush every entry");
    Expect(dlog::PendingCount() == 0,
           "Flush leaves no queued or in-flight entries");

    std::ifstream logFile("marco_debug.log");
    const std::string logText((std::istreambuf_iterator<char>(logFile)),
                              std::istreambuf_iterator<char>());
    Expect(logText.find("typed logger movement=W delay=42 ratio=1.50") !=
               std::string::npos,
           "logger preserves pointer, integer, and floating argument types");

    dlog::Shutdown();
    const uint64_t dropsBeforeRejectedWrite = dlog::DroppedCount();
    dlog::Write(dlog::Subsystem::StressTest, dlog::Level::Info,
                __FILE__, __LINE__, "must be rejected after shutdown");
    Expect(dlog::DroppedCount() == dropsBeforeRejectedWrite + 1,
           "rejected writes are reflected by the drop counter");
    Expect(dlog::DroppedCount() >= droppedBeforeInit,
           "drop counter is monotonic");

    std::cout << "PASS: bounded logger lifecycle\n";
    return 0;
}
