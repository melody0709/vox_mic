# Vendored DPDFNet runtime

This directory contains the pinned Windows x64 DPDFNet payload used by the
optional VoxMic backend:

- `model/dpdfnet2_48khz_hr.onnx`
- `runtime/sherpa-onnx-c-api.dll`
- `runtime/onnxruntime.dll`
- `runtime/onnxruntime_providers_shared.dll`
- `include/sherpa-onnx/c-api/c-api.h`

The model and DLLs are managed with Git LFS. Run `git lfs pull` after cloning
before using `build.bat --dpdfnet`. `metadata.json` records the pinned
SHA-256 values, including the source archive hash. The dependency preparation script verifies every file before
copying it into the disposable `build/cmake/x64-release/_deps/dpdfnet` stage.
Do not treat anything under `build/` as the dependency source; deleting that
directory is safe because this directory is the source of truth.

License notices are in `../DPDFNET_THIRD_PARTY_NOTICES.txt`.
