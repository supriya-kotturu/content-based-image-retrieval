/*
  Author:  Sai Supriya Kotturu
  Date:    2026-10-06
  Purpose: Declares Args, the key=value command-line parser shared by the extract and query
           programs (e.g. `extract db=data/olympus patch=9`).
*/

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

    // True if `key` was given. Counts as a read for unusedKeys().
    bool has(const std::string& key) const;

    // Values stay strings; the caller converts because only it knows the type and error policy.
    std::string get(const std::string& key, const std::string& fallback = "") const;

    // Keys given but never read via has()/get(). A typo like `pach=9` is otherwise silently
    // ignored and the run uses the default. Call after all reads.
    std::vector<std::string> unusedKeys() const;

    // Writes every key=value as given on the command line, one per line, sorted by key. Does not
    // count as a read.
    void print(std::ostream& os) const;

   private:
    std::map<std::string, std::string> values_;
    mutable std::set<std::string> used_;  // mutable: reading is logically const
};

#endif  // COMMON_H