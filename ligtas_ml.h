#pragma once
#include "ligtas_model.h"

// ============================================================
//  LIGTAS ML Wrapper
//  Auto-scaler values from ligtas_scaler.pkl (Cell 18 output)
// ============================================================

const float SCALER_MEAN[2]  = {498.320550f, 1041243.116005f};
const float SCALER_SCALE[2] = {288.296041f, 935218.639524f};

// Result struct returned by ligtas_predict()
struct LigtasResult {
    int   classIndex;       // 0 = Safe, 1 = Dangerous
    const char* status;     // "Safe" or "Dangerous"
    String coverageAreaStr; // e.g. "452.4 m2" or "452.4 m2 [!]"
};

// Call this with the raw AC voltage reading from your sensor
LigtasResult ligtas_predict(float acVoltage) {
    // Feature vector: [acVoltage, derived power estimate]
    // Feature 0 = acVoltage, Feature 1 = acVoltage^2 * 2 (proxy for power)
    float raw[2] = {
        acVoltage,
        acVoltage * acVoltage * 2.0f
    };

    // Apply StandardScaler: z = (x - mean) / scale
    float scaled[2] = {
        (raw[0] - SCALER_MEAN[0]) / SCALER_SCALE[0],
        (raw[1] - SCALER_MEAN[1]) / SCALER_SCALE[1]
    };

    // Run Random Forest inference
    Eloquent::ML::Port::RandomForest clf;
    int prediction = clf.predict(scaled);

    // Build coverage area string (simple linear estimate from voltage)
    float coverageArea = acVoltage * 0.9f;  // adjust multiplier if needed
    String areaStr = String(coverageArea, 1) + " m2";
    if (prediction == 1) areaStr += " [!]";

    LigtasResult result;
    result.classIndex      = prediction;
    result.status          = (prediction == 0) ? "Safe" : "Dangerous";
    result.coverageAreaStr = areaStr;

    return result;
}