#pragma once

#include <string>
#include <vector>

namespace deepseek
{

constexpr int kMinPollIntervalSeconds = 10;
constexpr int kMaxPollIntervalSeconds = 86400;
constexpr int kDefaultPollIntervalSeconds = 60;

enum class CurrencyPreference : int
{
    Auto = 0,
    CNY = 1,
    USD = 2,
};

struct Settings
{
    std::wstring apiKey;
    int pollIntervalSeconds{ kDefaultPollIntervalSeconds };
    CurrencyPreference currencyPreference{ CurrencyPreference::Auto };
};

inline bool operator==(const Settings& left, const Settings& right)
{
    return left.apiKey == right.apiKey
        && left.pollIntervalSeconds == right.pollIntervalSeconds
        && left.currencyPreference == right.currencyPreference;
}

inline bool operator!=(const Settings& left, const Settings& right)
{
    return !(left == right);
}

struct BalanceInfo
{
    std::wstring currency;
    std::wstring totalBalance;
    std::wstring grantedBalance;
    std::wstring toppedUpBalance;
};

struct BalanceResponse
{
    bool isAvailable{};
    std::vector<BalanceInfo> balanceInfos;
};

struct BalanceQueryResult
{
    bool success{};
    int httpStatus{};
    BalanceResponse balance;
    std::wstring errorMessage;
};

} // namespace deepseek
