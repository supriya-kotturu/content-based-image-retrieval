/*
  Author:  Sai Supriya Kotturu
  Date:    2026-10-06
  Purpose: Declares showResults, the OpenCV window query uses to display the target image and its
           matches with labels.
*/

#ifndef DISPLAY_H
#define DISPLAY_H

#include <string>
#include <vector>

// Shows the target on its own row, then the matches in rank order wrapping into a grid, in one
// window. The window scrolls vertically (mouse wheel, arrow keys, PageUp/PageDown, Home/End) and
// blocks until q or Esc is pressed or the window is closed.
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
