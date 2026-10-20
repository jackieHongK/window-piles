#include "json.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/win_util.h"

namespace wp::json {

namespace {

struct Reader {
    const std::string& s;
    size_t p = 0;
    int depth = 0;

    void Ws() {
        while (p < s.size() && isspace((unsigned char)s[p])) ++p;
    }
    bool Eat(char c) {
        Ws();
        if (p < s.size() && s[p] == c) {
            ++p;
            return true;
        }
        return false;
    }
    static void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) out += (char)(0xC0 | (cp >> 6)), out += (char)(0x80 | (cp & 0x3F));
        else if (cp < 0x10000)
            out += (char)(0xE0 | (cp >> 12)), out += (char)(0x80 | ((cp >> 6) & 0x3F)), out += (char)(0x80 | (cp & 0x3F));
        else
            out += (char)(0xF0 | (cp >> 18)), out += (char)(0x80 | ((cp >> 12) & 0x3F)),
                out += (char)(0x80 | ((cp >> 6) & 0x3F)), out += (char)(0x80 | (cp & 0x3F));
    }
    bool String(std::string& out) {
        Ws();
        if (p >= s.size() || s[p] != '"') return false;
        ++p;
        out.clear();
        while (p < s.size() && s[p] != '"') {
            char c = s[p++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p >= s.size()) return false;
            char e = s[p++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (p + 4 > s.size()) return false;
                    uint32_t cp = (uint32_t)strtoul(s.substr(p, 4).c_str(), nullptr, 16);
                    p += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && p + 6 <= s.size() && s[p] == '\\' && s[p + 1] == 'u') {
                        uint32_t lo = (uint32_t)strtoul(s.substr(p + 2, 4).c_str(), nullptr, 16);
                        p += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: out += e; break;
            }
        }
        return Eat('"');
    }
    bool Val(Value& v) {
        if (++depth > 64) return false;
        Ws();
        if (p >= s.size()) return false;
        char c = s[p];
        bool ok = true;
        if (c == '{') {
            ++p;
            v.type = Value::Type::Object;
            if (!Eat('}')) {
                do {
                    std::string key;
                    Value child;
                    if (!String(key) || !Eat(':') || !Val(child)) return false;
                    v.obj.emplace_back(std::move(key), std::move(child));
                } while (Eat(','));
                ok = Eat('}');
            }
        } else if (c == '[') {
            ++p;
            v.type = Value::Type::Array;
            if (!Eat(']')) {
                do {
                    Value child;
                    if (!Val(child)) return false;
                    v.arr.push_back(std::move(child));
                } while (Eat(','));
                ok = Eat(']');
            }
        } else if (c == '"') {
            v.type = Value::Type::String;
            ok = String(v.str);
        } else if (s.compare(p, 4, "true") == 0) {
            v.type = Value::Type::Bool, v.b = true, p += 4;
        } else if (s.compare(p, 5, "false") == 0) {
            v.type = Value::Type::Bool, v.b = false, p += 5;
        } else if (s.compare(p, 4, "null") == 0) {
            p += 4;
        } else {
            size_t start = p;
            while (p < s.size() && (isdigit((unsigned char)s[p]) || strchr("+-.eE", s[p]))) ++p;
            if (p == start) return false;
            v.type = Value::Type::Number;
            v.str = s.substr(start, p - start);
        }
        --depth;
        return ok;
    }
};

}  // namespace

const Value* Value::Get(const char* key) const {
    for (auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

std::wstring Value::WStr() const { return type == Type::String ? Widen(str) : std::wstring(); }

long long Value::Int(long long def) const {
    if (type != Type::Number && type != Type::String) return def;
    return strtoll(str.c_str(), nullptr, 10);
}

uint64_t Value::U64() const {
    if (type != Type::Number && type != Type::String) return 0;
    return strtoull(str.c_str(), nullptr, 10);
}

bool Parse(const std::string& text, Value& out) {
    Reader r{text};
    if (!r.Val(out)) return false;
    r.Ws();
    return r.p == text.size();
}

std::string Quote(const std::wstring& ws) {
    std::string s = Utf8(ws), o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if ((unsigned char)c < 0x20) {
            char b[8];
            snprintf(b, sizeof(b), "\\u%04x", c);
            o += b;
        } else o += c;
    }
    return o + "\"";
}

}  // namespace wp::json
