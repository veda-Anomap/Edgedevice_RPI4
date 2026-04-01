# Revision Notes: SubCameraApp Testing Utility Refinement

This document outlines the architectural changes made to align the testing utility with the production system (C++ baseline) and the functional report.

## 1. Preprocessing Algorithm Standardization
The **Enhancer Pipeline** has been updated to support the exact 9-grid algorithms used in the C++ comparison tools.

| Index | Algorithm | Key Technology |
|---|---|---|
| 1 | **RETINEX** | Multi-scale Guided Filter + CLAHE (HSV) |
| 2 | **YUV ADV** | YUV Gamma LUT + Median Blur + CLAHE |
| 3 | **WWGIF** | Gradient-Aware Weighted Guided Filter (IEEE 2024) |
| 4 | **TONEMAP** | Max-Channel guided filtering + Adaptive Gamma |
| 5 | **DETAIL** | Multi-scale DoG (Gaussian Difference) boost |
| 6 | **HYBRID** | Sequential ToneMapping + Detail Boosting |
| 7 | **BALANCED_V5** | Lab Sigmoid Mapping + Log Saturation Correct |
| 8 | **CleanSharp** | YUV Edge-preserving detail boost |
| 9 | **RAW ORIGIN** | Bypass (Unprocessed) |

## 2. Dynamic UI & Efficiency
- **Video-Only AF Filters**: Auto Focus sharpening filters (Guided, GradRatio, ELP) are now restricted to the **Live Tuning Tab (Tab 2)** to prevent confusion during static image analysis.
- **Context-Aware Parameters**: The parameter tuning panel in Tab 2 now dynamically hides/shows slider groups relevant to the selected mode.
- **Standardized Config**: A new `testing_params.json` is provided in the `config/` directory, pre-populated with optimal thresholds from the `SubCameraApp_기능보고서_260326`.

## 3. Implementation Details
- **SOLID Compliance**: The strategy pattern was strictly followed for `WWGIFEnhancer` and `UltimateBalancedV5`.
- **Latency Tracking**: Real-time performance profiling is active for all new enhancers, showing per-frame latency in the status overlay.
