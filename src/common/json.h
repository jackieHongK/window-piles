// Tiny JSON reader/writer helpers for the app's own state files (no external dependency).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace wp::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool b = false;
    std::string str;  // UTF-8 for strings; the literal text for numbers (exact uint64 round trip)
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    const Value* Get(const char* key) const;
    std::wstring WStr() const;
    long long Int(long long def = 0) const;
    uint64_t U64() const;  // numbers or numeric strings
    bool Bool(bool def = false) const { return type == Type::Bool ? b : def; }
};

bool Parse(const std::string& text, Value& out);

// JSON string literal (with quotes) from a wide string.
std::string Quote(const std::wstring& s);

}  // namespace wp::json
