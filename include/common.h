#ifndef COMMON_H
#define COMMON_H

#include <map>
#include <ostream>
#include <set>
#include <string>
#include <vector>

class Args {
   public:
    // Parses argv[1..] as key=value tokens, split at the FIRST '=' so values may contain '='.
    // Returns false on a malformed token (no '=' or empty key).
    bool parse(int argc, char** argv);

    bool has(const std::string& key) const;

    // Values stay strings; the caller converts because only it knows the type and error policy.
    std::string get(const std::string& key, const std::string& fallback = "") const;

    // Keys given but never read via has()/get(). A typo like `pach=9` is otherwise silently
    // ignored and the run uses the default. Call after all reads.
    std::vector<std::string> unusedKeys() const;

    void print(std::ostream& os) const;

   private:
    std::map<std::string, std::string> values_;
    mutable std::set<std::string> used_;  // mutable: reading is logically const
};

#endif  // COMMON_H