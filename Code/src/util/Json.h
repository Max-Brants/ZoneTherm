// Minimal RAII ownership for cJSON trees (ESP-IDF's bundled `json`
// component). Build with the regular cJSON_Add* API; JsonDoc guarantees the
// tree is freed and dump() handles the print buffer.

#pragma once

#include <string>

#include "cJSON.h"

class JsonDoc {
public:
    explicit JsonDoc(cJSON* root) : root_(root) {}
    ~JsonDoc() {
        if (root_) cJSON_Delete(root_);
    }
    JsonDoc(const JsonDoc&) = delete;
    JsonDoc& operator=(const JsonDoc&) = delete;

    cJSON* get() const { return root_; }
    explicit operator bool() const { return root_ != nullptr; }

    std::string dump() const {
        if (!root_) return "";
        char* printed = cJSON_PrintUnformatted(root_);
        if (!printed) return "";
        std::string out(printed);
        cJSON_free(printed);
        return out;
    }

private:
    cJSON* root_;
};

namespace json {

// Missing or non-string members fall back to the default.
inline std::string getString(const cJSON* obj, const char* key,
                             const std::string& fallback = "") {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : fallback;
}

inline double getNumber(const cJSON* obj, const char* key, double fallback = 0) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

inline bool getBool(const cJSON* obj, const char* key, bool fallback = false) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

inline bool has(const cJSON* obj, const char* key) {
    return cJSON_GetObjectItemCaseSensitive(obj, key) != nullptr;
}

}  // namespace json
