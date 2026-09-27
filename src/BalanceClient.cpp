#include "BalanceClient.h"

#include "JsonParser.h"
#include "StringUtils.h"

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <string_view>
#include <vector>

namespace deepseek
{
namespace
{

constexpr wchar_t kDeepSeekHost[] = L"api.deepseek.com";
constexpr wchar_t kBalancePath[] = L"/user/balance";
constexpr size_t kMaxResponseBodyBytes = 1024 * 1024;

class WinHttpHandle
{
public:
    WinHttpHandle() = default;

    explicit WinHttpHandle(HINTERNET handle)
        : handle_(handle)
    {
    }

    ~WinHttpHandle()
    {
        if (handle_ != nullptr)
            WinHttpCloseHandle(handle_);
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    WinHttpHandle(WinHttpHandle&& other) noexcept
        : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_ != nullptr)
                WinHttpCloseHandle(handle_);
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    HINTERNET get() const
    {
        return handle_;
    }

private:
    HINTERNET handle_{};
};

std::wstring GetHttpErrorMessage(DWORD errorCode)
{
    switch (errorCode)
    {
    case ERROR_WINHTTP_TIMEOUT:
        return L"网络请求超时";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return L"无法解析 api.deepseek.com";
    case ERROR_WINHTTP_CANNOT_CONNECT:
        return L"无法连接到 DeepSeek API";
    case ERROR_WINHTTP_CONNECTION_ERROR:
        return L"与 DeepSeek API 的连接中断";
    case ERROR_WINHTTP_SECURE_FAILURE:
        return L"HTTPS 安全连接验证失败";
    case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
        return L"系统要求客户端证书，无法完成请求";
    default:
        return FormatWindowsError(errorCode);
    }
}

std::wstring BuildHttpError(int statusCode, const std::wstring& apiMessage)
{
    std::wstring message;
    switch (statusCode)
    {
    case 400:
        message = L"请求参数无效";
        break;
    case 401:
        message = L"API Key 无效或已失效";
        break;
    case 402:
        message = L"账户余额不足或账户状态异常";
        break;
    case 403:
        message = L"当前 API Key 无权访问余额接口";
        break;
    case 429:
        message = L"请求过于频繁，请增大轮询间隔";
        break;
    case 500:
    case 502:
    case 503:
    case 504:
        message = L"DeepSeek 服务暂时不可用";
        break;
    default:
        message = L"DeepSeek API 返回 HTTP " + std::to_wstring(statusCode);
        break;
    }

    if (!apiMessage.empty())
    {
        message += L"：";
        message += apiMessage;
    }
    return message;
}

bool AddAuthorizationHeader(
    HINTERNET request,
    const std::wstring& apiKey,
    std::wstring& errorMessage)
{
    const std::wstring headers =
        L"Authorization: Bearer " + apiKey + L"\r\n"
        L"Accept: application/json\r\n"
        L"Cache-Control: no-cache\r\n";

    if (!WinHttpAddRequestHeaders(
        request,
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
    {
        errorMessage = L"无法设置 DeepSeek 授权请求头：" + FormatWindowsError(GetLastError());
        return false;
    }
    return true;
}

bool ReadResponseBody(
    HINTERNET request,
    std::vector<unsigned char>& body,
    std::wstring& errorMessage)
{
    while (true)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
        {
            errorMessage = L"读取 DeepSeek 响应失败：" + GetHttpErrorMessage(GetLastError());
            return false;
        }
        if (available == 0)
            return true;

        if (body.size() + available > kMaxResponseBodyBytes)
        {
            errorMessage = L"DeepSeek 响应超过 1 MB，已拒绝处理";
            return false;
        }

        const size_t oldSize = body.size();
        body.resize(oldSize + available);

        DWORD read = 0;
        if (!WinHttpReadData(
            request,
            body.data() + oldSize,
            available,
            &read))
        {
            errorMessage = L"读取 DeepSeek 响应失败：" + GetHttpErrorMessage(GetLastError());
            return false;
        }
        body.resize(oldSize + read);
        if (read == 0)
            return true;
    }
}

} // namespace

BalanceQueryResult BalanceClient::Query(const std::wstring& apiKey)
{
    BalanceQueryResult result;

    if (apiKey.empty())
    {
        result.errorMessage = L"未配置 DeepSeek API Key";
        return result;
    }
    if (!IsValidApiKey(apiKey))
    {
        result.errorMessage = L"API Key 包含无效字符";
        return result;
    }

    WinHttpHandle session(WinHttpOpen(
        L"TrafficMonitor-DeepSeekUsagePlugin/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (session.get() == nullptr)
    {
        session = WinHttpHandle(WinHttpOpen(
            L"TrafficMonitor-DeepSeekUsagePlugin/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0));
    }
    if (session.get() == nullptr)
    {
        result.errorMessage = L"无法初始化 WinHTTP：" + FormatWindowsError(GetLastError());
        return result;
    }

    WinHttpSetTimeouts(session.get(), 10000, 10000, 20000, 20000);

    WinHttpHandle connection(WinHttpConnect(
        session.get(),
        kDeepSeekHost,
        INTERNET_DEFAULT_HTTPS_PORT,
        0));
    if (connection.get() == nullptr)
    {
        result.errorMessage = L"无法连接到 DeepSeek API：" + GetHttpErrorMessage(GetLastError());
        return result;
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.get(),
        L"GET",
        kBalancePath,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (request.get() == nullptr)
    {
        result.errorMessage = L"无法创建 WinHTTP 请求：" + FormatWindowsError(GetLastError());
        return result;
    }

    if (!AddAuthorizationHeader(request.get(), apiKey, result.errorMessage))
        return result;

    if (!WinHttpSendRequest(
        request.get(),
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0))
    {
        result.errorMessage = L"发送 DeepSeek 请求失败：" + GetHttpErrorMessage(GetLastError());
        return result;
    }

    if (!WinHttpReceiveResponse(request.get(), nullptr))
    {
        result.errorMessage = L"接收 DeepSeek 响应失败：" + GetHttpErrorMessage(GetLastError());
        return result;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(
        request.get(),
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX))
    {
        result.errorMessage = L"无法读取 DeepSeek 响应状态：" + FormatWindowsError(GetLastError());
        return result;
    }
    result.httpStatus = static_cast<int>(statusCode);

    std::vector<unsigned char> body;
    if (!ReadResponseBody(request.get(), body, result.errorMessage))
        return result;

    const std::string utf8Body(
        reinterpret_cast<const char*>(body.data()),
        body.size());

    if (statusCode < 200 || statusCode >= 300)
    {
        std::wstring apiMessage;
        ExtractApiErrorMessage(utf8Body, apiMessage);
        result.errorMessage = BuildHttpError(static_cast<int>(statusCode), apiMessage);
        return result;
    }

    if (!ParseBalanceResponse(utf8Body, result.balance, result.errorMessage))
        return result;

    result.success = true;
    return result;
}

} // namespace deepseek
