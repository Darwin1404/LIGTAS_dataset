#pragma once
#include "ligtas_model.h"

// ============================================================
//  LIGTAS ML Wrapper — v7.0  (2-Class: Safe / Dangerous)
//  Model   : Random Forest (100 trees, 2 classes)
//  Classes : 0 = Safe  |  1 = Dangerous
//  Accuracy: 96.54%  Precision: 96.54%  Recall: 96.54%
//
//  Sensor   : ZMPT101B (rated range 0-250V AC)
//  Use case : Flood water AC leakage monitoring
//  Sampling : 500ms interval (call ligtas_predict every 500ms)
//
//  Classification thresholds:
//    0.00 –  5.00 V  →  Dead Zone (clamped to Safe, no ML)
//    5.01 – 250 V    →  ML decision (all cases, no hard threshold)
//
//  CHANGES IN v7.0 (from v6.0):
//  ┌────────────────────────────────────────────────────────────┐
//  │  • Hard threshold at 30V REMOVED — ML now handles all      │
//  │    decisions above dead zone.                              │
//  │  • Spike rejection added via 3 new temporal features:      │
//  │      dv_dt          — voltage change from previous sample  │
//  │      rolling_mean_5 — average of last 5 readings (2.5s)   │
//  │      consec_above_30 — consecutive readings >= 30V         │
//  │  • Spikes (1-2 samples of high V) → classified as Safe     │
//  │  • Real leakage (sustained ≥30V, consec ≥ 3) → Dangerous  │
//  │  • Dataset expanded with labeled spike samples.            │
//  │  • Feature count: 6 → 9                                   │
//  └────────────────────────────────────────────────────────────┘
//
//  Coverage Area Formula (ESD flood water surface spread):
//    Based on: Rifkin & Shafer (2008) Electric Shock Drowning Study
//    Lethal gradient in fresh water = 6.56 V/m (2 V/ft, US Coast Guard)
//    hazard_radius (m)  = V / 6.56
//    coverage_area (m2) = pi x (V / 6.56)^2
//    Ref: https://en.wikipedia.org/wiki/Electric_shock_drowning
//
//  Features (9):
//    [0] voltage_v            — raw sensor reading (clamped, dead zone applied)
//    [1] voltage_squared      — V^2
//    [2] coverage_area_m2     — pi x (V/6.56)^2
//    [3] voltage_class        — 0-4 zone category
//    [4] danger_score         — V / (1 + log(area+1))
//    [5] sensor_noise_level   — V x 0.02
//    [6] dv_dt                — change from previous reading (spike detector)
//    [7] rolling_mean_5       — average of last 5 readings
//    [8] consec_above_30      — consecutive readings >= 30V
//
//  IMPORTANT: Call ligtas_predict() exactly once every 500ms.
//             Temporal features depend on this timing.
// ============================================================

// ── StandardScaler values (v7.0, 9 features, 2 classes) ──────
const float SCALER_MEAN[9]  = {
    87.236891f,     // [0] voltage_v
    12524.263352f,  // [1] voltage_squared
    914.311927f,    // [2] coverage_area_m2
    2.858557f,      // [3] voltage_class
    11.416161f,     // [4] danger_score
    1.744738f,      // [5] sensor_noise_level
    9.331306f,      // [6] dv_dt
    72.518186f,     // [7] rolling_mean_5
    4.974994f       // [8] consec_above_30
};

const float SCALER_SCALE[9] = {
    70.099844f,     // [0] voltage_v
    16763.105686f,  // [1] voltage_squared
    1223.761193f,   // [2] coverage_area_m2
    1.159209f,      // [3] voltage_class
    6.913384f,      // [4] danger_score
    1.401997f,      // [5] sensor_noise_level
    30.771785f,     // [6] dv_dt
    65.399814f,     // [7] rolling_mean_5
    4.907424f       // [8] consec_above_30
};

// ── Constants ─────────────────────────────────────────────────
static const float ESD_GRADIENT      = 6.56f;
static const float DEAD_ZONE_V       = 5.0f;
static const int   ROLLING_WINDOW    = 5;    // 5 samples = 2.5 seconds

// ── Temporal state (persists between calls) ───────────────────
static float _prev_voltage              = 0.0f;
static float _rolling_buf[ROLLING_WINDOW] = {0,0,0,0,0};
static int   _rolling_idx               = 0;
static int   _consec_above_30           = 0;

// ── Result struct ─────────────────────────────────────────────
struct LigtasResult {
    int         classIndex;       // 0=Safe, 1=Dangerous
    const char* status;           // "Safe", "Dangerous"
    float       hazardRadius;     // estimated hazard radius in meters
    float       coverageArea;     // estimated electrified surface area in m2
    String      coverageAreaStr;  // formatted e.g. "65.7 m2 [!]"
    float       dangerScore;      // combined risk index
};

// ── Internal: voltage class zone (0-4) ───────────────────────
static int getVoltageClass(float v) {
    if (v < 5.0f)   return 0;
    if (v < 26.0f)  return 1;
    if (v < 30.0f)  return 2;
    if (v < 100.0f) return 3;
    return 4;
}

// ── Internal: rolling mean of last 5 readings ─────────────────
static float getRollingMean() {
    float sum = 0.0f;
    for (int i = 0; i < ROLLING_WINDOW; i++) sum += _rolling_buf[i];
    return sum / (float)ROLLING_WINDOW;
}

// ── Reset temporal state (call on system restart/init) ────────
void ligtas_reset() {
    _prev_voltage     = 0.0f;
    _consec_above_30  = 0;
    _rolling_idx      = 0;
    for (int i = 0; i < ROLLING_WINDOW; i++) _rolling_buf[i] = 0.0f;
}

// ── Main prediction function ──────────────────────────────────
//  acVoltage — current reading from ZMPT101B (0-250V)
//
//  Call this ONCE every 500ms in your main loop.
LigtasResult ligtas_predict(float acVoltage) {

    // Clamp to sensor rated range
    if (acVoltage > 250.0f) acVoltage = 250.0f;
    if (acVoltage < 0.0f)   acVoltage = 0.0f;

    // ── Update temporal state ─────────────────────────────────
    float dv_dt = acVoltage - _prev_voltage;
    _prev_voltage = acVoltage;

    _rolling_buf[_rolling_idx] = acVoltage;
    _rolling_idx = (_rolling_idx + 1) % ROLLING_WINDOW;
    float rolling_mean = getRollingMean();

    _consec_above_30 = (acVoltage >= 30.0f) ? (_consec_above_30 + 1) : 0;

    // ── FILTER: Dead Zone ─────────────────────────────────────
    // Any reading <= 5V is sensor noise floor. Return Safe instantly.
    if (acVoltage <= DEAD_ZONE_V) {
        LigtasResult r;
        r.classIndex      = 0;
        r.status          = "Safe";
        r.hazardRadius    = 0.0f;
        r.coverageArea    = 0.0f;
        r.coverageAreaStr = "0.0 m2";
        r.dangerScore     = 0.0f;
        return r;
    }

    // ── Compute static features ───────────────────────────────
    float voltage_v          = acVoltage;
    float voltage_squared    = acVoltage * acVoltage;
    float hazard_radius      = acVoltage / ESD_GRADIENT;
    float coverage_area_m2   = 3.14159265f * hazard_radius * hazard_radius;
    float voltage_class      = (float)getVoltageClass(acVoltage);
    float danger_score       = acVoltage / (1.0f + log(coverage_area_m2 + 1.0f));
    float sensor_noise_level = acVoltage * 0.02f;

    // ── ML Prediction (all voltages above dead zone) ──────────
    float raw[9] = {
        voltage_v,
        voltage_squared,
        coverage_area_m2,
        voltage_class,
        danger_score,
        sensor_noise_level,
        dv_dt,
        rolling_mean,
        (float)_consec_above_30
    };

    float scaled[9];
    for (int i = 0; i < 9; i++)
        scaled[i] = (raw[i] - SCALER_MEAN[i]) / SCALER_SCALE[i];

    Eloquent::ML::Port::RandomForest clf;
    int final_prediction = clf.predict(scaled);

    // ── Build result ──────────────────────────────────────────
    const char* statusLabels[2] = { "Safe", "Dangerous" };
    String areaStr = String(coverage_area_m2, 1) + " m2";
    if (final_prediction == 1) areaStr += " [!]";

    LigtasResult result;
    result.classIndex      = final_prediction;
    result.status          = statusLabels[final_prediction];
    result.hazardRadius    = hazard_radius;
    result.coverageArea    = coverage_area_m2;
    result.coverageAreaStr = areaStr;
    result.dangerScore     = danger_score;
    return result;
}
