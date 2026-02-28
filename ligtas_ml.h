#pragma once
#include "ligtas_model.h"

// ============================================================
//  LIGTAS ML Wrapper — v3.0  (Gap-Fixed, 8 features)
//  Model   : Random Forest (100 trees, 3 classes)
//  Classes : 0 = Safe  |  1 = Check  |  2 = Dangerous
//  Accuracy: 99.96%  Precision: 99.96%  Recall: 99.96%
//
//  Scaler values from ligtas_scaler.pkl — DO NOT EDIT
//  Feature order MUST match training (Cell 7 of notebook):
//    [0] voltage_v
//    [1] voltage_squared        (V²)
//    [2] coverage_area_m2       (π × V²)
//    [3] voltage_class          (0–4)
//    [4] danger_score           (V / (1 + log(area+1)))
//    [5] voltage_delta          (current V – previous V)
//    [6] spike_flag             (1 if |delta| > 10V)
//    [7] sensor_noise_level     (V × 0.02)
// ============================================================

// ── StandardScaler values (8 features) ──────────────────────
const float SCALER_MEAN[8]  = {
    447.294408f,      // [0] voltage_v
    295631.146343f,   // [1] voltage_squared
    928752.637491f,   // [2] coverage_area_m2
    3.620893f,        // [3] voltage_class
    30.187455f,       // [4] danger_score
    -0.073383f,       // [5] voltage_delta
    0.008770f,        // [6] spike_flag
    7.161230f         // [7] sensor_noise_level
};

const float SCALER_SCALE[8] = {
    309.125959f,      // [0] voltage_v
    298721.604436f,   // [1] voltage_squared
    938461.597968f,   // [2] coverage_area_m2
    0.910172f,        // [3] voltage_class
    19.044808f,       // [4] danger_score
    38.037916f,       // [5] voltage_delta
    0.093237f,        // [6] spike_flag
    8.147400f         // [7] sensor_noise_level
};

// ── Result struct ─────────────────────────────────────────────
struct LigtasResult {
    int     classIndex;       // 0=Safe, 1=Check, 2=Dangerous
    const char* status;       // "Safe", "Check", "Dangerous"
    float   coverageArea;     // estimated electrified area in m2
    String  coverageAreaStr;  // formatted e.g. "2827.4 m2"
    float   dangerScore;      // combined risk index
    int     spikeFlag;        // 1 if sudden voltage jump detected
};

// ── Internal: get voltage class (0–4) ────────────────────────
static int getVoltageClass(float v) {
    if (v < 5.0f)   return 0;
    if (v < 15.0f)  return 1;
    if (v < 30.0f)  return 2;
    if (v < 100.0f) return 3;
    return 4;
}

// ── Main prediction function ──────────────────────────────────
//
//  acVoltage   — current reading from ZMPT101B (V)
//  prevVoltage — previous reading (V), pass 0.0f on first call
//
LigtasResult ligtas_predict(float acVoltage, float prevVoltage = 0.0f) {

    // Feature 0: raw voltage
    float voltage_v = acVoltage;

    // Feature 1 & 2: GAP 1 & 3 FIX — A = pi x V^2, no distance assumption
    float voltage_squared  = acVoltage * acVoltage;
    float coverage_area_m2 = 3.14159265f * voltage_squared;

    // Feature 3: voltage class
    float voltage_class = (float)getVoltageClass(acVoltage);

    // Feature 4: danger score
    float danger_score = acVoltage / (1.0f + log(coverage_area_m2 + 1.0f));

    // Feature 5 & 6: GAP 4 FIX — temporal features
    float voltage_delta = acVoltage - prevVoltage;
    float spike_flag    = (fabs(voltage_delta) > 10.0f) ? 1.0f : 0.0f;

    // Feature 7: GAP 6 FIX — sensor noise estimate (ZMPT101B ~2%)
    float sensor_noise_level = acVoltage * 0.02f;

    // Build feature vector
    float raw[8] = {
        voltage_v,
        voltage_squared,
        coverage_area_m2,
        voltage_class,
        danger_score,
        voltage_delta,
        spike_flag,
        sensor_noise_level
    };

    // Apply StandardScaler: z = (x - mean) / scale
    float scaled[8];
    for (int i = 0; i < 8; i++) {
        scaled[i] = (raw[i] - SCALER_MEAN[i]) / SCALER_SCALE[i];
    }

    // Random Forest inference
    Eloquent::ML::Port::RandomForest clf;
    int prediction = clf.predict(scaled);

    // Build result
    const char* statusLabels[3] = { "Safe", "Check", "Dangerous" };
    String areaStr = String(coverage_area_m2, 1) + " m2";
    if (prediction == 2) areaStr += " [!]";
    if (prediction == 1) areaStr += " [?]";

    LigtasResult result;
    result.classIndex      = prediction;
    result.status          = statusLabels[prediction];
    result.coverageArea    = coverage_area_m2;
    result.coverageAreaStr = areaStr;
    result.dangerScore     = danger_score;
    result.spikeFlag       = (int)spike_flag;
    return result;
}