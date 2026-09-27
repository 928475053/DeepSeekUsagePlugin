#pragma once

#include "Models.h"

#include <Windows.h>

namespace deepseek
{

bool ShowOptionsDialog(
    HINSTANCE instance,
    HWND parent,
    Settings& settings);

HINSTANCE GetPluginInstance();

} // namespace deepseek
