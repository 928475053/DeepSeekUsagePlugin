#include "ConfigStore.h"

#include "StringUtils.h"

#include <Windows.h>
#include <dpapi.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace deepseek
{
namespace
{

constexpr uint32_t kConfigMagic = 0x50555344; // DSUP
constexpr uint32_t kConfigVersion = 1;
constexpr uint64_t kMaxConfigFileSize = 64 * 1024;
constexpr uint32_t kMaxApiKeyUtf8Bytes = 16 * 1024;

constexpr unsigned char kEntropy[] = {
    'D', 'e', 'e', 'p', 'S', 'e', 'e', 'k',
    'U', 's', 'a', 'g', 'e', 'P', 'l', 'u', 'g', 'i', 'n',
    '-', 'C', 'o', 'n', 'f', 'i', 'g', '-', 'v', '1'
};

#pragma pack(push, 1)
struct ConfigFileHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t payloadSize;
};
#pragma pack(pop)

void AppendUInt32(std::vector<unsigned char>& buffer, uint32_t value)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(value));
}

bool ReadUInt32(
    const std::vector<unsigned char>& buffer,
    size_t& offset,
    uint32_t& value)
{
    if (offset > buffer.size() || buffer.size() - offset < sizeof(value))
        return false;

    std::memcpy(&value, buffer.data() + offset, sizeof(value));
    offset += sizeof(value);
    return true;
}

std::vector<unsigned char> BuildPayload(const Settings& settings)
{
    const std::string apiKey = WideToUtf8(settings.apiKey);

    std::vector<unsigned char> payload;
    payload.reserve(sizeof(uint32_t) * 3 + apiKey.size());
    AppendUInt32(payload, static_cast<uint32_t>(settings.pollIntervalSeconds));
    AppendUInt32(payload, static_cast<uint32_t>(settings.currencyPreference));
    AppendUInt32(payload, static_cast<uint32_t>(apiKey.size()));
    payload.insert(payload.end(), apiKey.begin(), apiKey.end());
    return payload;
}

bool ParsePayload(
    const std::vector<unsigned char>& payload,
    Settings& settings,
    std::wstring& errorMessage)
{
    size_t offset = 0;
    uint32_t interval = 0;
    uint32_t currency = 0;
    uint32_t apiKeySize = 0;

    if (!ReadUInt32(payload, offset, interval)
        || !ReadUInt32(payload, offset, currency)
        || !ReadUInt32(payload, offset, apiKeySize))
    {
        errorMessage = L"配置文件数据不完整";
        return false;
    }

    if (apiKeySize > kMaxApiKeyUtf8Bytes || apiKeySize > payload.size() - offset)
    {
        errorMessage = L"配置文件中的 API Key 长度无效";
        return false;
    }

    if (interval < static_cast<uint32_t>(kMinPollIntervalSeconds)
        || interval > static_cast<uint32_t>(kMaxPollIntervalSeconds))
    {
        errorMessage = L"配置文件中的轮询间隔无效";
        return false;
    }

    if (currency > static_cast<uint32_t>(CurrencyPreference::USD))
    {
        errorMessage = L"配置文件中的币种设置无效";
        return false;
    }

    const std::string apiKeyUtf8(
        reinterpret_cast<const char*>(payload.data() + offset),
        apiKeySize);
    const std::wstring apiKey = Utf8ToWide(apiKeyUtf8);
    if (!apiKeyUtf8.empty() && apiKey.empty())
    {
        errorMessage = L"配置文件中的 API Key 不是有效的 UTF-8 文本";
        return false;
    }
    if (!apiKey.empty() && !IsValidApiKey(apiKey))
    {
        errorMessage = L"配置文件中的 API Key 包含无效字符";
        return false;
    }

    if (offset + apiKeySize != payload.size())
    {
        errorMessage = L"配置文件格式无效";
        return false;
    }

    settings.apiKey = apiKey;
    settings.pollIntervalSeconds = static_cast<int>(interval);
    settings.currencyPreference = static_cast<CurrencyPreference>(currency);
    return true;
}

DATA_BLOB MakeBlob(const void* data, size_t size)
{
    DATA_BLOB blob{};
    blob.pbData = const_cast<BYTE*>(static_cast<const BYTE*>(data));
    blob.cbData = static_cast<DWORD>(size);
    return blob;
}

bool ReadFileBytes(
    const std::wstring& filePath,
    std::vector<unsigned char>& bytes,
    std::wstring& errorMessage)
{
    HANDLE file = CreateFileW(
        filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return false;
        errorMessage = L"无法读取配置文件：" + FormatWindowsError(error);
        return false;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize)
        || fileSize.QuadPart < 0
        || static_cast<uint64_t>(fileSize.QuadPart) > kMaxConfigFileSize)
    {
        const DWORD error = GetLastError();
        CloseHandle(file);
        errorMessage = fileSize.QuadPart > 0
            ? L"配置文件过大"
            : L"无法获取配置文件大小：" + FormatWindowsError(error);
        return false;
    }

    bytes.resize(static_cast<size_t>(fileSize.QuadPart));
    size_t totalRead = 0;
    while (totalRead < bytes.size())
    {
        const DWORD readSize = static_cast<DWORD>(
            std::min<size_t>(bytes.size() - totalRead, 64 * 1024));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + totalRead, readSize, &read, nullptr))
        {
            const DWORD error = GetLastError();
            CloseHandle(file);
            errorMessage = L"读取配置文件失败：" + FormatWindowsError(error);
            return false;
        }
        if (read == 0)
            break;
        totalRead += read;
    }

    CloseHandle(file);

    if (totalRead != bytes.size())
    {
        errorMessage = L"配置文件读取不完整";
        return false;
    }

    return true;
}

bool WriteFileBytes(
    const std::wstring& filePath,
    const std::vector<unsigned char>& bytes,
    std::wstring& errorMessage)
{
    HANDLE file = CreateFileW(
        filePath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE)
    {
        errorMessage = L"无法创建配置文件：" + FormatWindowsError(GetLastError());
        return false;
    }

    size_t totalWritten = 0;
    while (totalWritten < bytes.size())
    {
        const DWORD writeSize = static_cast<DWORD>(
            std::min<size_t>(bytes.size() - totalWritten, 64 * 1024));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + totalWritten, writeSize, &written, nullptr))
        {
            const DWORD error = GetLastError();
            CloseHandle(file);
            errorMessage = L"写入配置文件失败：" + FormatWindowsError(error);
            return false;
        }
        totalWritten += written;
    }

    if (!FlushFileBuffers(file))
    {
        const DWORD error = GetLastError();
        CloseHandle(file);
        errorMessage = L"刷新配置文件失败：" + FormatWindowsError(error);
        return false;
    }

    CloseHandle(file);
    return true;
}

std::wstring GetModulePath()
{
    std::wstring path(MAX_PATH, L'\0');
    while (true)
    {
        const DWORD length = GetModuleFileNameW(
            GetPluginModuleHandle(),
            path.data(),
            static_cast<DWORD>(path.size()));
        if (length == 0)
            return {};
        if (length < path.size() - 1)
        {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring GetDirectoryName(const std::wstring& path)
{
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring{} : path.substr(0, separator);
}

} // namespace

std::wstring ConfigStore::MakeConfigPath(const std::wstring& configDirectory)
{
    if (!configDirectory.empty())
    {
        std::wstring result = configDirectory;
        if (result.back() != L'\\' && result.back() != L'/')
            result.push_back(L'\\');
        result += L"DeepSeekUsagePlugin.dat";
        return result;
    }

    std::wstring moduleDirectory = GetDirectoryName(GetModulePath());
    if (moduleDirectory.empty())
        return L"DeepSeekUsagePlugin.dat";

    moduleDirectory.push_back(L'\\');
    moduleDirectory += L"DeepSeekUsagePlugin.dat";
    return moduleDirectory;
}

ConfigLoadStatus ConfigStore::Load(
    const std::wstring& filePath,
    Settings& settings,
    std::wstring& errorMessage)
{
    errorMessage.clear();

    if (GetFileAttributesW(filePath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return ConfigLoadStatus::NotFound;
        errorMessage = L"无法访问配置文件：" + FormatWindowsError(error);
        return ConfigLoadStatus::Error;
    }

    std::vector<unsigned char> fileBytes;
    if (!ReadFileBytes(filePath, fileBytes, errorMessage))
        return ConfigLoadStatus::Error;

    if (fileBytes.size() < sizeof(ConfigFileHeader))
    {
        errorMessage = L"配置文件头不完整";
        return ConfigLoadStatus::Error;
    }

    ConfigFileHeader header{};
    std::memcpy(&header, fileBytes.data(), sizeof(header));
    if (header.magic != kConfigMagic || header.version != kConfigVersion)
    {
        errorMessage = L"配置文件版本不受支持";
        return ConfigLoadStatus::Error;
    }
    if (header.payloadSize > fileBytes.size() - sizeof(ConfigFileHeader)
        || header.payloadSize != fileBytes.size() - sizeof(ConfigFileHeader))
    {
        errorMessage = L"配置文件长度无效";
        return ConfigLoadStatus::Error;
    }

    DATA_BLOB encryptedBlob = MakeBlob(
        fileBytes.data() + sizeof(ConfigFileHeader),
        header.payloadSize);
    DATA_BLOB entropyBlob = MakeBlob(kEntropy, sizeof(kEntropy));
    DATA_BLOB plainBlob{};
    if (!CryptUnprotectData(
        &encryptedBlob,
        nullptr,
        &entropyBlob,
        nullptr,
        nullptr,
        CRYPTPROTECT_UI_FORBIDDEN,
        &plainBlob))
    {
        errorMessage = L"无法解密配置文件：" + FormatWindowsError(GetLastError());
        return ConfigLoadStatus::Error;
    }

    std::vector<unsigned char> payload(
        plainBlob.pbData,
        plainBlob.pbData + plainBlob.cbData);
    LocalFree(plainBlob.pbData);

    Settings loaded;
    if (!ParsePayload(payload, loaded, errorMessage))
        return ConfigLoadStatus::Error;

    settings = std::move(loaded);
    return ConfigLoadStatus::Loaded;
}

bool ConfigStore::Save(
    const std::wstring& filePath,
    const Settings& settings,
    std::wstring& errorMessage)
{
    errorMessage.clear();

    if (settings.pollIntervalSeconds < kMinPollIntervalSeconds
        || settings.pollIntervalSeconds > kMaxPollIntervalSeconds)
    {
        errorMessage = L"轮询间隔必须在 10 到 86400 秒之间";
        return false;
    }
    if (!settings.apiKey.empty() && !IsValidApiKey(settings.apiKey))
    {
        errorMessage = L"API Key 只能包含可见的 ASCII 字符";
        return false;
    }

    const std::vector<unsigned char> payload = BuildPayload(settings);
    if (payload.size() > std::numeric_limits<DWORD>::max())
    {
        errorMessage = L"配置数据过大";
        return false;
    }

    DATA_BLOB plainBlob = MakeBlob(payload.data(), payload.size());
    DATA_BLOB entropyBlob = MakeBlob(kEntropy, sizeof(kEntropy));
    DATA_BLOB encryptedBlob{};
    if (!CryptProtectData(
        &plainBlob,
        L"DeepSeekUsagePlugin",
        &entropyBlob,
        nullptr,
        nullptr,
        CRYPTPROTECT_UI_FORBIDDEN,
        &encryptedBlob))
    {
        errorMessage = L"无法加密配置：" + FormatWindowsError(GetLastError());
        return false;
    }

    ConfigFileHeader header{};
    header.magic = kConfigMagic;
    header.version = kConfigVersion;
    header.payloadSize = encryptedBlob.cbData;

    std::vector<unsigned char> fileBytes(sizeof(header) + encryptedBlob.cbData);
    std::memcpy(fileBytes.data(), &header, sizeof(header));
    std::memcpy(
        fileBytes.data() + sizeof(header),
        encryptedBlob.pbData,
        encryptedBlob.cbData);
    LocalFree(encryptedBlob.pbData);

    const std::wstring directory = GetDirectoryName(filePath);
    if (!directory.empty())
        CreateDirectoryW(directory.c_str(), nullptr);

    const std::wstring temporaryPath = filePath + L".tmp";
    if (!WriteFileBytes(temporaryPath, fileBytes, errorMessage))
    {
        DeleteFileW(temporaryPath.c_str());
        return false;
    }

    if (!MoveFileExW(
        temporaryPath.c_str(),
        filePath.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        errorMessage = L"无法替换配置文件：" + FormatWindowsError(GetLastError());
        DeleteFileW(temporaryPath.c_str());
        return false;
    }

    return true;
}

} // namespace deepseek
