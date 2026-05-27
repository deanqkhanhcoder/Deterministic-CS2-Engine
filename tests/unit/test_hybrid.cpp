#include "movement_reconstruction.h"
#include "runtime_config.h"
#include <iostream>

using namespace physics;

int main() {
    rcfg::Init();
    InitLUT();
    
    // Diagonal stop
    int ms = LookupStopDur2D(176.0, 176.0, 2, false);
    std::cout << "Diagonal Stop X (counter both): " << ms << " ms\n";
    
    // Straight stop
    int ms2 = LookupStopDur2D(250.0, 0.0, 0, false);
    std::cout << "Straight Stop X: " << ms2 << " ms\n";
    
    // 1-key diagonal stop
    int ms3 = LookupStopDur2D(176.0, 176.0, 1, false);
    std::cout << "1-Key Diagonal Stop X (hold orth): " << ms3 << " ms\n";

    // 1-key diagonal stop (release orth)
    int ms4 = LookupStopDur2D(176.0, 176.0, 0, false);
    std::cout << "1-Key Diagonal Stop X (release orth): " << ms4 << " ms\n";

    return 0;
}
