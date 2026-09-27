#pragma once

#include "Models.h"

#include <string>

namespace deepseek
{

enum class ConfigLoadStatus
{
    Loaded,
    NotFound,
    Error,
};

class ConfigStore
{
public:
    static std::wstring MakeConfigPath(const std::wstring& configDirectory);

    static ConfigLoadStatus Load(
        const std::wstring& filePath,
        Settings& settings,
        std::wstring& errorMessage);

    static bool Save(
        const std::wstring& filePath,
        const Settings& settings,
        std::wstring& errorMessage);
};

} // namespace deepseek
