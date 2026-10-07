#include <display.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace {
const int kMargin = 120;
const int kGap = 60;
const int kTargetGap = 120;  // space between the target row and the matches grid
const int kColumns = 5;      // matches per row before wrapping
const int kMinCanvasWidth = 1300;  // so the settings title line is never clipped
const int kMaxWindowWidth = 1800;  // wider canvases are scaled down to fit the screen
const int kViewportHeight = 950;   // taller canvases scroll vertically
const int kScrollStep = 80;        // pixels per wheel notch or arrow press

// Windows virtual-key codes as returned by cv::waitKeyEx
const int kKeyUp = 2490368;
const int kKeyDown = 2621440;
const int kKeyPageUp = 2162688;
const int kKeyPageDown = 2228224;
const int kKeyHome = 2359296;
const int kKeyEnd = 2293760;

const double kLabelScale = 0.7;
const int kLabelThickness = 2;
const int kLineHeight = 34;
const cv::Scalar kTextColor(235, 235, 235);
const cv::Scalar kTitleColor(120, 220, 255);  // BGR: amber, so the settings line stands out

// A failed load becomes a black tile, so tile i still lines up with rank i
cv::Mat loadTile(const std::string& path, int size) {
    cv::Mat image = cv::imread(path, cv::IMREAD_COLOR);
    if (image.empty()) {
        std::cerr << path << ": could not read image for display\n";
        return cv::Mat::zeros(size, size, CV_8UC3);
    }
    cv::Mat tile;
    cv::resize(image, tile, cv::Size(size, size));
    return tile;
}

// putText has no newline support, so draw one line at a time
void drawLines(cv::Mat& canvas, const std::string& text, int x, int y, double scale,
               const cv::Scalar& color) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        cv::putText(canvas, line, cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX, scale, color,
                    kLabelThickness, cv::LINE_AA);
        y += kLineHeight;
    }
}

// highgui has no scrollbars, so show a viewport onto the canvas and move it ourselves
struct ScrollState {
    int offset = 0;
    int maxOffset = 0;
};

void scrollBy(ScrollState& state, int delta) {
    state.offset = std::clamp(state.offset + delta, 0, state.maxOffset);
}

void onMouse(int event, int, int, int flags, void* userdata) {
    if (event == cv::EVENT_MOUSEWHEEL) {
        // Wheel delta is positive when scrolling up; one notch is +-120
        const int notches = cv::getMouseWheelDelta(flags) / 120;
        scrollBy(*static_cast<ScrollState*>(userdata), -notches * kScrollStep);
    }
}

// Blocks until q / Esc is pressed or the window is closed. Closing via the X button is
// detected too, so the process can't linger and keep query.exe locked.
void scrollView(const std::string& windowName, const cv::Mat& canvas) {
    const int viewHeight = std::min(canvas.rows, kViewportHeight);
    ScrollState state;
    state.maxOffset = canvas.rows - viewHeight;

    cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);
    cv::setMouseCallback(windowName, onMouse, &state);

    for (;;) {
        cv::Mat view = canvas(cv::Rect(0, state.offset, canvas.cols, viewHeight)).clone();

        // Thin position bar on the right edge, only when there is something to scroll
        if (state.maxOffset > 0) {
            const int barHeight = std::max(30, viewHeight * viewHeight / canvas.rows);
            const int barTop = state.offset * (viewHeight - barHeight) / state.maxOffset;
            cv::rectangle(view, cv::Rect(canvas.cols - 10, barTop, 6, barHeight),
                          cv::Scalar(160, 160, 160), cv::FILLED);
        }
        cv::imshow(windowName, view);

        const int key = cv::waitKeyEx(30);
        if (key == 'q' || key == 27) {
            break;
        }
        // Windows virtual-key codes as returned by waitKeyEx
        if (key == kKeyUp) {
            scrollBy(state, -kScrollStep);
        } else if (key == kKeyDown) {
            scrollBy(state, kScrollStep);
        } else if (key == kKeyPageUp) {
            scrollBy(state, -viewHeight * 9 / 10);
        } else if (key == kKeyPageDown) {
            scrollBy(state, viewHeight * 9 / 10);
        } else if (key == kKeyHome) {
            scrollBy(state, -state.maxOffset);
        } else if (key == kKeyEnd) {
            scrollBy(state, state.maxOffset);
        }

        if (cv::getWindowProperty(windowName, cv::WND_PROP_VISIBLE) < 1) {
            break;
        }
    }
}
}  // namespace

int showResults(const std::string& targetPath, const std::vector<std::string>& matchPaths,
                const std::vector<std::string>& matchLabels, const std::string& title,
                int thumbSize) {
    if (matchLabels.size() != matchPaths.size()) {
        return 1;
    }
    const std::string windowName = "Results";

    // Layout: the target alone on top, then the matches in rank order wrapping left-to-right,
    // top-to-bottom, so a long list becomes a grid instead of one unreadably wide strip.
    const int matchCount = static_cast<int>(matchPaths.size());
    const int cols = std::max(1, std::min(kColumns, matchCount));
    const int matchRows = (matchCount + cols - 1) / cols;
    const int cellHeight = thumbSize + 2 * kLineHeight + 20;  // image + two label lines

    const int gridWidth = cols * thumbSize + (cols - 1) * kGap;
    const int width = std::max(kMinCanvasWidth, 2 * kMargin + gridWidth);
    const int gridHeight = matchRows > 0 ? matchRows * cellHeight + (matchRows - 1) * kGap : 0;
    const int height = kMargin + cellHeight + (matchRows > 0 ? kTargetGap + gridHeight : 0) +
                       kMargin / 2;
    cv::Mat canvas(height, width, CV_8UC3, cv::Scalar(32, 32, 32));

    drawLines(canvas, title, kMargin, kMargin / 2 + 10, kLabelScale + 0.1, kTitleColor);

    // Draws one tile with its label underneath at the cell's top-left corner
    auto place = [&](const std::string& path, const std::string& label, int x, int y) {
        loadTile(path, thumbSize).copyTo(canvas(cv::Rect(x, y, thumbSize, thumbSize)));
        drawLines(canvas, label, x, y + thumbSize + kLineHeight, kLabelScale, kTextColor);
    };

    place(targetPath, "TARGET\n" + std::filesystem::path(targetPath).filename().string(),
          kMargin, kMargin);

    const int gridTop = kMargin + cellHeight + kTargetGap;
    for (int i = 0; i < matchCount; i++) {
        const int col = i % cols;
        const int row = i / cols;
        place(matchPaths[static_cast<std::size_t>(i)], matchLabels[static_cast<std::size_t>(i)],
              kMargin + col * (thumbSize + kGap), gridTop + row * (cellHeight + kGap));
    }

    // Shrink to fit the screen width only; height is handled by scrolling
    if (width > kMaxWindowWidth) {
        const double s = static_cast<double>(kMaxWindowWidth) / width;
        cv::resize(canvas, canvas, cv::Size(), s, s, cv::INTER_AREA);
    }

    scrollView(windowName, canvas);
    cv::destroyAllWindows();
    return 0;
}
