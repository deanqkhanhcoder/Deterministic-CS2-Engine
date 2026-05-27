#include "topology.h"
#include "debug_logger.h"
#include <vector>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <avrt.h>

namespace topology {

struct Core {
    uint8_t efficiencyClass; // Higher is P-core, lower is E-core
    GROUP_AFFINITY affinity;
    bool isAssigned;
};

static std::vector<Core> s_pCores;
static std::vector<Core> s_eCores;
static std::mutex s_mutex;

void Init() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_pCores.clear();
    s_eCores.clear();

    DWORD bufferSize = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bufferSize);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        DLOG_ERR(Scheduler, "Topology: Failed to get buffer size");
        return;
    }

    std::vector<uint8_t> buffer(bufferSize);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, 
        reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()), &bufferSize)) {
        DLOG_ERR(Scheduler, "Topology: Failed to get processor info");
        return;
    }

    std::vector<Core> tempCores;
    uint8_t maxEfficiency = 0;

    auto* ptr = buffer.data();
    while (ptr < buffer.data() + bufferSize) {
        auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(ptr);
        if (info->Relationship == RelationProcessorCore) {
            Core c;
            c.efficiencyClass = info->Processor.EfficiencyClass;
            // Get the first group affinity for this core
            if (info->Processor.GroupCount > 0) {
                c.affinity = info->Processor.GroupMask[0];
                c.isAssigned = false;
                tempCores.push_back(c);
                if (c.efficiencyClass > maxEfficiency) {
                    maxEfficiency = c.efficiencyClass;
                }
            }
        }
        ptr += info->Size;
    }

    for (auto& c : tempCores) {
        if (maxEfficiency == 0 || c.efficiencyClass == maxEfficiency) {
            // P-core (or symmetric core on non-hybrid CPU): isolate the primary thread to avoid SMT sharing
            ULONG_PTR mask = c.affinity.Mask;
            if (mask != 0) {
                c.affinity.Mask = mask & (~mask + 1); // Lowest set bit
            }
            s_pCores.push_back(c);
        } else {
            // E-core: keep full core mask (including SMT siblings if any exist on other architectures)
            s_eCores.push_back(c);
        }
    }

    // Exclude Core 0 entirely if we have enough P-cores to spare. Core 0 handles the majority of NT kernel DPCs.
    if (s_pCores.size() > 1) {
        auto it = std::find_if(s_pCores.begin(), s_pCores.end(), [](const Core& c) {
            return (c.affinity.Mask & 1) != 0 && c.affinity.Group == 0;
        });
        if (it != s_pCores.end()) {
            s_pCores.erase(it);
        }
    }

    DLOG_INFO(Scheduler, "Topology initialized: %zu P-Cores, %zu E-Cores", s_pCores.size(), s_eCores.size());
}

bool PinCriticalThread(const wchar_t* mmcssProfile) {
    std::lock_guard<std::mutex> lock(s_mutex);
    
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristicsW(mmcssProfile, &taskIndex);
    if (hTask) {
        DLOG_INFO(Scheduler, "MMCSS registered critical thread under profile: %ls", reinterpret_cast<int64_t>(mmcssProfile));
    } else {
        DLOG_WARN(Scheduler, "Failed to register MMCSS for critical thread (error %lu)", GetLastError());
    }

    // Find an unassigned P-Core
    for (auto& core : s_pCores) {
        if (!core.isAssigned) {
            core.isAssigned = true;
            if (SetThreadGroupAffinity(GetCurrentThread(), &core.affinity, nullptr)) {
                DLOG_INFO(Scheduler, "Pinned critical thread to P-Core mask 0x%llx in Group %u", core.affinity.Mask, core.affinity.Group);
            } else {
                DLOG_ERR(Scheduler, "Failed to set thread affinity for critical thread (error %lu)", GetLastError());
            }
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
            return true;
        }
    }
    
    // Fallback if all assigned
    DLOG_WARN(Scheduler, "No unassigned P-Cores left for critical thread");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    return false;
}

bool PinHookThread(const wchar_t* mmcssProfile) {
    std::lock_guard<std::mutex> lock(s_mutex);
    
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristicsW(mmcssProfile, &taskIndex);
    if (hTask) {
        DLOG_INFO(Scheduler, "MMCSS registered hook thread under profile: %ls", reinterpret_cast<int64_t>(mmcssProfile));
    } else {
        DLOG_WARN(Scheduler, "Failed to register MMCSS for hook thread (error %lu)", GetLastError());
    }

    // Find an unassigned P-Core (prioritize high efficiency)
    for (auto& core : s_pCores) {
        if (!core.isAssigned) {
            core.isAssigned = true;
            if (SetThreadGroupAffinity(GetCurrentThread(), &core.affinity, nullptr)) {
                DLOG_INFO(Scheduler, "Pinned hook thread to P-Core mask 0x%llx in Group %u", core.affinity.Mask, core.affinity.Group);
            } else {
                DLOG_ERR(Scheduler, "Failed to set thread affinity for hook thread (error %lu)", GetLastError());
            }
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
            return true;
        }
    }
    
    DLOG_WARN(Scheduler, "No unassigned P-Cores left for hook thread");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    return false;
}

bool PinBackgroundThread() {
    std::lock_guard<std::mutex> lock(s_mutex);
    
    // Prefer E-cores
    if (!s_eCores.empty()) {
        GROUP_AFFINITY eCoreAffinity = s_eCores[0].affinity;
        // Combine E-core masks belonging to the same processor group
        eCoreAffinity.Mask = 0;
        for (const auto& c : s_eCores) {
            if (c.affinity.Group == eCoreAffinity.Group) {
                eCoreAffinity.Mask |= c.affinity.Mask;
            }
        }
        
        if (SetThreadGroupAffinity(GetCurrentThread(), &eCoreAffinity, nullptr)) {
            DLOG_INFO(Scheduler, "Pinned background thread to E-Core mask 0x%llx in Group %u", eCoreAffinity.Mask, eCoreAffinity.Group);
        } else {
            DLOG_ERR(Scheduler, "Failed to set thread affinity for background thread (error %lu)", GetLastError());
        }
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        return true;
    }
    
    // If no E-cores, float on all cores but drop priority
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    return false;
}

bool PinUIThread() {
    std::lock_guard<std::mutex> lock(s_mutex);
    
    // Prefer E-cores for UI to keep P-cores clean
    if (!s_eCores.empty()) {
        GROUP_AFFINITY eCoreAffinity = s_eCores[0].affinity;
        eCoreAffinity.Mask = 0;
        for (const auto& c : s_eCores) {
            if (c.affinity.Group == eCoreAffinity.Group) {
                eCoreAffinity.Mask |= c.affinity.Mask;
            }
        }
        if (SetThreadGroupAffinity(GetCurrentThread(), &eCoreAffinity, nullptr)) {
            DLOG_INFO(Scheduler, "Pinned UI thread to E-Core mask 0x%llx in Group %u", eCoreAffinity.Mask, eCoreAffinity.Group);
        }
        return true;
    }
    
    // If no E-cores, calculate mask of all unassigned cores
    GROUP_AFFINITY safeAffinity = {0, 0, {0}};
    safeAffinity.Group = 0;
    bool foundSafe = false;
    for (const auto& c : s_pCores) {
        if (!c.isAssigned) {
            safeAffinity.Group = c.affinity.Group;
            safeAffinity.Mask |= c.affinity.Mask;
            foundSafe = true;
        }
    }
    
    if (foundSafe && safeAffinity.Mask != 0) {
        SetThreadGroupAffinity(GetCurrentThread(), &safeAffinity, nullptr);
        DLOG_INFO(Scheduler, "Pinned UI thread away from isolated P-Cores: mask 0x%llx", safeAffinity.Mask);
        return true;
    }
    
    return false;
}

bool AreSmtSiblings(uint16_t groupA, uint8_t cpuA, uint16_t groupB, uint8_t cpuB) {
    if (groupA != groupB) return false;
    if (cpuA == cpuB) return false;
    std::lock_guard<std::mutex> lock(s_mutex);
    for (const auto& core : s_pCores) {
        if (core.affinity.Group == groupA) {
            bool hasA = (core.affinity.Mask & (1ULL << cpuA)) != 0;
            bool hasB = (core.affinity.Mask & (1ULL << cpuB)) != 0;
            if (hasA && hasB) return true;
        }
    }
    for (const auto& core : s_eCores) {
        if (core.affinity.Group == groupA) {
            bool hasA = (core.affinity.Mask & (1ULL << cpuA)) != 0;
            bool hasB = (core.affinity.Mask & (1ULL << cpuB)) != 0;
            if (hasA && hasB) return true;
        }
    }
    return false;
}

} // namespace topology
