#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "config.h"

bool showSettingsDialog(HINSTANCE hInstance, HWND hParent, Config& config);
