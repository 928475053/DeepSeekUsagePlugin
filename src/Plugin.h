#pragma once

#include "Models.h"
#include "PluginInterface.h"

#include <Windows.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace deepseek
{

class DeepSeekBalanceItem : public IPluginItem
{
public:
    explicit DeepSeekBalanceItem(class DeepSeekPlugin& plugin)
        : plugin_(plugin)
    {
    }

    const wchar_t* GetItemName() const override;
    const wchar_t* GetItemId() const override;
    const wchar_t* GetItemLableText() const override;
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override;

private:
    DeepSeekPlugin& plugin_;
};

class DeepSeekPlugin : public ITMPlugin
{
public:
    DeepSeekPlugin();
    ~DeepSeekPlugin();

    static DeepSeekPlugin& Instance();

    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    OptionReturn ShowOptionsDialog(void* hParent) override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    const wchar_t* GetTooltipInfo() override;
    int GetCommandCount() override;
    const wchar_t* GetCommandName(int command_index) override;
    void OnPluginCommand(int command_index, void* hWnd, void* para) override;
    void OnInitialize(ITrafficMonitor* pApp) override;

    const std::wstring& ItemValueText() const;
    const std::wstring& TooltipText() const;

private:
    enum class DisplayState
    {
        Unconfigured,
        Loading,
        Success,
        Error,
    };

    struct RuntimeState
    {
        DisplayState state{ DisplayState::Unconfigured };
        bool hasSuccessfulData{};
        bool requestInProgress{};
        bool unavailable{};
        std::wstring lastValueText;
        std::wstring currency;
        std::wstring totalBalance;
        std::wstring grantedBalance;
        std::wstring toppedUpBalance;
        std::wstring lastUpdated;
        std::wstring lastError;
        std::wstring configError;
    };

    void ConfigureFromDirectory(const std::wstring& configDirectory);
    void LoadSettingsLocked();
    void EnsureWorker();
    void RequestRefresh();
    void StopWorker();
    void WorkerMain() noexcept;
    void ApplyQueryResult(const Settings& settings, BalanceQueryResult result);
    void SetUnconfiguredState();
    void SetLoadingState();
    void UpdateDisplayTextsLocked();

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    Settings settings_;
    RuntimeState state_;
    std::wstring configPath_;
    bool configLoaded_{};
    bool workerStarted_{};
    bool stopRequested_{};
    bool refreshRequested_{ true };
    std::thread worker_;
    DeepSeekBalanceItem item_;
    std::wstring valueText_;
    std::wstring tooltipText_;
    ITrafficMonitor* app_{};

    static DeepSeekPlugin instance_;
};

} // namespace deepseek
