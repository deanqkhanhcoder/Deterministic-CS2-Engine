# Counter-Strafe V26 Architecture

This document describes the modular architecture of the Counter-Strafe Engine V26.

## Module Decoupling

The engine has been decoupled from a monolithic `state_engine.cpp` into 6 distinct sub-systems:

```mermaid
graph TD
    IR[input_router.cpp] --> |Event| CSC[counterstrafe_controller.cpp]
    IR --> |Event| AFC[autofire_controller.cpp]
    
    TL[timer_lifecycle.cpp] --> |Tick| CSC
    
    CSC --> |Movement State| SR[state_reconciliation.cpp]
    AFC --> |Shot State| SR
    
    SR --> |Rebuild/Sync| EI[engine_internal.h]
    
    MR[movement_reconstruction.cpp] -.-> |Physics LUT| CSC
    
    subgraph Shared Context
        EI[engine_internal.h : s_state, s_stateMutex]
    end
```

## Lifecycle of a Key Press

```mermaid
sequenceDiagram
    participant OS as Windows Hook
    participant IR as Input Router
    participant CSC as CounterStrafe Controller
    participant MR as Movement Reconstruction
    participant TL as Timer Lifecycle
    
    OS->>IR: HandleKeyDown(W)
    IR->>CSC: ResolveAxis(Y)
    CSC->>MR: EstimateTrueVelocity2D()
    MR-->>CSC: Current Vx, Vy
    CSC->>MR: LookupStopDur2D()
    MR-->>CSC: Target Stop Ms
    CSC->>TL: SetTimer(W, StopMs)
```

## State Reconciliation

The `RebuildState` function uses a Local Snapshot pattern to synchronize logical state with OS physical state without blocking the hook thread.

```mermaid
stateDiagram-v2
    [*] --> SyncPhysical : OS Focus Loss
    SyncPhysical --> RebuildBhop : Read VK_SPACE
    RebuildBhop --> RebuildWASD : Read WASD
    RebuildWASD --> FlushBatch : Re-evaluate Axis
    FlushBatch --> [*] : Publish State
```

## Regression CI Pipeline

The project features a standalone, headless deterministic simulator (`tools/forensics/deterministic_simulator.cpp`) that links against the physics engine directly to output CSV dumps for regression testing (`tools/run_regression.py`). This guarantees zero gameplay variation across refactors.
