#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <fstream>
#include "movement_reconstruction.h"
#include "runtime_config.h"
#include "config.h"

using namespace movement;

void RunSimulations(const std::string& outPath) {
    std::ofstream out(outPath);
    if (!out.is_open()) return;
    
    out << "Test,Vx,Vy,WishMode,Crouch,StopMs\n";

    // Re-initialize physics engine
    InitLUT();
    
    // Simulate all combinations of speeds up to 250
    std::vector<double> testSpeeds = { 0.0, 50.0, 100.0, 150.0, 176.77, 200.0, 250.0 };
    
    for (double vx : testSpeeds) {
        for (double vy : testSpeeds) {
            for (int mode = 0; mode <= 3; mode++) {
                for (int crouch = 0; crouch <= 1; crouch++) {
                    int ms = LookupStopDur2D(vx, vy, mode, crouch == 1);
                    out << std::fixed << std::setprecision(2) 
                        << "Stop2D," << vx << "," << vy << "," << mode << "," << crouch << "," << ms << "\n";
                }
            }
        }
    }
    
    out.close();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: deterministic_simulator <output.csv>\n";
        return 1;
    }
    RunSimulations(argv[1]);
    return 0;
}
