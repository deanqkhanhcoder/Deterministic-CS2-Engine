#include "config_io.h"

#include <windows.h>

#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    const std::wstring configPath = config_io::GetConfigPath();
    const std::wstring tempPath = configPath + L".tmp";
    DeleteFileW(configPath.c_str());
    DeleteFileW(tempPath.c_str());

    RuntimeConfig first{};
    first.quickTapMs = 11;
    first.socdMode = SocdMode::HUMANIZED;
    first.activeBrakeProfileIndex = 1;
    RuntimeConfig second{};
    second.quickTapMs = 77;
    second.socdMode = SocdMode::OFF;
    second.activeBrakeProfileIndex = 4;

    if (!config_io::Save(first)) {
        std::cerr << "initial Save failed, GetLastError=" << GetLastError() << '\n';
        return 2;
    }

    for (auto mode : {SocdMode::FULL, SocdMode::HUMANIZED, SocdMode::OFF}) {
        RuntimeConfig input{}; input.socdMode = mode;
        assert(config_io::Save(input));
        RuntimeConfig output{};
        assert(config_io::Load(output) && output.socdMode == mode);
    }
    assert(WritePrivateProfileStringW(L"Strafe", L"socd_mode", nullptr, configPath.c_str()));
    RuntimeConfig legacy{};
    assert(config_io::Load(legacy) && legacy.socdMode == SocdMode::FULL);
    for (const auto* bad : {L"99", L"1.5", L"garbage", L""}) {
        assert(WritePrivateProfileStringW(L"Strafe", L"socd_mode", bad, configPath.c_str()));
        RuntimeConfig invalid{};
        assert(config_io::Load(invalid) && invalid.socdMode == SocdMode::OFF);
    }

    std::vector<std::thread> writers;
    for (int writer = 0; writer < 4; ++writer) {
        writers.emplace_back([&, writer] {
            const RuntimeConfig& cfg = (writer & 1) ? first : second;
            for (int iteration = 0; iteration < 50; ++iteration) {
                assert(config_io::Save(cfg));
            }
        });
    }
    for (auto& writer : writers) writer.join();

    RuntimeConfig loaded{};
    assert(config_io::Load(loaded));
    const bool isFirst = loaded.quickTapMs == first.quickTapMs &&
                         loaded.activeBrakeProfileIndex == first.activeBrakeProfileIndex && loaded.socdMode == first.socdMode;
    const bool isSecond = loaded.quickTapMs == second.quickTapMs &&
                          loaded.activeBrakeProfileIndex == second.activeBrakeProfileIndex && loaded.socdMode == second.socdMode;
    assert(isFirst || isSecond);
    assert(GetFileAttributesW(tempPath.c_str()) == INVALID_FILE_ATTRIBUTES);

    DeleteFileW(configPath.c_str());
    std::cout << "test_config_io_atomic: concurrent replace stable\n";
    return 0;
}
