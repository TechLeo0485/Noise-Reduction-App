# Models

The current build needs no model files. RNNoise's weights are compiled into the binary from
the RNNoise source archive.

This directory is reserved for the optional Advanced AI engine (Phase 3). Requirements a model
must meet before it is added:

1. A license that allows redistribution in a free desktop application (record it in
   `THIRD_PARTY_LICENSES.md`).
2. ONNX format, opset supported by the pinned ONNX Runtime version.
3. Streaming operation with a hop of 10 ms or less at 48 kHz (or 16 kHz with documented resampling).
4. Measured P99 inference time below 5 ms on the CPU backend of a mid-range machine.
   Larger models are only offered on backends where they meet the deadline.
5. Recorded algorithmic latency (look-ahead) and quality results (PESQ/STOI) in
   `docs/latency-benchmarks.md`.

Models are packaged with the installer for offline use. They are never downloaded at runtime.
