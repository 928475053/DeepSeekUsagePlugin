#pragma once

#include <string>
#include <string_view>

#include <Windows.h>

namespace deepseek
{

HMODULE GetPluginModuleHandle();
bool IsValidApiKey(std::wstring_view value);
std::wstring Trim(std::wstring value);
std::wstring Utf8ToWide(std::string_view value);
std::string WideToUtf8(std::wstring_view value);
std::wstring FormatWindowsError(unsigned long errorCode);
std::wstring FormatLocalTimestamp();

} // namespace deepseek
