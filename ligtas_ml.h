#pragma once
#include "ligtas_model.h"

// ============================================================
//  LIGTAS ML Wrapper — v6.0  (2-Class: Safe / Dangerous)
//  Model   : Random Forest (100 trees, 2 classes)
//  Classes : 0 = Safe  |  1 = Dangerous
//  Accuracy: 100.00%  Precision: 100.00%  Recall: 100.00%
//
//  Sensor   : ZMPT101B (rated range 0-250V AC)
//  Use case : Flood water AC leakage monitoring
//
//  Classification thresholds:
//    0.00 –  5.00 V  →  Dead Zone (clamped to Safe, no ML)
//    5.01 – 29.99 V  →  Safe      (via ML)
//    30.00 – 250 V   →  Dangerous (hard threshold — immediate, no ML)
//
//  CHANGES IN v6.0 (from v5.3):
//  ┌────────────────────────────────────────────────────────────┐
//  │  • Warning class (25–30V) removed entirely.                │
//  │  • Dataset relabeled: all voltages < 30V → Safe (0),       │
//  │    all voltages ≥ 30V → Dangerous (1).                     │
//  │  • Model retrained as binary classifier (2 classes).       │
//  │  • Debounce filter removed — no longer needed without      │
//  │    a Warning boundary zone.                                 │
//  │  • Hard threshold at 30V still bypasses ML for immediate   │
//  │    Dangerous confirmation.                                  │
//  │  • Dead zone (0–5V) unchanged — returns Safe instantly.    │
//  └────────────────────────────────────────────────────────────┘
//
//  Coverage Area Formula (ESD flood water surface spread):
//    Based on: Rifkin & Shafer (2008) Electric Shock Drowning Study
//    Lethal gradient in fresh water = 6.56 V/m (2 V/ft, US Coast Guard)
//    hazard_radius (m)  = V / 6.56
//    coverage_area (m2) = pi x (V / 6.56)^2
//    Ref: https://en.wikipedia.org/wiki/Electric_shock_drowning
//
//  Features (6):
//    [0] voltage_v            — raw sensor reading (clamped, dead zone applied)
//    [1] voltage_squared      — V^2
//    [2] coverage_area_m2     — pi x (V/6.56)^2
//    [3] voltage_class        — 0-4 zone category
//    [4] danger_score         — V / (1 + log(area+1))
//    [5] sensor_noise_level   — V x 0.02
// ============================================================

// ── StandardScaler values (retrained v6.0 revised, 6 features, 2 classes) ─
const float SCALER_MEAN[6]  = {
    98.928814f,     // [0] voltage_v
    15660.134569f,  // [1] voltage_squared
    1143.240717f,   // [2] coverage_area_m2
    2.839912f,      // [3] voltage_class
    12.387490f,     // [4] danger_score
    1.991743f       // [5] sensor_noise_level
};

const float SCALER_SCALE[6] = {
    76.636964f,     // [0] voltage_v
    18163.302349f,  // [1] voltage_squared
    1325.980100f,   // [2] coverage_area_m2
    1.320061f,      // [3] voltage_class
    7.733209f,      // [4] danger_score
    1.940684f       // [5] sensor_noise_level
};

// ── Constants ─────────────────────────────────────────────────
// ESD lethal gradient constant (fresh water)
// 2 V/ft = 6.56 V/m — Rifkin & Shafer (2008), US Coast Guard
static const float ESD_GRADIENT      = 6.56f;

// Dead zone: readings at or below this voltage are absolute zero
// Chosen based on ZMPT101B noise floor characterization (~5V max)
static const float DEAD_ZONE_V       = 5.0f;

// Hard threshold: readings at or above this are always Dangerous
// Bypasses ML to guarantee immediate detection at the 30V cutoff
static const float DANGEROUS_FLOOR_V = 30.0f;

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

// ── Main prediction function ──────────────────────────────────
//  acVoltage — current reading from ZMPT101B (0-250V)
//
//  Call this once per sensor reading in your main loop.
LigtasResult ligtas_predict(float acVoltage) {

    // Clamp to sensor rated range
    if (acVoltage > 250.0f) acVoltage = 250.0f;
    if (acVoltage < 0.0f)   acVoltage = 0.0f;

    // ── FILTER 1: Dead Zone ───────────────────────────────────
    // Any reading ≤ 5V is treated as absolute 0 (sensor noise floor).
    // Return Safe immediately — no ML needed.
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

    // ── Compute derived features ──────────────────────────────
    float voltage_v          = acVoltage;
    float voltage_squared    = acVoltage * acVoltage;
    float hazard_radius      = acVoltage / ESD_GRADIENT;
    float coverage_area_m2   = 3.14159265f * hazard_radius * hazard_radius;
    float voltage_class      = (float)getVoltageClass(acVoltage);
    float danger_score       = acVoltage / (1.0f + log(coverage_area_m2 + 1.0f));
    float sensor_noise_level = acVoltage * 0.02f;

    // ── FILTER 2: Hard Threshold Override (≥ 30V) ────────────
    // Skip ML entirely — deterministic and immediate.
    int final_prediction;
    if (acVoltage >= DANGEROUS_FLOOR_V) {
        final_prediction = 1;  // always Dangerous at or above 30V
    } else {
        // ── ML Prediction (5V – 29.99V) ──────────────────────
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
        final_prediction = clf.predict(scaled);
    }

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
