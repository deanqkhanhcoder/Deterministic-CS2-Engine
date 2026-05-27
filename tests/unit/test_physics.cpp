#include "movement_reconstruction.h"
#include "runtime_config.h"
#include <iostream>

using namespace physics;

int main() {
    RuntimeConfig rc;
    
    // Diagonal stop
    int ms = SimulateTrueStopDuration(176.77, 176.77, -0.7071, -0.7071, Axis::X, rc, false);
    std::cout << "Diagonal Stop X: " << ms << " ms\n";
    
    // Straight stop
    int ms2 = SimulateTrueStopDuration(250.0, 0.0, -1.0, 0.0, Axis::X, rc, false);
    std::cout << "Straight Stop X: " << ms2 << " ms\n";
    
    // 1-key diagonal stop
    int ms3 = SimulateTrueStopDuration(176.77, 176.77, -0.7071, 0.7071, Axis::X, rc, false);
    std::cout << "1-Key Diagonal Stop X: " << ms3 << " ms\n";

    return 0;
}
