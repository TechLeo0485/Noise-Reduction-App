# Models

| Files | Used by | Source |
|---|---|---|
| `voxlock/enroll.onnx`, `voxlock/chunk_int8_full.onnx` | Strict voice isolation | [VoxLock](https://huggingface.co/arbazsiddiqui/voxlock) revision `a50f4e1d`, Apache-2.0 |

RNNoise's weights are compiled into the binary from the RNNoise source archive.

The VoxLock files are not stored in git. CMake downloads them once at configure time (SHA-256
pinned, see `cmake/Dependencies.cmake`) or copies them from `VOXLOCK_MODEL_DIR`, and the build
places them in `bin/models/voxlock/`; the installer ships them. They are never downloaded at
runtime. VoxLock runs at 16 kHz with an 8 ms hop and 24 ms of algorithmic look-ahead; see
`docs/audio-pipeline.md` (Strict voice isolation) and `docs/latency-benchmarks.md` §6.

Requirements for any further model:

1. A license that allows redistribution in a free desktop application (record it in
   `THIRD_PARTY_LICENSES.md`).
2. ONNX format, opset supported by the pinned ONNX Runtime version.
3. Streaming operation with a hop of 10 ms or less at 48 kHz (or 16 kHz with documented resampling).
4. Measured inference time that keeps up in real time on the CPU backend of a mid-range machine.
5. Recorded algorithmic latency (look-ahead) and quality results (PESQ/STOI) in
   `docs/latency-benchmarks.md`.

Models are packaged with the installer for offline use. They are never downloaded at runtime.
