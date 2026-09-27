#include "StringUtils.h"

#include <Windows.h>

#include <algorithm>
#include <cwctype>

namespace deepseek
{

HMODULE GetPluginModuleHandle()
{
    HMODULE module = nullptr;
    static const int moduleAnchor = 0;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&moduleAnchor),
        &module);
    return module;
}

bool IsValidApiKey(std::wstring_view value)
{
    return std::all_of(
        value.begin(),
        value.end(),
        [](wchar_t ch) { return ch >= 0x21 && ch <= 0x7E; });
}

std::wstring Trim(std::wstring value)
{
    const auto isSpace = [](wchar_t ch) { return std::iswspace(ch) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
    return value;
}

std::wstring Utf8ToWide(std::string_view value)
{
    if (value.empty())
        return {};

    const int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (length <= 0)
        return {};

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        length);
    return result;
}

std::string WideToUtf8(std::wstring_view value)
{
    if (value.empty())
        return {};

    const int length = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (length <= 0)
        return {};

    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        length,
        nullptr,
        nullptr);
    return result;
}

std::wstring FormatWindowsError(unsigned long errorCode)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        0,
        reinterpret_cast<wchar_t*>(&buffer),
        0,
        nullptr);

    std::wstring message;
    if (length != 0 && buffer != nullptr)
        message.assign(buffer, length);
    else
        message = L"Win32 错误 " + std::to_wstring(errorCode);

    if (buffer != nullptr)
        LocalFree(buffer);

    return Trim(message);
}

std::wstring FormatLocalTimestamp()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);

    wchar_t buffer[32]{};
    swprintf_s(
        buffer,
        L"%04u-%02u-%02u %02u:%02u:%02u",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond);
    return buffer;
}

} // namespace deepseek
