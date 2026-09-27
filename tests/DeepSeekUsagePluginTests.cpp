#include "BalanceClient.h"
#include "ConfigStore.h"
#include "JsonParser.h"
#include "PluginInterface.h"
#include "StringUtils.h"

#include <Windows.h>

#include <iostream>
#include <string>

namespace
{

int g_failures = 0;

void Check(bool condition, const char* message)
{
    if (condition)
        return;

    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

void TestValidBalanceResponse()
{
    const std::string json = R"json(
        {
          "is_available": true,
          "balance_infos": [
            {
              "currency": "CNY",
              "total_balance": "110.00",
              "granted_balance": "10.00",
              "topped_up_balance": "100.00"
            }
          ]
        })json";

    deepseek::BalanceResponse response;
    std::wstring error;
    Check(
        deepseek::ParseBalanceResponse(json, response, error),
        "valid balance response should parse");
    Check(response.isAvailable, "is_available should be true");
    Check(response.balanceInfos.size() == 1, "one balance entry should be parsed");
    if (response.balanceInfos.size() == 1)
    {
        Check(response.balanceInfos[0].currency == L"CNY", "currency should match");
        Check(response.balanceInfos[0].totalBalance == L"110.00", "total should match");
        Check(response.balanceInfos[0].grantedBalance == L"10.00", "granted should match");
        Check(response.balanceInfos[0].toppedUpBalance == L"100.00", "topped-up should match");
    }
}

void TestReorderedResponse()
{
    const std::string json =
        R"json({"unused":{"nested":[1,true,null,"x"]},"balance_infos":[{"topped_up_balance":"2.5","currency":"USD","total_balance":3.75,"granted_balance":"0"}],"is_available":false})json";

    deepseek::BalanceResponse response;
    std::wstring error;
    Check(
        deepseek::ParseBalanceResponse(json, response, error),
        "reordered response should parse");
    Check(!response.isAvailable, "is_available should be false");
    Check(response.balanceInfos.size() == 1, "one balance entry should be parsed");
    if (response.balanceInfos.size() == 1)
    {
        Check(response.balanceInfos[0].currency == L"USD", "USD should be parsed");
        Check(response.balanceInfos[0].totalBalance == L"3.75", "number total should be accepted");
    }
}

void TestApiErrorExtraction()
{
    const std::string json =
        R"json({"error":{"type":"authentication_error","message":"Invalid API key \u4e2d\u6587"}})json";

    std::wstring error;
    Check(
        deepseek::ExtractApiErrorMessage(json, error),
        "API error should be extracted");
    Check(error == L"Invalid API key 中文", "escaped unicode should be decoded");
}

void TestInvalidJson()
{
    deepseek::BalanceResponse response;
    std::wstring error;
    Check(
        !deepseek::ParseBalanceResponse("{\"is_available\":true", response, error),
        "truncated JSON should fail");
    Check(!error.empty(), "invalid JSON should return an error message");
}

void TestConfigRoundTrip()
{
    wchar_t tempDirectory[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, tempDirectory) != 0, "temp directory should be available");

    const std::wstring path =
        std::wstring(tempDirectory)
        + L"DeepSeekUsagePlugin-test-"
        + std::to_wstring(GetCurrentProcessId())
        + L".dat";
    DeleteFileW(path.c_str());

    deepseek::Settings saved;
    saved.apiKey = L"sk-test-123456";
    saved.pollIntervalSeconds = 75;
    saved.currencyPreference = deepseek::CurrencyPreference::USD;

    std::wstring error;
    Check(
        deepseek::ConfigStore::Save(path, saved, error),
        "settings should save");

    deepseek::Settings loaded;
    const deepseek::ConfigLoadStatus status =
        deepseek::ConfigStore::Load(path, loaded, error);
    Check(status == deepseek::ConfigLoadStatus::Loaded, "settings should load");
    Check(loaded == saved, "loaded settings should match saved settings");

    DeleteFileW(path.c_str());
}

std::wstring GetPluginPath()
{
    std::wstring testDirectory(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(
        nullptr,
        testDirectory.data(),
        static_cast<DWORD>(testDirectory.size()));
    if (length == 0 || length >= testDirectory.size())
        return {};
    testDirectory.resize(length);

    const size_t separator = testDirectory.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return {};
    testDirectory.resize(separator);

    return testDirectory
        + L"\\..\\..\\..\\"
#ifdef _WIN64
        + L"x64"
#else
        + L"Win32"
#endif
        + L"\\Release\\plugins\\DeepSeekUsagePlugin.dll";
}

void TestPluginLoad()
{
    const std::wstring pluginPath = GetPluginPath();
    HMODULE module = LoadLibraryW(pluginPath.c_str());
    Check(module != nullptr, "plugin DLL should load");
    if (module == nullptr)
        return;

    using GetInstanceFunction = ITMPlugin* (*)();
    const auto getInstance = reinterpret_cast<GetInstanceFunction>(
        GetProcAddress(module, "TMPluginGetInstance"));
    Check(getInstance != nullptr, "TMPluginGetInstance should be exported");

    if (getInstance != nullptr)
    {
        ITMPlugin* plugin = getInstance();
        Check(plugin != nullptr, "plugin instance should not be null");
        if (plugin != nullptr)
        {
            Check(plugin->GetAPIVersion() == 8, "plugin API version should be 8");
            Check(plugin->GetItem(0) != nullptr, "balance item should exist");
            Check(plugin->GetItem(1) == nullptr, "unknown item index should return null");
            Check(
                std::wstring(plugin->GetInfo(ITMPlugin::TMI_NAME)).find(L"DeepSeek") != std::wstring::npos,
                "plugin name should be exposed");

            plugin->DataRequired();
            IPluginItem* item = plugin->GetItem(0);
            Check(item != nullptr, "balance item should remain available");
            if (item != nullptr)
            {
                Check(
                    item->GetItemValueText() != nullptr && *item->GetItemValueText() != L'\0',
                    "balance item should provide display text");
                Check(
                    std::wstring(item->GetItemId()) == L"DeepSeekBalance",
                    "balance item ID should be stable");
            }
        }
    }

    FreeLibrary(module);
}

void TestInvalidApiKeyAgainstLiveService()
{
    const deepseek::BalanceQueryResult result =
        deepseek::BalanceClient::Query(L"sk-invalid-test-key");
    Check(!result.success, "invalid API key should fail");
    Check(result.httpStatus == 401, "invalid API key should return HTTP 401");
}

} // namespace

int main(int argc, char* argv[])
{
    TestValidBalanceResponse();
    TestReorderedResponse();
    TestApiErrorExtraction();
    TestInvalidJson();
    TestConfigRoundTrip();
    TestPluginLoad();

    if (argc > 1 && std::string(argv[1]) == "--test-network")
        TestInvalidApiKeyAgainstLiveService();

    if (g_failures == 0)
    {
        std::cout << "All tests passed.\n";
        return 0;
    }

    std::cerr << g_failures << " test(s) failed.\n";
    return 1;
}
