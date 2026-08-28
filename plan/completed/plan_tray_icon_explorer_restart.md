# Restore tray icon after Explorer restart

## Goal

Re-register the VoxMic notification-area icon when Windows Explorer recreates
the taskbar, without resetting the current icon/tooltip or tray menu state.

## Completed

- Registered the shell's `TaskbarCreated` broadcast message in `TrayIcon`.
- Routed window messages through `TrayIcon::handleWindowMessage` and issue a
  fresh `NIM_ADD` using the existing `NOTIFYICONDATA` state after Explorer
  restarts.
- Verified the Release build and existing CTest suite.

