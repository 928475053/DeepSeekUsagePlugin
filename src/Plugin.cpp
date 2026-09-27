#include "Plugin.h"

#include "BalanceClient.h"
#include "ConfigStore.h"
#include "OptionsDialog.h"
#include "StringUtils.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <exception>

namespace deepseek
{

DeepSeekPlugin DeepSeekPlugin::instance_;

namespace
{

const wchar_t* CurrencyName(const std::wstring& currency)
{
    if (currency == L"CNY")
        return L"人民币 CNY";
    if (currency == L"USD")
        return L"美元 USD";
    return currency.c_str();
}

std::wstring CurrencySymbol(const std::wstring& currency)
{
    if (currency == L"CNY")
        return L"¥";
    if (currency == L"USD")
        return L"$";
    return currency + L" ";
}

const BalanceInfo* SelectBalance(
    const BalanceResponse& response,
    CurrencyPreference preference)
{
    if (response.balanceInfos.empty())
        return nullptr;

    const auto findCurrency = [&response](const wchar_t* currency) -> const BalanceInfo*
    {
        const auto iterator = std::find_if(
            response.balanceInfos.begin(),
            response.balanceInfos.end(),
            [currency](const BalanceInfo& info) { return info.currency == currency; });
        return iterator == response.balanceInfos.end() ? nullptr : &*iterator;
    };

    switch (preference)
    {
    case CurrencyPreference::CNY:
        return findCurrency(L"CNY");
    case CurrencyPreference::USD:
        return findCurrency(L"USD");
    case CurrencyPreference::Auto:
    default:
        if (const BalanceInfo* cny = findCurrency(L"CNY"))
            return cny;
        return &response.balanceInfos.front();
    }
}

std::wstring NormalizeCurrency(std::wstring currency)
{
    std::transform(
        currency.begin(),
        currency.end(),
        currency.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
    return currency;
}

} // namespace

const wchar_t* DeepSeekBalanceItem::GetItemName() const
{
    return L"DeepSeek 余额";
}

const wchar_t* DeepSeekBalanceItem::GetItemId() const
{
    return L"DeepSeekBalance";
}

const wchar_t* DeepSeekBalanceItem::GetItemLableText() const
{
    return L"DeepSeek";
}

const wchar_t* DeepSeekBalanceItem::GetItemValueText() const
{
    return plugin_.ItemValueText().c_str();
}

const wchar_t* DeepSeekBalanceItem::GetItemValueSampleText() const
{
    return L"¥000.00";
}

DeepSeekPlugin::DeepSeekPlugin()
    : item_(*this)
{
    configPath_ = ConfigStore::MakeConfigPath({});
    UpdateDisplayTextsLocked();
}

DeepSeekPlugin::~DeepSeekPlugin()
{
    StopWorker();
}

DeepSeekPlugin& DeepSeekPlugin::Instance()
{
    return instance_;
}

IPluginItem* DeepSeekPlugin::GetItem(int index)
{
    return index == 0 ? &item_ : nullptr;
}

void DeepSeekPlugin::DataRequired()
{
    EnsureWorker();

    std::lock_guard<std::mutex> lock(mutex_);
    UpdateDisplayTextsLocked();
}

ITMPlugin::OptionReturn DeepSeekPlugin::ShowOptionsDialog(void* hParent)
{
    Settings editedSettings;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        editedSettings = settings_;
    }

    if (!deepseek::ShowOptionsDialog(
        GetPluginInstance(),
        reinterpret_cast<HWND>(hParent),
        editedSettings))
    {
        return OR_OPTION_UNCHANGED;
    }

    bool changed = false;
    std::wstring saveError;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        changed = editedSettings != settings_;
        settings_ = editedSettings;
        configLoaded_ = true;
        state_.configError.clear();
        if (settings_.apiKey.empty())
        {
            state_ = {};
            state_.state = DisplayState::Unconfigured;
        }
        else
        {
            state_.requestInProgress = true;
            state_.state = state_.hasSuccessfulData
                ? DisplayState::Success
                : DisplayState::Loading;
        }
        refreshRequested_ = true;
        UpdateDisplayTextsLocked();
    }
    condition_.notify_all();

    if (!ConfigStore::Save(configPath_, editedSettings, saveError))
    {
        MessageBoxW(
            reinterpret_cast<HWND>(hParent),
            saveError.c_str(),
            L"DeepSeek 余额插件",
            MB_OK | MB_ICONWARNING);
    }

    return changed ? OR_OPTION_CHANGED : OR_OPTION_UNCHANGED;
}

const wchar_t* DeepSeekPlugin::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:
        return L"DeepSeek 余额监控";
    case TMI_DESCRIPTION:
        return L"通过 DeepSeek 官方余额接口实时查询 API 账户余额，支持自定义轮询间隔。";
    case TMI_AUTHOR:
        return L"928475053";
    case TMI_COPYRIGHT:
        return L"Copyright (C) 2026 928475053";
    case TMI_VERSION:
        return L"1.0.0";
    case TMI_URL:
        return L"https://github.com/928475053/DeepSeekUsagePlugin";
    default:
        return L"";
    }
}

void DeepSeekPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (index != EI_CONFIG_DIR)
        return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (configLoaded_)
            return;
    }

    ConfigureFromDirectory(data == nullptr ? std::wstring{} : std::wstring(data));
}

const wchar_t* DeepSeekPlugin::GetTooltipInfo()
{
    return tooltipText_.c_str();
}

int DeepSeekPlugin::GetCommandCount()
{
    return 1;
}

const wchar_t* DeepSeekPlugin::GetCommandName(int command_index)
{
    return command_index == 0 ? L"立即刷新 DeepSeek 余额" : nullptr;
}

void DeepSeekPlugin::OnPluginCommand(int command_index, void* hWnd, void* para)
{
    (void)hWnd;
    (void)para;
    if (command_index == 0)
    {
        EnsureWorker();
        RequestRefresh();
    }
}

void DeepSeekPlugin::OnInitialize(ITrafficMonitor* pApp)
{
    app_ = pApp;
    if (app_ != nullptr)
    {
        const wchar_t* configDirectory = app_->GetPluginConfigDir();
        ConfigureFromDirectory(
            configDirectory == nullptr ? std::wstring{} : std::wstring(configDirectory));
    }
    EnsureWorker();
}

const std::wstring& DeepSeekPlugin::ItemValueText() const
{
    return valueText_;
}

const std::wstring& DeepSeekPlugin::TooltipText() const
{
    return tooltipText_;
}

void DeepSeekPlugin::ConfigureFromDirectory(const std::wstring& configDirectory)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (configLoaded_)
            return;

        configPath_ = ConfigStore::MakeConfigPath(configDirectory);
        LoadSettingsLocked();
        configLoaded_ = true;
        refreshRequested_ = true;
        UpdateDisplayTextsLocked();
    }
    condition_.notify_all();
    EnsureWorker();
}

void DeepSeekPlugin::LoadSettingsLocked()
{
    Settings loaded;
    std::wstring error;
    const ConfigLoadStatus status = ConfigStore::Load(configPath_, loaded, error);

    switch (status)
    {
    case ConfigLoadStatus::Loaded:
        settings_ = std::move(loaded);
        state_.configError.clear();
        break;
    case ConfigLoadStatus::NotFound:
        settings_ = {};
        state_.configError.clear();
        state_.state = DisplayState::Unconfigured;
        break;
    case ConfigLoadStatus::Error:
        settings_ = {};
        state_ = {};
        state_.state = DisplayState::Error;
        state_.lastError = error;
        state_.configError = error;
        break;
    }
}

void DeepSeekPlugin::EnsureWorker()
{
    bool startWorker = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!workerStarted_)
        {
            workerStarted_ = true;
            startWorker = true;
        }
        refreshRequested_ = true;
    }
    condition_.notify_all();

    if (!startWorker)
        return;

    try
    {
        worker_ = std::thread(&DeepSeekPlugin::WorkerMain, this);
    }
    catch (const std::exception& exception)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        workerStarted_ = false;
        state_.state = DisplayState::Error;
        state_.lastError =
            L"无法启动余额查询线程：" + Utf8ToWide(exception.what());
        UpdateDisplayTextsLocked();
    }
}

void DeepSeekPlugin::RequestRefresh()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        refreshRequested_ = true;
        if (!settings_.apiKey.empty())
        {
            state_.requestInProgress = true;
            state_.state = state_.hasSuccessfulData
                ? DisplayState::Success
                : DisplayState::Loading;
        }
        UpdateDisplayTextsLocked();
    }
    condition_.notify_all();
}

void DeepSeekPlugin::StopWorker()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

void DeepSeekPlugin::WorkerMain() noexcept
{
    while (true)
    {
        Settings currentSettings;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopRequested_)
                return;
            currentSettings = settings_;
            refreshRequested_ = false;
        }

        if (currentSettings.apiKey.empty())
        {
            SetUnconfiguredState();
        }
        else
        {
            SetLoadingState();
            try
            {
                BalanceQueryResult result = BalanceClient::Query(currentSettings.apiKey);
                ApplyQueryResult(currentSettings, std::move(result));
            }
            catch (const std::exception& exception)
            {
                BalanceQueryResult result;
                result.errorMessage =
                    L"余额查询发生异常：" + Utf8ToWide(exception.what());
                ApplyQueryResult(currentSettings, std::move(result));
            }
            catch (...)
            {
                BalanceQueryResult result;
                result.errorMessage = L"余额查询发生未知异常";
                ApplyQueryResult(currentSettings, std::move(result));
            }
        }

        std::unique_lock<std::mutex> lock(mutex_);
        const int interval = std::clamp(
            currentSettings.pollIntervalSeconds,
            kMinPollIntervalSeconds,
            kMaxPollIntervalSeconds);
        condition_.wait_for(
            lock,
            std::chrono::seconds(interval),
            [this] { return stopRequested_ || refreshRequested_; });
        if (stopRequested_)
            return;
    }
}

void DeepSeekPlugin::ApplyQueryResult(
    const Settings& currentSettings,
    BalanceQueryResult result)
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_.requestInProgress = false;

    if (result.success)
    {
        const BalanceInfo* selected = SelectBalance(
            result.balance,
            currentSettings.currencyPreference);
        if (selected == nullptr)
        {
            state_.state = state_.hasSuccessfulData
                ? DisplayState::Success
                : DisplayState::Error;
            state_.lastError = L"DeepSeek 响应中没有可用的余额信息";
            return;
        }

        const std::wstring currency = NormalizeCurrency(selected->currency);
        if (currentSettings.currencyPreference != CurrencyPreference::Auto
            && currency != (currentSettings.currencyPreference == CurrencyPreference::CNY
                ? L"CNY"
                : L"USD"))
        {
            state_.state = state_.hasSuccessfulData
                ? DisplayState::Success
                : DisplayState::Error;
            state_.lastError = L"DeepSeek 响应中没有所选币种的余额信息";
            return;
        }

        state_.state = DisplayState::Success;
        state_.hasSuccessfulData = true;
        state_.unavailable = !result.balance.isAvailable;
        state_.currency = currency;
        state_.totalBalance = selected->totalBalance;
        state_.grantedBalance = selected->grantedBalance;
        state_.toppedUpBalance = selected->toppedUpBalance;
        state_.lastUpdated = FormatLocalTimestamp();
        state_.lastError.clear();

        std::wstring value = CurrencySymbol(currency) + selected->totalBalance;
        if (state_.unavailable)
            value.insert(value.begin(), L'!');
        state_.lastValueText = std::move(value);
    }
    else
    {
        state_.state = state_.hasSuccessfulData
            ? DisplayState::Success
            : DisplayState::Error;
        state_.lastError = result.errorMessage.empty()
            ? L"查询 DeepSeek 余额失败"
            : std::move(result.errorMessage);
    }

}

void DeepSeekPlugin::SetUnconfiguredState()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopRequested_)
        return;

    state_.state = DisplayState::Unconfigured;
    state_.requestInProgress = false;
    state_.hasSuccessfulData = false;
    state_.unavailable = false;
    state_.lastValueText.clear();
    state_.currency.clear();
    state_.totalBalance.clear();
    state_.grantedBalance.clear();
    state_.toppedUpBalance.clear();
    state_.lastUpdated.clear();
    state_.lastError.clear();
}

void DeepSeekPlugin::SetLoadingState()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopRequested_)
        return;

    state_.requestInProgress = true;
    state_.state = state_.hasSuccessfulData
        ? DisplayState::Success
        : DisplayState::Loading;
}

void DeepSeekPlugin::UpdateDisplayTextsLocked()
{
    if (state_.state == DisplayState::Unconfigured)
    {
        valueText_ = L"未配置";
        tooltipText_ = state_.configError.empty()
            ? L"DeepSeek 余额：请在插件选项中填写 API Key。"
            : L"DeepSeek 余额配置错误：" + state_.configError;
        return;
    }

    if (state_.state == DisplayState::Loading || state_.requestInProgress)
    {
        if (!state_.hasSuccessfulData)
        {
            valueText_ = L"查询中...";
            tooltipText_ = L"DeepSeek 余额：正在查询...";
            return;
        }
    }

    if (state_.state == DisplayState::Error && !state_.hasSuccessfulData)
    {
        valueText_ = L"查询失败";
        tooltipText_ = L"DeepSeek 余额查询失败";
        if (!state_.lastError.empty())
            tooltipText_ += L"：\r\n" + state_.lastError;
        if (!state_.configError.empty())
            tooltipText_ += L"\r\n配置错误：" + state_.configError;
        return;
    }

    valueText_ = state_.lastValueText;
    if (valueText_.empty())
        valueText_ = L"查询失败";

    if (state_.requestInProgress)
    {
        tooltipText_ = L"DeepSeek 余额：正在刷新，当前显示上次结果。\r\n";
    }
    else
    {
        tooltipText_.clear();
    }

    tooltipText_ += L"DeepSeek 余额\r\n";
    tooltipText_ += L"状态：";
    if (!state_.lastError.empty())
        tooltipText_ += L"查询失败，显示上次结果";
    else if (state_.unavailable)
        tooltipText_ += L"余额不足或账户不可用";
    else
        tooltipText_ += L"正常";
    tooltipText_ += L"\r\n";

    if (!state_.currency.empty())
    {
        tooltipText_ += L"币种：";
        tooltipText_ += CurrencyName(state_.currency);
        tooltipText_ += L"\r\n";
    }
    if (!state_.totalBalance.empty())
    {
        tooltipText_ += L"总余额：";
        tooltipText_ += CurrencySymbol(state_.currency);
        tooltipText_ += state_.totalBalance;
        tooltipText_ += L"\r\n";
    }
    if (!state_.grantedBalance.empty())
    {
        tooltipText_ += L"赠送余额：";
        tooltipText_ += CurrencySymbol(state_.currency);
        tooltipText_ += state_.grantedBalance;
        tooltipText_ += L"\r\n";
    }
    if (!state_.toppedUpBalance.empty())
    {
        tooltipText_ += L"充值余额：";
        tooltipText_ += CurrencySymbol(state_.currency);
        tooltipText_ += state_.toppedUpBalance;
        tooltipText_ += L"\r\n";
    }
    if (!state_.lastUpdated.empty())
    {
        tooltipText_ += L"更新时间：";
        tooltipText_ += state_.lastUpdated;
        tooltipText_ += L"\r\n";
    }

    tooltipText_ += L"轮询间隔：";
    tooltipText_ += std::to_wstring(settings_.pollIntervalSeconds);
    tooltipText_ += L" 秒";

    if (!state_.lastError.empty())
    {
        tooltipText_ += L"\r\n\r\n上次查询错误：";
        tooltipText_ += state_.lastError;
    }

    if (!state_.hasSuccessfulData || !state_.lastError.empty())
    {
        valueText_ += L"?";
    }
}

} // namespace deepseek

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &deepseek::DeepSeekPlugin::Instance();
}
