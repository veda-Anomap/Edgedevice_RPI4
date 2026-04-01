# tester

C++ test harness for SubCameraApp integration checks.

## Purpose

- Validate pipeline behavior without full production startup.
- Run targeted test modes for stream, AI, and event handling regressions.

## Key Files

- `tester_main.cpp`: entry point for tester executable.
- `TesterRegistry.h`: test mode registration.
- `TesterModes.*`: individual test mode implementations.
- `TesterApp.*`: test runner wrapper.

## Usage

Build with repository-level tester make target and run in an isolated test environment.
