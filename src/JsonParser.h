#pragma once

#include "Models.h"

#include <string>

namespace deepseek
{

bool ParseBalanceResponse(
    const std::string& utf8Json,
    BalanceResponse& response,
    std::wstring& errorMessage);

bool ExtractApiErrorMessage(
    const std::string& utf8Json,
    std::wstring& errorMessage);

} // namespace deepseek
