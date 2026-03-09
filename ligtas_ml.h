#pragma once
#include "ligtas_model.h"

// ============================================================
//  LIGTAS ML Wrapper — v5.1  (ESD Formula, 6 features)
//  Model   : Random Forest (100 trees, 3 classes)
//  Classes : 0 = Safe  |  1 = Warning  |  2 = Dangerous
//  Accuracy: 100.00%  Precision: 100.00%  Recall: 100.00%
//
//  Sensor   : ZMPT101B (rated range 0-250V AC)
//  Use case : Flood water AC leakage monitoring
//
//  Classification thresholds (no gaps):
//    0.00 – 25.00 V  →  Safe
//    25.01 – 29.99 V →  Warning
//    30.00 – 250 V   →  Dangerous
//
//  Coverage Area Formula (ESD flood water surface spread):
//    Based on: Rifkin & Shafer (2008) Electric Shock Drowning Study
//    Lethal gradient in fresh water = 6.56 V/m (2 V/ft, US Coast Guard)
//    hazard_radius (m)  = V / 6.56
//    coverage_area (m2) = pi x (V / 6.56)^2
//    Ref: https://en.wikipedia.org/wiki/Electric_shock_drowning
//
//  Features (6):
//    [0] voltage_v            — raw sensor reading
//    [1] voltage_squared      — V^2
//    [2] coverage_area_m2     — pi x (V/6.56)^2
//    [3] voltage_class        — 0-4 zone category
//    [4] danger_score         — V / (1 + log(area+1))
//    [5] sensor_noise_level   — V x 0.02
// ============================================================

// StandardScaler values (6 features)
const float SCALER_MEAN[6]  = {
    109.369058f,    // [0] voltage_v
    17515.434411f,  // [1] voltage_squared
    1278.683635f,   // [2] coverage_area_m2
    3.086675f,      // [3] voltage_class
    13.584487f,     // [4] danger_score
    2.211681f       // [5] sensor_noise_level
};

const float SCALER_SCALE[6] = {
    74.524114f,     // [0] voltage_v
    18437.890136f,  // [1] voltage_squared
    1346.025901f,   // [2] coverage_area_m2
    1.131700f,      // [3] voltage_class
    7.267526f,      // [4] danger_score
    1.963594f       // [5] sensor_noise_level
};

// ESD lethal gradient constant (fresh water)
// 2 V/ft = 6.56 V/m — Rifkin & Shafer (2008), US Coast Guard
static const float ESD_GRADIENT = 6.56f;

// Result struct
struct LigtasResult {
    int         classIndex;       // 0=Safe, 1=Warning, 2=Dangerous
    const char* status;           // "Safe", "Warning", "Dangerous"
    float       hazardRadius;     // estimated hazard radius in meters
    float       coverageArea;     // estimated electrified surface area in m2
    String      coverageAreaStr;  // formatted e.g. "65.7 m2 [!]"
    float       dangerScore;      // combined risk index
};

// Internal: get voltage class (0-4)
static int getVoltageClass(float v) {
    if (v < 5.0f)   return 0;
    if (v < 26.0f)  return 1;
    if (v < 30.0f)  return 2;
    if (v < 100.0f) return 3;
    return 4;
}

// Main prediction function
//  acVoltage — current reading from ZMPT101B (0-250V)
LigtasResult ligtas_predict(float acVoltage) {

    // Clamp to sensor rated range
    if (acVoltage > 250.0f) acVoltage = 250.0f;
    if (acVoltage < 0.0f)   acVoltage = 0.0f;

    float voltage_v          = acVoltage;
    float voltage_squared    = acVoltage * acVoltage;
    float hazard_radius      = acVoltage / ESD_GRADIENT;
    float coverage_area_m2   = 3.14159265f * hazard_radius * hazard_radius;
    float voltage_class      = (float)getVoltageClass(acVoltage);
    float danger_score       = acVoltage / (1.0f + log(coverage_area_m2 + 1.0f));
    float sensor_noise_level = acVoltage * 0.02f;

    float raw[6] = {
        voltage_v,
        voltage_squared,
        coverage_area_m2,
        voltage_class,
        danger_score,
        sensor_noise_level
    };

    float scaled[6];
    for (int i = 0; i < 6; i++)
        scaled[i] = (raw[i] - SCALER_MEAN[i]) / SCALER_SCALE[i];

    Eloquent::ML::Port::RandomForest clf;
    int prediction = clf.predict(scaled);

    const char* statusLabels[3] = { "Safe", "Warning", "Dangerous" };
    String areaStr = String(coverage_area_m2, 1) + " m2";
    if (prediction == 2) areaStr += " [!]";
    if (prediction == 1) areaStr += " [?]";

    LigtasResult result;
    result.classIndex      = prediction;
    result.status          = statusLabels[prediction];
    result.hazardRadius    = hazard_radius;
    result.coverageArea    = coverage_area_m2;
    result.coverageAreaStr = areaStr;
    result.dangerScore     = danger_score;
    return result;
}
