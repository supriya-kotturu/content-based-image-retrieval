/*
  Author:  Sai Supriya Kotturu
  Date:    2026-09-21
  Purpose: Sobel X, Sobel Y and gradient magnitude, copied from Project 1's filters.cpp and
           generalized to work on 1-channel (grayscale) as well as 3-channel (BGR) images
*/

#include <sobel.h>

#include <cmath>

// Sobel X (project 1 - task 7) as separable 1x3 filters, positive to the right. src is CV_8UC1
// or CV_8UC3, dest becomes CV_16SC1 or CV_16SC3 in [-255, 255]. Returns 0 on success.
int sobelX3x3(cv::Mat& src, cv::Mat& dest) {
    // 1 channel (grayscale) or 3 channels (BGR)
    CV_Assert(src.type() == CV_8UC1 || src.type() == CV_8UC3);

    const int rows = src.rows;
    const int cols = src.cols;
    // pixels are indexed as [c * chans + k] so one loop serves both layouts
    const int chans = src.channels();
    const int outType = CV_MAKETYPE(CV_16S, chans);

    // separable Sobel X = [1 2 1]^T (vertical smooth) x [-1 0 1] (horizontal
    // difference). right - left, so positive when brightness increases rightward
    static const int h[3] = {-1, 0, 1};
    static const int v[3] = {1, 2, 1};

    // output is signed: edges can be dark->bright (+) or bright->dark (-).
    // zeros leaves the 1-pixel border at 0 (not required by the spec)
    dest = cv::Mat::zeros(src.size(), outType);

    // horizontal difference results are in [-255, 255]
    cv::Mat temp = cv::Mat::zeros(src.size(), outType);

    // horizontal pass: every row, because the vertical pass reads temp rows
    // r-1..r+1 for dest rows 1..rows-2, i.e. all rows 0..rows-1
    for (int r = 0; r < rows; r++) {
        const uchar* srcRow = src.ptr<uchar>(r);
        short* tRow = temp.ptr<short>(r);

        for (int c = 1; c < cols - 1; c++) {
            for (int k = 0; k < chans; k++) {
                int sum = 0;
                for (int j = 0; j < 3; j++) {
                    sum += h[j] * srcRow[(c + j - 1) * chans + k];
                }
                tRow[c * chans + k] = static_cast<short>(sum);
            }
        }
    }

    // vertical pass: 3 row pointers set once per row, not per pixel
    for (int r = 1; r < rows - 1; r++) {
        const short* tRows[3];
        for (int i = 0; i < 3; i++) {
            tRows[i] = temp.ptr<short>(r + i - 1);
        }
        short* destRow = dest.ptr<short>(r);

        for (int c = 1; c < cols - 1; c++) {
            for (int k = 0; k < chans; k++) {
                int sum = 0;  // [1 2 1] sums to 4, so range is [-1020, 1020]
                for (int i = 0; i < 3; i++) {
                    sum += v[i] * tRows[i][c * chans + k];
                }
                // divide by the smoothing weight to bring it back to [-255, 255]
                destRow[c * chans + k] = static_cast<short>(sum / 4);
            }
        }
    }

    return 0;
}

// Sobel Y (project 1 - task 7) as separable 1x3 filters, positive up. src is CV_8UC1 or
// CV_8UC3, dest becomes CV_16SC1 or CV_16SC3 in [-255, 255]. Returns 0 on success.
int sobelY3x3(cv::Mat& src, cv::Mat& dest) {
    // 1 channel (grayscale) or 3 channels (BGR)
    CV_Assert(src.type() == CV_8UC1 || src.type() == CV_8UC3);

    const int rows = src.rows;
    const int cols = src.cols;
    const int chans = src.channels();
    const int outType = CV_MAKETYPE(CV_16S, chans);

    // separable Sobel Y = [1 0 -1]^T (vertical difference) x [1 2 1]
    // (horizontal smooth). Row indices grow downward, so "positive up" means
    // above - below: the row above (tRows[0]) gets +1.
    static const int h[3] = {1, 2, 1};
    static const int v[3] = {1, 0, -1};

    // output is signed: brighter above (+) or brighter below (-).
    // zeros leaves the 1-pixel border at 0 (not required by the spec)
    dest = cv::Mat::zeros(src.size(), outType);

    // horizontal smoothing results are in [0, 1020] (weights sum to 4)
    cv::Mat temp = cv::Mat::zeros(src.size(), outType);

    // horizontal pass: every row, because the vertical pass reads temp rows
    // r-1..r+1 for dest rows 1..rows-2, i.e. all rows 0..rows-1
    for (int r = 0; r < rows; r++) {
        const uchar* srcRow = src.ptr<uchar>(r);
        short* tRow = temp.ptr<short>(r);

        for (int c = 1; c < cols - 1; c++) {
            for (int k = 0; k < chans; k++) {
                int sum = 0;
                for (int j = 0; j < 3; j++) {
                    sum += h[j] * srcRow[(c + j - 1) * chans + k];
                }
                tRow[c * chans + k] = static_cast<short>(sum);
            }
        }
    }

    // vertical pass: 3 row pointers set once per row, not per pixel
    for (int r = 1; r < rows - 1; r++) {
        const short* tRows[3];
        for (int i = 0; i < 3; i++) {
            tRows[i] = temp.ptr<short>(r + i - 1);
        }
        short* destRow = dest.ptr<short>(r);

        for (int c = 1; c < cols - 1; c++) {
            for (int k = 0; k < chans; k++) {
                int sum = 0;  // above - below of values in [0, 1020]: [-1020, 1020]
                for (int i = 0; i < 3; i++) {
                    sum += v[i] * tRows[i][c * chans + k];
                }
                // divide by the horizontal smoothing weight (4) -> [-255, 255]
                destRow[c * chans + k] = static_cast<short>(sum / 4);
            }
        }
    }

    return 0;
}

// Gradient magnitude (task 8): per channel sqrt(sx^2 + sy^2), clamped to 255.
// sx/sy are CV_16SC1 or CV_16SC3 Sobel outputs of the same type, dest becomes CV_8UC1 or
// CV_8UC3. Returns 0 on success.
int magnitude(cv::Mat& sx, cv::Mat& sy, cv::Mat& dest) {
    // both inputs must be signed Sobel outputs of the same size and channel count
    CV_Assert(sx.type() == CV_16SC1 || sx.type() == CV_16SC3);
    CV_Assert(sx.type() == sy.type());
    CV_Assert(sx.size() == sy.size());

    const int chans = sx.channels();
    dest.create(sx.size(), CV_MAKETYPE(CV_8U, chans));

    for (int r = 0; r < sx.rows; r++) {
        const short* sxRow = sx.ptr<short>(r);
        const short* syRow = sy.ptr<short>(r);
        uchar* destRow = dest.ptr<uchar>(r);

        for (int c = 0; c < sx.cols; c++) {
            for (int k = 0; k < chans; k++) {
                // squares reach 2 * 255^2 = 130050, fine in int
                int x = sxRow[c * chans + k];
                int y = syRow[c * chans + k];

                // max is 255 * sqrt(2) ~ 360. Clamping (not scaling) keeps typical
                // edges at full brightness; only strong diagonals saturate at 255.
                destRow[c * chans + k] =
                    cv::saturate_cast<uchar>(std::sqrt(static_cast<float>(x * x + y * y)));
            }
        }
    }

    return 0;
}
