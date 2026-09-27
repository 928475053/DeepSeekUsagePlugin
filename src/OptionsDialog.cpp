#include "OptionsDialog.h"

#include "StringUtils.h"
#include "resource.h"

#include <CommCtrl.h>

#include <algorithm>
#include <string>

namespace deepseek
{
namespace
{

struct OptionsDialogContext
{
    Settings settings;
};

std::wstring GetDialogText(HWND dialog, int controlId)
{
    HWND control = GetDlgItem(dialog, controlId);
    if (control == nullptr)
        return {};

    const int length = GetWindowTextLengthW(control);
    if (length <= 0)
        return {};

    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void SetPasswordVisibility(HWND dialog, bool visible)
{
    HWND edit = GetDlgItem(dialog, IDC_API_KEY_EDIT);
    if (edit == nullptr)
        return;

    const wchar_t passwordCharacter = visible
        ? L'\0'
        : static_cast<wchar_t>(0x25CF);
    SendMessageW(
        edit,
        EM_SETPASSWORDCHAR,
        static_cast<WPARAM>(passwordCharacter),
        0);
    InvalidateRect(edit, nullptr, TRUE);
}

void InitializeControls(HWND dialog, const OptionsDialogContext& context)
{
    SetDlgItemTextW(dialog, IDC_API_KEY_EDIT, context.settings.apiKey.c_str());
    SetDlgItemInt(
        dialog,
        IDC_INTERVAL_EDIT,
        static_cast<UINT>(context.settings.pollIntervalSeconds),
        FALSE);

    SendDlgItemMessageW(dialog, IDC_API_KEY_EDIT, EM_SETLIMITTEXT, 8192, 0);
    SendDlgItemMessageW(dialog, IDC_INTERVAL_EDIT, EM_SETLIMITTEXT, 5, 0);
    SendDlgItemMessageW(
        dialog,
        IDC_INTERVAL_SPIN,
        UDM_SETRANGE32,
        static_cast<WPARAM>(kMinPollIntervalSeconds),
        static_cast<LPARAM>(kMaxPollIntervalSeconds));

    HWND currencyCombo = GetDlgItem(dialog, IDC_CURRENCY_COMBO);
    SendMessageW(currencyCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动（优先 CNY）"));
    SendMessageW(currencyCombo, CB_SETITEMDATA, 0, static_cast<LPARAM>(CurrencyPreference::Auto));
    SendMessageW(currencyCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"人民币 CNY"));
    SendMessageW(currencyCombo, CB_SETITEMDATA, 1, static_cast<LPARAM>(CurrencyPreference::CNY));
    SendMessageW(currencyCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"美元 USD"));
    SendMessageW(currencyCombo, CB_SETITEMDATA, 2, static_cast<LPARAM>(CurrencyPreference::USD));

    const int selectedIndex = std::clamp(
        static_cast<int>(context.settings.currencyPreference),
        0,
        2);
    SendMessageW(currencyCombo, CB_SETCURSEL, selectedIndex, 0);
}

INT_PTR CALLBACK OptionsDialogProc(
    HWND dialog,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    auto* context = reinterpret_cast<OptionsDialogContext*>(
        GetWindowLongPtrW(dialog, DWLP_USER));

    switch (message)
    {
    case WM_INITDIALOG:
    {
        context = reinterpret_cast<OptionsDialogContext*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        InitializeControls(dialog, *context);
        SetPasswordVisibility(dialog, false);
        SetFocus(GetDlgItem(dialog, IDC_API_KEY_EDIT));
        return FALSE;
    }
    case WM_COMMAND:
        if (context == nullptr)
            return FALSE;

        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            const std::wstring apiKey = Trim(GetDialogText(dialog, IDC_API_KEY_EDIT));

            BOOL intervalValid = FALSE;
            const UINT interval = GetDlgItemInt(
                dialog,
                IDC_INTERVAL_EDIT,
                &intervalValid,
                FALSE);
            if (!intervalValid
                || interval < static_cast<UINT>(kMinPollIntervalSeconds)
                || interval > static_cast<UINT>(kMaxPollIntervalSeconds))
            {
                MessageBoxW(
                    dialog,
                    L"轮询间隔必须是 10 到 86400 之间的整数秒。",
                    L"设置无效",
                    MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(dialog, IDC_INTERVAL_EDIT));
                return TRUE;
            }
            if (!apiKey.empty() && !IsValidApiKey(apiKey))
            {
                MessageBoxW(
                    dialog,
                    L"API Key 只能包含可见的 ASCII 字符，请检查是否包含空格、换行或中文。",
                    L"设置无效",
                    MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(dialog, IDC_API_KEY_EDIT));
                return TRUE;
            }

            const int currencyIndex = static_cast<int>(SendDlgItemMessageW(
                dialog,
                IDC_CURRENCY_COMBO,
                CB_GETCURSEL,
                0,
                0));
            if (currencyIndex == CB_ERR)
            {
                MessageBoxW(
                    dialog,
                    L"请选择币种。",
                    L"设置无效",
                    MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            const LRESULT currencyData = SendDlgItemMessageW(
                dialog,
                IDC_CURRENCY_COMBO,
                CB_GETITEMDATA,
                static_cast<WPARAM>(currencyIndex),
                0);
            if (currencyData == CB_ERR)
                return TRUE;

            context->settings.apiKey = apiKey;
            context->settings.pollIntervalSeconds = static_cast<int>(interval);
            context->settings.currencyPreference =
                static_cast<CurrencyPreference>(currencyData);
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        case IDC_SHOW_KEY_CHECK:
            SetPasswordVisibility(
                dialog,
                IsDlgButtonChecked(dialog, IDC_SHOW_KEY_CHECK) == BST_CHECKED);
            return TRUE;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

} // namespace

HINSTANCE GetPluginInstance()
{
    return reinterpret_cast<HINSTANCE>(GetPluginModuleHandle());
}

bool ShowOptionsDialog(
    HINSTANCE instance,
    HWND parent,
    Settings& settings)
{
    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&commonControls);

    OptionsDialogContext context{ settings };
    const INT_PTR result = DialogBoxParamW(
        instance,
        MAKEINTRESOURCEW(IDD_OPTIONS_DIALOG),
        parent,
        OptionsDialogProc,
        reinterpret_cast<LPARAM>(&context));

    if (result != IDOK)
        return false;

    settings = std::move(context.settings);
    return true;
}

} // namespace deepseek
