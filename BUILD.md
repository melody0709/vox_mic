# Build & Packaging Reference

Developer reference for building, testing, packaging, and signing. This file is
English-only and is the single source for build details.

- User-facing docs: `README.md`
- Architecture, data flow, parameters: `ARCHITECTURE.md`
- Always-loaded rules: `AGENTS.md`

## Build Commands

```cmd
build.bat                           # Incremental CMake build + install (DPDFNet by default)
build.bat --rebuild                 # Clean rebuild
build.bat --clean                   # Clean cmake/run/artifacts/logs (keeps packages)
build.bat --rnnoise-only            # Single-exe payload without DPDFNet assets
build.bat --package                 # Build + Portable (.7z) + MSI
build.bat --rnnoise-only --package  # Build + Portable + MSI (RNNoise-only)
build.bat --package-portable        # Build + Portable only
build.bat --package-msi             # Build + MSI only
build.bat --dpdfnet --package       # Build + package the DPDFNet payload
build.bat --dpdfnet --test-dpdfnet  # Build + run DPDFNet smoke tests
build.bat --require-signing ...     # Sign release (needs VOXMIC_SIGN_* env vars)
```

```cmd
build\run\x64-release\voxmic.exe                 # Launch (tray background)
build\run\x64-release\voxmic.exe --list-devices  # List devices
```

When a build or restart is requested, check for a running `voxmic.exe` first.
If it is running, close that VoxMic process directly before building; do not
wait for the user to exit it manually. Do not terminate unrelated processes.

Before any configure/build work, `build.bat` runs `scripts/check_architecture.ps1`.
A guardrail violation fails the build fast rather than after a full compile.

## Toolchain

- VS2022 C++ x64 (MSVC + CMake + Ninja)
- 7-Zip on PATH (Portable `.7z`)
- .NET SDK + WiX v4 SDK via NuGet (MSI; no machine-level WiX install needed)
- Android: SDK `D:\@APP\android-platform-sdk\android-sdk`, Gradle 8.7, JDK 17

## Slint C++ SDK (prerequisite for the planned UI refactor)

Not wired into `CMakeLists.txt` yet. This records where the SDK lives so the
integration step has nothing to rediscover.

- Slint 1.17.1 (win64 MSVC AMD64) is installed at
  `%USERPROFILE%\.workbuddy\binaries\slint\1.17.1\cpp-sdk`.
- The user-level `CMAKE_PREFIX_PATH` points at that directory, so
  `find_package(Slint)` resolves with no extra flags.
- The official package is an NSIS installer. Extract it with 7-Zip instead of
  running it, which leaves the machine untouched:

  ```cmd
  7z x Slint-cpp-1.17.1-win64-MSVC-AMD64.exe -o<target-dir>
  ```

- Package integrity record:
  `sha256 f5b537da448c1e3d72a24a774e19518ae412b9706b8ef49bdee64b62b878fe56`.
- Link target is `Slint::Slint`. It requires C++20 (this project targets C++23)
  and MSVC gets `/bigobj` added automatically. `.slint` files are registered with
  `slint_target_sources()`.
- Runtime, important: the prebuilt package ships **only** a shared library.
  `slint_cpp.dll` (26.3 MB) has to be deployed next to the executable — the
  existing package has no static library, so static linking is not an option
  short of building Slint from source with Rust.
- `.slint` sources belong in the **repository-root** `ui/` directory. The
  architecture guard only scans that path, and it fails the build as soon as a
  `.slint` file exists without an `AboutSlint` widget, because the royalty-free
  licence tier requires visible disclosure.

Verified end to end with a throwaway CMake project (MSVC 14.44 + Ninja + C++23):
configure, compile, link and run all succeed; omitting `slint_cpp.dll` makes the
executable exit with `0xC0000135` (STATUS_DLL_NOT_FOUND).

## DPDFNet Payload (Git LFS)

The DPDFNet payload is enabled by default and vendored under `third_party/dpdfnet/`;
large files are tracked with Git LFS. After cloning, run `git lfs pull` before
`build.bat`; the preparation script verifies and stages the vendored files into
`build/cmake/x64-release/_deps/dpdfnet`. `build/` is disposable and can be cleaned
without losing the dependency source. A pinned download/cache fallback remains only
for a missing vendor directory.

## DPDFNet Smoke Tests

```cmd
build\cmake\x64-release\dpdfnet_smoke.exe build\run\x64-release build\run\x64-release\models\dpdfnet2_48khz_hr.onnx
build\cmake\x64-release\dpdfnet_fallback_smoke.exe build\run\x64-release build\run\x64-release\models\missing-for-test.onnx
build\cmake\x64-release\dpdfnet_pipeline_switch_smoke.exe build\run\x64-release build\run\x64-release\models\dpdfnet2_48khz_hr.onnx
build\cmake\x64-release\dpdfnet_failure_smoke.exe build\run\x64-release build\run\x64-release\models\dpdfnet2_48khz_hr.onnx
```

## Known Shutdown Limitation

The worker still cannot be forcibly cancelled while it is inside a native
sherpa-onnx `Run()` call. What changed is that this no longer hangs the
application:

- `DpdfnetProcessor::WORKER_STOP_TIMEOUT_MS` (2 s) bounds how long shutdown
  waits for the worker to observe its `std::stop_token`.
- When the budget expires the thread is detached and the session is abandoned:
  its events, denoiser and loaded library are deliberately leaked, because the
  worker may still be using them. `workerAbandoned()` reports this, the
  processor reports itself as not ready, and `prepare()` refuses to reuse the
  session - so DPDFNet stays on RNNoise until the process restarts.
- Regression cover: `dpdfnet_failure_smoke` parks a worker in a 10 s sleep and
  asserts that shutdown still returns inside the budget.

Reclaiming those leaked resources needs a process-isolated adapter, which is
still the only real fix.

## build/ Layout

Whitelist enforced by `scripts/validate_build_layout.ps1`:

```
build/
├─ cmake/x64-release/   CMake/Ninja cache, objects, package staging
├─ run/x64-release/     Sole runnable development payload
├─ packages/            Verified .7z / .msi + .sha256 / .input.sha256 sidecars
├─ artifacts/           Verification / test / diagnostic reports
├─ logs/                Explicit build and test logs
└─ README.txt           Layout description
```

## Packaging & Version Protection

- Output: `build/packages/` — `.7z` / `.msi` plus `.sha256` and `.input.sha256` sidecars.
- Same-version artifacts are protected by input-digest sidecars: changing code without
  bumping `APP_VERSION` and re-running `--package*` fails the packaging step.
- `packaging/windows/ProductIdentity.wxi` holds frozen permanent GUIDs (UpgradeCode);
  editing them breaks the upgrade chain.
- Upgrade contract and signing rules: `packaging/windows/UPGRADE_CONTRACT.md`.
- Version bump checklist (must be followed item by item): `AGENTS.md`.

## Android App

```powershell
cd android_app; .\gradlew.bat assembleDebug --no-daemon --console=plain    # Debug
cd android_app; .\gradlew.bat assembleRelease --no-daemon --console=plain  # Release
```

Output: `VoxMic_Source-v<versionName>.apk` (auto-named by `build.gradle`, no manual rename).

Release requires `keystore.properties` + `voxmic.keystore` (both gitignored; a fresh
clone must generate them):

```powershell
& "C:\Program Files\Java\jdk-17\bin\keytool.exe" -genkey -v `
  -keystore android_app/voxmic.keystore -alias voxmic `
  -keyalg RSA -keysize 2048 -validity 10000 `
  -storepass voxmic123 -keypass voxmic123 `
  -dname "CN=VoxMic, OU=Dev, O=VoxMic, L=N/A, ST=N/A, C=CN"
```

`android_app/keystore.properties`:

```
storePassword=voxmic123
keyPassword=voxmic123
keyAlias=voxmic
storeFile=../voxmic.keystore
```

## Runtime Usage

Select **CABLE Output** as the microphone in Windows apps. Tray icon: left-click =
settings window, right-click = menu (Demand Mode / Always Hot / Exit), close window = hide.
