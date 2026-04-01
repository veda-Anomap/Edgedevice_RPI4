# SubCameraApp_refactory_260311

Code snapshot for the SubCameraApp edge pipeline.

## Main components
- `src/`: C++ runtime modules (stream, AI, networking, edge bridge).
- `config/`: runtime and threshold configuration.
- `tester/`: C++ tester entry points and mode registry.
- `testing_app/`: C++ utility test app.
- `testing_app_python*`: Python GUI and analysis utilities.

## Repository policy
- Commit source code and documentation only.
- Exclude build outputs, media artifacts, logs, and model binaries.
