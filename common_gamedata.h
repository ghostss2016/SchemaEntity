#pragma once
#ifndef FLEET_COMMON_GAMEDATA_H
#define FLEET_COMMON_GAMEDATA_H
// Shared storage adapter only. Address resolution and ABI checks stay in callers.
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <chrono>
#include <mutex>
#include <sstream>

namespace FleetGamedata {
inline void reportUnavailable(const std::string& key) {
    static std::mutex lock;
    static std::map<std::string, std::chrono::steady_clock::time_point> reported;
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> guard(lock);
    auto it = reported.find(key);
    if (it != reported.end() && now - it->second < std::chrono::seconds(60)) return;
    reported[key] = now;
    std::fprintf(stderr, "[FleetGamedata] Self-repair unresolved: %s; dependent call skipped. Check signatures.ini and engine analysis.\n", key.c_str());
}
class Document {
public:
    std::map<std::string, std::string> values;

    bool load(const std::string& path, std::string& error) {
        values.clear();
        std::ifstream file(path);
        if (!file) { error = "missing gamedata: " + path; return false; }
        const std::string text(std::istreambuf_iterator<char>(file), {});
        if (file.bad()) { error = "cannot read gamedata: " + path; return false; }
        size_t position = 0;
        std::string key;
        if (token(text, position, key) != 1 || key != "Gamedata" ||
            token(text, position, key) != 2 || key != "{" ||
            !object(text, position, "", 0) || token(text, position, key) != 0) {
            values.clear();
            error = "invalid or duplicate shared gamedata: " + path;
            return false;
        }
        return true;
    }

    const char* get(const std::string& key) const {
        auto it = values.find(key);
        return it == values.end() ? "" : it->second.c_str();
    }

    int integer(const std::string& key, int missing = -1) const {
        const char* value = get(key);
        if (!*value) return missing;
        char* end = nullptr;
        errno = 0;
        const long parsed = std::strtol(value, &end, 10);
        return errno || *end || parsed < INT_MIN || parsed > INT_MAX ? missing : static_cast<int>(parsed);
    }

private:
    // Valve KeyValues defaults to literal backslashes (e.g. signatures "\\x55").
    static int token(const std::string& text, size_t& pos, std::string& out) {
        out.clear();
        while (pos < text.size()) {
            if (std::isspace(static_cast<unsigned char>(text[pos]))) { ++pos; continue; }
            if (text.compare(pos, 2, "//") == 0) {
                const size_t next = text.find('\n', pos);
                pos = next == std::string::npos ? text.size() : next + 1;
                continue;
            }
            break;
        }
        if (pos == text.size()) return 0;
        const char first = text[pos++];
        if (first == '{' || first == '}') { out += first; return 2; }
        if (first == '"') {
            while (pos < text.size() && text[pos] != '"') out += text[pos++];
            if (pos == text.size()) return -1;
            ++pos;
            return 1;
        }
        out += first;
        while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos])) &&
               text[pos] != '{' && text[pos] != '}') out += text[pos++];
        return 1;
    }

    bool object(const std::string& text, size_t& pos, const std::string& prefix, unsigned depth) {
        if (depth > 32) return false;
        std::map<std::string, bool> keys;
        std::string key, value;
        for (;;) {
            const int kind = token(text, pos, key);
            if (kind == 2 && key == "}") return true;
            if (kind != 1 || key.empty() || key.find('/') != std::string::npos) return false;
            std::string folded = key;
            for (char& c : folded) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!keys.emplace(folded, true).second) return false;
            const std::string path = prefix + key;
            const int valueKind = token(text, pos, value);
            if (valueKind == 2 && value == "{") {
                if (!object(text, pos, path + '/', depth + 1)) return false;
            } else if (valueKind == 1) {
                if (!values.emplace(path, value).second) return false;
            } else return false;
        }
    }
};

inline std::string pathFromLoadedPlugin() {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        const size_t path = line.find('/');
        const size_t addons = line.find("/addons/", path);
        if (path != std::string::npos && addons != std::string::npos)
            return line.substr(path, addons - path) + "/addons/configs/signatures.ini";
    }
    return {};
}

inline const Document& current() {
    static const Document document = [] {
        Document result;
        std::string error;
        if (!result.load(pathFromLoadedPlugin(), error))
            std::fprintf(stderr, "[FleetGamedata] %s\n", error.c_str());
        return result;
    }();
    return document;
}

struct Pattern {
    std::string bytes;
    std::string mask;
};

inline const Pattern& pattern(const std::string& key) {
    static std::mutex lock;
    static std::map<std::string, Pattern> patterns;
    std::lock_guard<std::mutex> guard(lock);
    auto existing = patterns.find(key);
    if (existing != patterns.end()) return existing->second;
    Pattern result;
    std::istringstream input(current().get(key));
    std::string token;
    while (input >> token) {
        if (token == "?" || token == "??") {
            result.bytes += '\0';
            result.mask += '?';
        } else if (token.size() == 2 && token.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
            result.bytes += static_cast<char>(std::strtoul(token.c_str(), nullptr, 16));
            result.mask += 'x';
        } else {
            result = Pattern();
            break;
        }
    }
    if (result.bytes.empty()) reportUnavailable(key);
    return patterns.emplace(key, std::move(result)).first->second;
}
} // namespace FleetGamedata
#endif
