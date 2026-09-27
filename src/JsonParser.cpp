#include "JsonParser.h"

#include "StringUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace deepseek
{
namespace
{

constexpr size_t kMaxJsonDepth = 64;

class JsonParser
{
public:
    explicit JsonParser(std::string_view text)
        : text_(text)
    {
    }

    bool ParseBalanceDocument(BalanceResponse& response, std::wstring& error)
    {
        const size_t originalPosition = position_;
        if (!SkipWhitespace())
        {
            error = ErrorAtPosition();
            return false;
        }

        if (!Consume('{'))
        {
            error = L"DeepSeek 响应不是有效的 JSON 对象";
            return false;
        }

        bool hasBalanceInfos = false;
        while (true)
        {
            if (!SkipWhitespace())
            {
                error = ErrorAtPosition();
                return false;
            }

            if (Consume('}'))
                break;

            std::string key;
            if (!ParseString(key) || !SkipWhitespace() || !Consume(':'))
            {
                error = ErrorAtPosition();
                return false;
            }

            if (!SkipWhitespace())
            {
                error = ErrorAtPosition();
                return false;
            }

            if (key == "is_available")
            {
                if (!ParseBool(response.isAvailable))
                {
                    error = ErrorAtPosition();
                    return false;
                }
            }
            else if (key == "balance_infos")
            {
                if (!ParseBalanceInfos(response.balanceInfos))
                {
                    error = ErrorAtPosition();
                    return false;
                }
                hasBalanceInfos = true;
            }
            else if (key == "error")
            {
                std::wstring apiError;
                if (!ParseErrorValue(apiError))
                {
                    error = ErrorAtPosition();
                    return false;
                }
                if (!apiError.empty())
                    error = std::move(apiError);
            }
            else if (!SkipValue(0))
            {
                error = ErrorAtPosition();
                return false;
            }

            if (!SkipWhitespace())
            {
                error = ErrorAtPosition();
                return false;
            }

            if (Consume(','))
                continue;
            if (Consume('}'))
                break;

            error = ErrorAtPosition();
            return false;
        }

        if (!SkipWhitespace() || position_ != text_.size())
        {
            position_ = originalPosition;
            error = ErrorAtPosition();
            return false;
        }

        if (!error.empty())
            return false;

        if (!hasBalanceInfos)
        {
            error = L"DeepSeek 响应中缺少 balance_infos 字段";
            return false;
        }

        return true;
    }

    bool ExtractError(std::wstring& error)
    {
        if (!SkipWhitespace() || !Consume('{'))
            return false;

        while (true)
        {
            if (!SkipWhitespace())
                return false;
            if (Consume('}'))
                return !error.empty();

            std::string key;
            if (!ParseString(key) || !SkipWhitespace() || !Consume(':') || !SkipWhitespace())
                return false;

            if (key == "error")
            {
                if (!ParseErrorValue(error))
                    return false;
            }
            else if (!SkipValue(0))
            {
                return false;
            }

            if (!SkipWhitespace())
                return false;
            if (Consume(','))
                continue;
            if (Consume('}'))
                return !error.empty();
            return false;
        }
    }

private:
    bool ParseErrorValue(std::wstring& error)
    {
        if (Current() == '{')
        {
            if (!Expect('{'))
                return false;

            while (true)
            {
                if (!SkipWhitespace())
                    return false;
                if (Consume('}'))
                    return true;

                std::string key;
                if (!ParseString(key) || !SkipWhitespace() || !Consume(':') || !SkipWhitespace())
                    return false;

                if (key == "message")
                {
                    std::string message;
                    if (!ParseStringLike(message))
                        return false;
                    error = Utf8ToWide(message);
                }
                else if (!SkipValue(1))
                {
                    return false;
                }

                if (!SkipWhitespace())
                    return false;
                if (Consume(','))
                    continue;
                if (Consume('}'))
                    return true;
                return false;
            }
        }

        if (Current() == '"')
        {
            std::string message;
            if (!ParseString(message))
                return false;
            error = Utf8ToWide(message);
            return true;
        }

        return SkipValue(0);
    }

    bool ParseBalanceInfos(std::vector<BalanceInfo>& infos)
    {
        if (!Expect('['))
            return false;

        infos.clear();
        if (!SkipWhitespace())
            return false;
        if (Consume(']'))
            return true;

        while (true)
        {
            BalanceInfo info;
            if (!ParseBalanceInfo(info))
                return false;
            infos.push_back(std::move(info));

            if (!SkipWhitespace())
                return false;
            if (Consume(','))
            {
                if (!SkipWhitespace())
                    return false;
                continue;
            }
            if (Consume(']'))
                return true;
            return false;
        }
    }

    bool ParseBalanceInfo(BalanceInfo& info)
    {
        if (!Expect('{'))
            return false;

        bool hasCurrency = false;
        bool hasTotalBalance = false;

        while (true)
        {
            if (!SkipWhitespace())
                return false;
            if (Consume('}'))
                break;

            std::string key;
            if (!ParseString(key) || !SkipWhitespace() || !Consume(':') || !SkipWhitespace())
                return false;

            if (key == "currency")
            {
                std::string value;
                if (!ParseStringLike(value))
                    return false;
                info.currency = Utf8ToWide(value);
                hasCurrency = true;
            }
            else if (key == "total_balance")
            {
                std::string value;
                if (!ParseStringLike(value))
                    return false;
                info.totalBalance = Utf8ToWide(value);
                hasTotalBalance = true;
            }
            else if (key == "granted_balance")
            {
                std::string value;
                if (!ParseStringLike(value))
                    return false;
                info.grantedBalance = Utf8ToWide(value);
            }
            else if (key == "topped_up_balance")
            {
                std::string value;
                if (!ParseStringLike(value))
                    return false;
                info.toppedUpBalance = Utf8ToWide(value);
            }
            else if (!SkipValue(1))
            {
                return false;
            }

            if (!SkipWhitespace())
                return false;
            if (Consume(','))
                continue;
            if (Consume('}'))
                break;
            return false;
        }

        return hasCurrency && hasTotalBalance;
    }

    bool SkipValue(size_t depth)
    {
        if (depth > kMaxJsonDepth)
            return false;

        if (!SkipWhitespace())
            return false;

        const char current = Current();
        if (current == '"')
        {
            std::string ignored;
            return ParseString(ignored);
        }
        if (current == '{')
        {
            if (!Expect('{'))
                return false;
            if (!SkipWhitespace())
                return false;
            if (Consume('}'))
                return true;

            while (true)
            {
                std::string key;
                if (!ParseString(key) || !SkipWhitespace() || !Consume(':') || !SkipValue(depth + 1))
                    return false;
                if (!SkipWhitespace())
                    return false;
                if (Consume(','))
                {
                    if (!SkipWhitespace())
                        return false;
                    continue;
                }
                if (Consume('}'))
                    return true;
                return false;
            }
        }
        if (current == '[')
        {
            if (!Expect('['))
                return false;
            if (!SkipWhitespace())
                return false;
            if (Consume(']'))
                return true;

            while (true)
            {
                if (!SkipValue(depth + 1))
                    return false;
                if (!SkipWhitespace())
                    return false;
                if (Consume(','))
                {
                    if (!SkipWhitespace())
                        return false;
                    continue;
                }
                if (Consume(']'))
                    return true;
                return false;
            }
        }
        if (ConsumeLiteral("true") || ConsumeLiteral("false") || ConsumeLiteral("null"))
            return true;

        return ParseNumber();
    }

    bool ParseNumber()
    {
        const size_t start = position_;
        if (Current() == '-')
            ++position_;

        if (Current() == '0')
        {
            ++position_;
        }
        else
        {
            if (!IsDigit(Current()) || Current() == '0')
                return false;
            while (IsDigit(Current()))
                ++position_;
        }

        if (Current() == '.')
        {
            ++position_;
            if (!IsDigit(Current()))
                return false;
            while (IsDigit(Current()))
                ++position_;
        }

        if (Current() == 'e' || Current() == 'E')
        {
            ++position_;
            if (Current() == '+' || Current() == '-')
                ++position_;
            if (!IsDigit(Current()))
                return false;
            while (IsDigit(Current()))
                ++position_;
        }

        return position_ > start;
    }

    bool ParseStringLike(std::string& value)
    {
        if (Current() == '"')
            return ParseString(value);

        const size_t start = position_;
        if (!ParseNumber())
            return false;
        value.assign(text_.substr(start, position_ - start));
        return true;
    }

    bool ParseString(std::string& value)
    {
        if (!Expect('"'))
            return false;

        value.clear();
        while (true)
        {
            if (position_ >= text_.size())
                return false;

            const unsigned char ch = static_cast<unsigned char>(text_[position_++]);
            if (ch == '"')
                return true;
            if (ch < 0x20)
                return false;

            if (ch != '\\')
            {
                value.push_back(static_cast<char>(ch));
                continue;
            }

            if (position_ >= text_.size())
                return false;

            const char escaped = text_[position_++];
            switch (escaped)
            {
            case '"':
            case '\\':
            case '/':
                value.push_back(escaped);
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case 'u':
            {
                uint32_t codePoint = 0;
                if (!ParseHex4(codePoint))
                    return false;

                if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
                {
                    if (position_ + 2 > text_.size()
                        || text_[position_] != '\\'
                        || text_[position_ + 1] != 'u')
                    {
                        return false;
                    }
                    position_ += 2;

                    uint32_t lowSurrogate = 0;
                    if (!ParseHex4(lowSurrogate)
                        || lowSurrogate < 0xDC00
                        || lowSurrogate > 0xDFFF)
                    {
                        return false;
                    }

                    codePoint = 0x10000
                        + ((codePoint - 0xD800) << 10)
                        + (lowSurrogate - 0xDC00);
                }
                else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
                {
                    return false;
                }

                AppendUtf8(value, codePoint);
                break;
            }
            default:
                return false;
            }
        }
    }

    bool ParseBool(bool& value)
    {
        if (ConsumeLiteral("true"))
        {
            value = true;
            return true;
        }
        if (ConsumeLiteral("false"))
        {
            value = false;
            return true;
        }
        return false;
    }

    bool ParseHex4(uint32_t& value)
    {
        if (position_ + 4 > text_.size())
            return false;

        value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char ch = text_[position_++];
            value <<= 4;
            if (ch >= '0' && ch <= '9')
                value |= static_cast<uint32_t>(ch - '0');
            else if (ch >= 'a' && ch <= 'f')
                value |= static_cast<uint32_t>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F')
                value |= static_cast<uint32_t>(ch - 'A' + 10);
            else
                return false;
        }
        return true;
    }

    static void AppendUtf8(std::string& output, uint32_t codePoint)
    {
        if (codePoint <= 0x7F)
        {
            output.push_back(static_cast<char>(codePoint));
        }
        else if (codePoint <= 0x7FF)
        {
            output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else if (codePoint <= 0xFFFF)
        {
            output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else
        {
            output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    bool SkipWhitespace()
    {
        if (position_ == 0 && text_.size() >= 3
            && static_cast<unsigned char>(text_[0]) == 0xEF
            && static_cast<unsigned char>(text_[1]) == 0xBB
            && static_cast<unsigned char>(text_[2]) == 0xBF)
        {
            position_ = 3;
        }

        while (position_ < text_.size())
        {
            const char ch = text_[position_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
                break;
            ++position_;
        }
        return true;
    }

    bool Consume(char expected)
    {
        if (position_ >= text_.size() || text_[position_] != expected)
            return false;
        ++position_;
        return true;
    }

    bool Expect(char expected)
    {
        return Consume(expected);
    }

    bool ConsumeLiteral(std::string_view literal)
    {
        if (text_.substr(position_, literal.size()) != literal)
            return false;
        position_ += literal.size();
        return true;
    }

    char Current() const
    {
        return position_ < text_.size() ? text_[position_] : '\0';
    }

    static bool IsDigit(char ch)
    {
        return ch >= '0' && ch <= '9';
    }

    std::wstring ErrorAtPosition() const
    {
        return L"JSON 格式错误（位置 " + std::to_wstring(position_) + L"）";
    }

    std::string_view text_;
    size_t position_{};
};

} // namespace

bool ParseBalanceResponse(
    const std::string& utf8Json,
    BalanceResponse& response,
    std::wstring& errorMessage)
{
    errorMessage.clear();
    response = {};

    JsonParser parser(utf8Json);
    return parser.ParseBalanceDocument(response, errorMessage);
}

bool ExtractApiErrorMessage(
    const std::string& utf8Json,
    std::wstring& errorMessage)
{
    errorMessage.clear();

    JsonParser parser(utf8Json);
    return parser.ExtractError(errorMessage);
}

} // namespace deepseek
