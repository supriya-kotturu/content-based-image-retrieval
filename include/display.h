#ifndef DISPLAY_H
#define DISPLAY_H

#include <string>
#include <vector>

// Shows the target followed by the matches in one window, in rank order.
//
// matchLabels[i] is drawn under matchPaths[i]; '\n' starts a new line. The target is labelled
// "TARGET" plus its filename. `title` is drawn across the top, e.g. the feature and metric used,
// so a screenshot in the report is self-describing.
//
// Precondition: matchLabels.size() == matchPaths.size().
// Returns 0, or nonzero if the label count does not match.
int showResults(const std::string& targetPath, const std::vector<std::string>& matchPaths,
                const std::vector<std::string>& matchLabels, const std::string& title,
                int thumbSize = 300);

#endif
