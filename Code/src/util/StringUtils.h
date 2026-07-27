#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

// Small helpers filling the gap between std::string and the Arduino String
// API this codebase used to lean on (toUpperCase/startsWith/endsWith/toInt/
// toFloat/trim), used while porting away from Arduino.
namespace StringUtils {

inline bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

inline bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}

inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// strtol/strtof don't throw on bad input (unlike std::stoi/std::stof) and
// return 0 instead, matching Arduino String::toInt()/toFloat() behavior.
inline int toInt(const std::string& s) {
    return static_cast<int>(std::strtol(s.c_str(), nullptr, 10));
}

inline float toFloat(const std::string& s) {
    return std::strtof(s.c_str(), nullptr);
}

// Matches Arduino's String(float, decimals) formatting (default 2 decimals),
// which the JSON/MQTT payloads built throughout this codebase were tuned for.
inline std::string toString(float value, int decimals = 2) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    return buf;
}

inline std::string toString(int value) {
    return std::to_string(value);
}

inline std::string toString(unsigned long value) {
    return std::to_string(value);
}

// Decodes application/x-www-form-urlencoded text (used for both query
// strings and POST form bodies): '+' -> space, '%XX' -> byte.
inline std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size()) {
            out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

} // namespace StringUtils

#endif // STRINGUTILS_H
