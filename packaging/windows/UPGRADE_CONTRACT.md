# VoxMic MSI upgrade contract

The first public VoxMic MSI establishes a single `stable|x64|perMachine`
product line. Its permanent identity is stored in `ProductIdentity.wxi`.
`UpgradeCode`, the ProductCode UUID-v5 namespace, and the Component UUID-v5
namespace must never be regenerated.

Every public MSI uses `APP_VERSION` (`x.y.z`) from `src/version.h` as its
three-part product version. Any change to payload, installer authoring, or
signing inputs requires a new three-part public version. Same-version
artifacts are never overwritten.

The package is per-machine x64 and defaults to `Program Files\VoxMic`. The
installer UI permits a different directory. The selected directory is recorded
in `HKLM\Software\VoxMic\InstallFolder`; AppSearch restores it before
`RemoveExistingProducts`, so a Major Upgrade keeps the user's selection.

`MajorUpgrade` runs after `InstallInitialize`, rejects downgrades, and does not
allow same-version upgrades. MSI owns only its generated runtime-file
components, install-folder registry value, and Start Menu shortcut. It never
owns or removes `%LOCALAPPDATA%\VoxMic` configuration or logs. Windows startup
is an HKCU application setting and is not created by MSI; users opt in via the
settings dialog (see `src/startup_registration.cpp`).

Before publishing an upgrade, validate first install (including a custom
folder), N-1-to-N upgrade, repair, uninstall, rollback/files-in-use behavior,
and preservation of `%LOCALAPPDATA%\VoxMic` in an isolated Windows VM.

## Release signing

Unsigned local artifacts are explicitly named `-unsigned`. A release build
uses `build.bat --package --require-signing` and requires:

- `VOXMIC_SIGN_CERT_SHA1`: the certificate thumbprint available to signtool;
- `VOXMIC_SIGN_TIMESTAMP_URL`: the RFC 3161 timestamp endpoint; and
- optionally `VOXMIC_SIGNTOOL`: an explicit `signtool.exe` path.

The packaging script Authenticode-signs and verifies `voxmic.exe` before it
is added to either payload, regenerates its runtime hash manifest, and signs
and verifies the MSI. A `.7z` container has no Authenticode format; its signed
executable and published SHA-256 sidecar provide the release integrity checks.
