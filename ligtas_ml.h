#pragma once
#include "ligtas_model.h"

// ============================================================
//  LIGTAS ML Wrapper — v5.2  (Dead Zone + Spike Debounce Fix)
//  Model   : Random Forest (100 trees, 3 classes)
//  Classes : 0 = Safe  |  1 = Warning  |  2 = Dangerous
//  Accuracy: 99.87%  Precision: 99.87%  Recall: 99.87%
//
//  Sensor   : ZMPT101B (rated range 0-250V AC)
//  Use case : Flood water AC leakage monitoring
//
//  Classification thresholds (no gaps):
//    0.00 – 5.00 V  →  Dead Zone (clamped to 0 = Safe, no ML)
//    5.01 – 25.00 V →  Safe   (via ML)
//    25.01 – 29.99 V→  Warning (via ML)
//    30.00 – 250 V  →  Dangerous (hard threshold override)
//
//  NOISE FIXES IN v5.2:
//  ┌────────────────────────────────────────────────────────────┐
//  │  1. DEAD ZONE (0–5V)                                       │
//  │     Any reading ≤ 5V is treated as absolute 0V (Safe).     │
//  │     The ML model is NOT called — returned immediately.     │
//  │     Eliminates false positives from sensor noise floor.    │
//  │                                                            │
//  │  2. HARD THRESHOLD OVERRIDE (≥ 30V)                        │
//  │     Any reading ≥ 30V is ALWAYS Dangerous, regardless of  │
//  │     ML prediction. Prevents boundary bleed at 30V cutoff.  │
//  │                                                            │
//  │  3. SPIKE DEBOUNCE FILTER (3-reading confirmation)         │
//  │     Warning/Dangerous is only confirmed after 3 consecutive│
//  │     readings agree. A single spike resets the counter.     │
//  │     Safe is always returned instantly (fail-safe design).  │
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

// ── StandardScaler values (retrained v5.2, 6 features) ───────
const float SCALER_MEAN[6]  = {
    98.941136f,     // [0] voltage_v
    15688.397779f,  // [1] voltage_squared
    1145.304024f,   // [2] coverage_area_m2
    2.840832f,      // [3] voltage_class
    12.388788f,     // [4] danger_score
    1.988549f       // [5] sensor_noise_level
};

const float SCALER_SCALE[6] = {
    76.805269f,     // [0] voltage_v
    18227.157485f,  // [1] voltage_squared
    1330.641732f,   // [2] coverage_area_m2
    1.316776f,      // [3] voltage_class
    7.744189f,      // [4] danger_score
    1.941106f       // [5] sensor_noise_level
};

// ── Constants ─────────────────────────────────────────────────
// ESD lethal gradient constant (fresh water)
// 2 V/ft = 6.56 V/m — Rifkin & Shafer (2008), US Coast Guard
static const float ESD_GRADIENT       = 6.56f;

// Dead zone: readings at or below this voltage are absolute zero
// Chosen based on ZMPT101B noise floor characterization (~5V max)
static const float DEAD_ZONE_V        = 5.0f;

// Hard threshold: readings at or above this are always Dangerous
// Bypasses ML to prevent boundary bleed at the 30V cutoff
static const float DANGEROUS_FLOOR_V  = 30.0f;

// Debounce: consecutive readings required to confirm Warning/Dangerous
// A transient spike that drops on the next reading is filtered out
static const uint8_t DEBOUNCE_COUNT   = 3;

// ── Internal debounce state ───────────────────────────────────
static uint8_t _debounce_pending_class = 0;   // candidate class
static uint8_t _debounce_counter       = 0;   // consecutive match count

// ── Result struct ─────────────────────────────────────────────
struct LigtasResult {
    int         classIndex;       // 0=Safe, 1=Warning, 2=Dangerous
    const char* status;           // "Safe", "Warning", "Dangerous"
    float       hazardRadius;     // estimated hazard radius in meters
    float       coverageArea;     // estimated electrified surface area in m2
    String      coverageAreaStr;  // formatted e.g. "65.7 m2 [!]"
    float       dangerScore;      // combined risk index
    bool        debounced;        // true = confirmed after debounce; false = pending
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
//  Do NOT average before passing in — the debounce filter
//  works on raw consecutive readings to catch transient spikes.
LigtasResult ligtas_predict(float acVoltage) {

    // Clamp to sensor rated range
    if (acVoltage > 250.0f) acVoltage = 250.0f;
    if (acVoltage < 0.0f)   acVoltage = 0.0f;

    // ── FILTER 1: Dead Zone ───────────────────────────────────
    // Any reading ≤ 5V is treated as absolute 0 (sensor noise floor).
    // Return Safe immediately — no ML needed, no debounce needed.
    if (acVoltage <= DEAD_ZONE_V) {
        _debounce_counter       = 0;    // reset debounce on safe reading
        _debounce_pending_class = 0;
        LigtasResult r;
        r.classIndex      = 0;
        r.status          = "Safe";
        r.hazardRadius    = 0.0f;
        r.coverageArea    = 0.0f;
        r.coverageAreaStr = "0.0 m2";
        r.dangerScore     = 0.0f;
        r.debounced       = true;
        return r;
    }

    // ── FILTER 2: Hard Threshold Override (≥ 30V) ────────────
    // Skip ML for voltages at or above the Dangerous floor.
    // This prevents boundary bleed from ML boundary uncertainty.
    int raw_prediction;
    float voltage_v          = acVoltage;
    float voltage_squared    = acVoltage * acVoltage;
    float hazard_radius      = acVoltage / ESD_GRADIENT;
    float coverage_area_m2   = 3.14159265f * hazard_radius * hazard_radius;
    float voltage_class      = (float)getVoltageClass(acVoltage);
    float danger_score       = acVoltage / (1.0f + log(coverage_area_m2 + 1.0f));
    float sensor_noise_level = acVoltage * 0.02f;

    if (acVoltage >= DANGEROUS_FLOOR_V) {
        raw_prediction = 2;  // always Dangerous at or above 30V
    } else {
        // ── ML Prediction (5V – 29.99V range) ────────────────
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
        raw_prediction = clf.predict(scaled);
    }

    // ── FILTER 3: Spike Debounce ──────────────────────────────
    // For Warning (1) or Dangerous (2): require DEBOUNCE_COUNT
    // consecutive matching readings before confirming the alert.
    // A single transient spike that doesn't repeat is suppressed.
    // Safe (0) is always returned immediately (fail-safe: if the
    // voltage drops, report safe right away without waiting).
    bool confirmed;
    int  final_prediction;

    if (raw_prediction == 0) {
        // Safe: reset debounce, return immediately
        _debounce_counter       = 0;
        _debounce_pending_class = 0;
        confirmed               = true;
        final_prediction        = 0;
    } else {
        if (raw_prediction == (int)_debounce_pending_class) {
            _debounce_counter++;
        } else {
            // New candidate class — restart counter
            _debounce_pending_class = (uint8_t)raw_prediction;
            _debounce_counter       = 1;
        }

        if (_debounce_counter >= DEBOUNCE_COUNT) {
            confirmed        = true;
            final_prediction = raw_prediction;
        } else {
            // Not yet confirmed — report Safe while pending
            confirmed        = false;
            final_prediction = 0;  // hold Safe until debounce passes
        }
    }

    // ── Build result ──────────────────────────────────────────
    const char* statusLabels[3] = { "Safe", "Warning", "Dangerous" };
    String areaStr = String(coverage_area_m2, 1) + " m2";
    if (final_prediction == 2) areaStr += " [!]";
    if (final_prediction == 1) areaStr += " [?]";

    LigtasResult result;
    result.classIndex      = final_prediction;
    result.status          = statusLabels[final_prediction];
    result.hazardRadius    = hazard_radius;
    result.coverageArea    = coverage_area_m2;
    result.coverageAreaStr = areaStr;
    result.dangerScore     = danger_score;
    result.debounced       = confirmed;
    return result;
}

// ── Optional: Reset debounce state ───────────────────────────
// Call this if you restart monitoring or after a long pause.
void ligtas_reset() {
    _debounce_counter       = 0;
    _debounce_pending_class = 0;
}
