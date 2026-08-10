#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "config.h"

HWND createSettingsWindow(HINSTANCE hInstance, Config* pConfig);
