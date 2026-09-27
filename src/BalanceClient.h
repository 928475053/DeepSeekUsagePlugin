#pragma once

#include "Models.h"

#include <string>

namespace deepseek
{

class BalanceClient
{
public:
    static BalanceQueryResult Query(const std::wstring& apiKey);
};

} // namespace deepseek
