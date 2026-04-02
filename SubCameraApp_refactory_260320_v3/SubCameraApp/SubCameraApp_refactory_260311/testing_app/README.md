# testing_app

C++ utility app for parameter-driven testing and metrics.

## Structure

- `config/testing_params.json`: parameter definitions used by the test app.
- `src/core/ConfigLoader.*`: config parsing and typed access.
- `src/core/MetricsCalculator.*`: test metric computation.
- `src/core/ITestModule.h`: extension point for custom test modules.

## Intent

Use this app for controlled reproducible experiments before changing production thresholds.
