# src

Main C++ application source for SubCameraApp.

## Module Layout

- `ai`: pose inference and fall detection logic.
- `buffer`: pre/post-event frame buffering and event queueing.
- `controller`: orchestration and lifecycle control.
- `edge_device`: UART bridge and STM32 communication logic.
- `imageprocessing`: enhancement and preprocessing pipeline.
- `network`: TCP/UDP message and command interfaces.
- `protocol`: packet framing and type definitions.
- `rendering`: frame drawing and overlay utilities.
- `stream`: capture, pipeline, and streaming control.
- `system`: process/resource guard and monitoring.
- `transmitter`: chunked event media transmission.
- `util`: shared helpers, logging, and performance tools.

## Build Notes

- This folder is intended to be built from the repository root using `CMakeLists.txt`.
- Generated outputs must stay in `build/` and should not be committed.
