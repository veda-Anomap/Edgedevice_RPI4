# config

This folder contains runtime configuration for SubCameraApp.

## Files

- `AppConfig.h`: compile-time and runtime constants for AI, stream, event, and threshold behavior.
- `edge_device_config.json`: serial/edge bridge configuration for MCU communication.

## Notes

- Keep values in sync with runtime expectations in `src/controller` and `src/stream`.
- When changing thresholds, validate with `testing_app_python` scenarios before deployment.
