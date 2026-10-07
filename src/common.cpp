#include <common.h>

bool Args::parse(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string token = argv[i];
        std::size_t eq = token.find('=');
        if (eq == std::string::npos || eq == 0) {
            return false;
        }
        values_[token.substr(0, eq)] = token.substr(eq + 1);
    }
    return true;
}

bool Args::has(const std::string& key) const {
    used_.insert(key);
    return values_.count(key) > 0;
}

std::string Args::get(const std::string& key, const std::string& fallback) const {
    used_.insert(key);
    auto it = values_.find(key);
    return it == values_.end() ? fallback : it->second;
}

std::vector<std::string> Args::unusedKeys() const {
    std::vector<std::string> unused;
    for (const auto& kv : values_) {
        if (used_.count(kv.first) == 0) {
            unused.push_back(kv.first);
        }
    }
    return unused;
}

void Args::print(std::ostream& os) const {
    for (const auto& kv : values_) {
        os << kv.first << '=' << kv.second << '\n';
    }
}
