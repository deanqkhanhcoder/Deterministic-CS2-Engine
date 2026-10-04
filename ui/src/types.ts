export interface BrakeProfile {
  overlapDurationUs: number;
  brakeBiasMultiplier: number;
  authorityBiasMs: number;
  aggressivenessCurve: number;
  momentumMemoryMs: number;
  accuracyThreshold: number;
}

export interface RuntimeConfig {
  quickTapMs: number;
  maxScaleMs: number;
  crouchMult: number;
  latencyMarginMs: number;
  minStopMs: number;
  lutMaxMs: number;
  walkMemoryMs: number;
  walkRatioSkip: number;
  walkRatioLight: number;
  minWalkStopMs: number;
  walkMaxStopMs: number;
  decayK: number;
  dirChangePenaltyMs: number;
  tapSpamWindowMs: number;
  stopStrengthMin: number;
  tapSpamAlpha: number;
  tapSpamHalfLifeMs: number;
  minTapUs: number;
  physMaxSpeed: number;
  physFriction: number;
  physStopSpeed: number;
  physAccelerate: number;
  hardwareDebounceUs: number;
  humanizeMinUs: number;
  humanizeMaxUs: number;
  activeBrakeProfileIndex: number;
  safeModeEnabled: boolean;
  bhopEnabled: boolean;
  bhopMode: number;
  airborneDelayMs: number;
  scrollBurstGapMs: number;
  landingScanMs: number;
  airborneLockMs: number;
  spamIntervalMs: number;
  brakeProfiles: BrakeProfile[];
}

export interface TelemetryData {
  runtimeState: number;
  suspended: boolean;
  axisStateX: number;
  axisStateY: number;
  keys: {
    phys: [boolean, boolean, boolean, boolean];
    logical: [boolean, boolean, boolean, boolean];
  };
  bhop: {
    enabled: boolean;
    mode: number;
    state: number;
  };
  hookInstalled: boolean;
  targetActive: boolean;
  targetName: string;
  uptimeMs: number;
  timingActive: boolean;
  metrics: {
    hookLatencyP50Us: number;
    hookLatencyP99Us: number;
    timerJitterUs: number;
    wakeOversleepUs: number;
    spinDurationUs: number;
    stateMutationLatencyUs: number;
    schedulerSpikes: number;
    coreMigrations: number;
    wakeVarianceUs: number;
    timerOversleepPeakUs: number;
  };
  profile: {
    name: string;
    overlapUs: number;
    brakeBias: number;
    authorityBiasMs: number;
    aggrCurve: number;
    accuracyThreshold: number;
  };
  affinity: {
    timingCore: number;
    hookCore: number;
    smtCollision: boolean;
  };
  timeline: {
    index: number;
    jitter: number[];
    oversleep: number[];
  };
}
