// ---------------------------------------------------------------------------
// Minimal JSON helpers implementation (FR-11).
// ---------------------------------------------------------------------------

#include "agent/json.hpp"

#include <cctype>
#include <cstddef>

namespace lumina::agent {

namespace {

// Position just after the ':' that follows the key's closing quote, or npos when
// the key is absent. This is a deliberately small scanner: it expects well-formed
// input from our own app and only needs to locate flat "key": value pairs.
[[nodiscard]] std::size_t findValue(std::string_view json, std::string_view key)
{
    const std::string needle = "\"" + std::string(key) + "\"";
    const std::size_t keyPos = json.find(needle);
    if (keyPos == std::string_view::npos) {
        return std::string_view::npos;
    }
    const std::size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string_view::npos) {
        return std::string_view::npos;
    }
    return colon + 1;
}

void skipWhitespace(std::string_view json, std::size_t& index)
{
    while (index < json.size() &&
           std::isspace(static_cast<unsigned char>(json[index])) != 0) {
        ++index;
    }
}

// Parse a JSON string starting at the opening quote at `index`; on success leaves
// `index` just past the closing quote and returns the decoded value.
[[nodiscard]] std::optional<std::string> parseString(std::string_view json, std::size_t& index)
{
    if (index >= json.size() || json[index] != '"') {
        return std::nullopt;
    }
    ++index;  // consume the opening quote
    std::string out;
    while (index < json.size()) {
        const char c = json[index++];
        if (c == '"') {
            return out;
        }
        if (c == '\\' && index < json.size()) {
            const char escaped = json[index++];
            switch (escaped) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'u': {
                // Decode a \uXXXX escape to UTF-8. Only the Basic Multilingual
                // Plane is handled (enough for our names); surrogate pairs are
                // copied as-is, which the app never sends.
                if (index + 4 > json.size()) {
                    return std::nullopt;
                }
                unsigned int code = 0;
                for (int i = 0; i < 4; ++i) {
                    const char digit = json[index++];
                    code <<= 4;
                    if (digit >= '0' && digit <= '9') {
                        code += static_cast<unsigned int>(digit - '0');
                    } else if (digit >= 'a' && digit <= 'f') {
                        code += static_cast<unsigned int>(digit - 'a' + 10);
                    } else if (digit >= 'A' && digit <= 'F') {
                        code += static_cast<unsigned int>(digit - 'A' + 10);
                    } else {
                        return std::nullopt;
                    }
                }
                if (code < 0x80) {
                    out.push_back(static_cast<char>(code));
                } else if (code < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                break;
            }
            default: return std::nullopt;
            }
        } else {
            out.push_back(c);
        }
    }
    return std::nullopt;  // unterminated string
}

}  // namespace

std::string escapeJson(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                // Other control characters are not valid raw in JSON; emit \u00XX.
                static const char* kHex = "0123456789abcdef";
                out += "\\u00";
                out.push_back(kHex[(static_cast<unsigned char>(c) >> 4) & 0xF]);
                out.push_back(kHex[static_cast<unsigned char>(c) & 0xF]);
            } else {
                out.push_back(c);  // UTF-8 continuation/lead bytes pass through
            }
        }
    }
    return out;
}

std::optional<std::string> jsonGetString(std::string_view json, std::string_view key)
{
    std::size_t index = findValue(json, key);
    if (index == std::string_view::npos) {
        return std::nullopt;
    }
    skipWhitespace(json, index);
    return parseString(json, index);
}

std::optional<long long> jsonGetInt(std::string_view json, std::string_view key)
{
    std::size_t index = findValue(json, key);
    if (index == std::string_view::npos) {
        return std::nullopt;
    }
    skipWhitespace(json, index);
    const std::size_t start = index;
    if (index < json.size() && json[index] == '-') {
        ++index;
    }
    while (index < json.size() && std::isdigit(static_cast<unsigned char>(json[index])) != 0) {
        ++index;
    }
    if (index == start) {
        return std::nullopt;
    }
    try {
        return std::stoll(std::string(json.substr(start, index - start)));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<bool> jsonGetBool(std::string_view json, std::string_view key)
{
    std::size_t index = findValue(json, key);
    if (index == std::string_view::npos) {
        return std::nullopt;
    }
    skipWhitespace(json, index);
    if (json.substr(index, 4) == "true") {
        return true;
    }
    if (json.substr(index, 5) == "false") {
        return false;
    }
    return std::nullopt;
}

std::vector<std::string> jsonGetStringArray(std::string_view json, std::string_view key)
{
    std::vector<std::string> values;
    std::size_t index = findValue(json, key);
    if (index == std::string_view::npos) {
        return values;
    }
    skipWhitespace(json, index);
    if (index >= json.size() || json[index] != '[') {
        return values;
    }
    ++index;  // consume '['
    while (index < json.size()) {
        skipWhitespace(json, index);
        if (index < json.size() && json[index] == ']') {
            break;
        }
        if (index < json.size() && json[index] == ',') {
            ++index;
            continue;
        }
        const std::optional<std::string> value = parseString(json, index);
        if (!value.has_value()) {
            break;  // malformed array: return what we have
        }
        values.push_back(*value);
    }
    return values;
}

}  // namespace lumina::agent
