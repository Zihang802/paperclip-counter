// -----------------------------------------------------------------------------
//  回形针自动计数（Paperclip Counter）
//
//  思路：
//    1. 灰度 + 高斯模糊，压掉传感器噪点
//    2. Otsu 自动阈值（反相），把暗的钢丝从亮的背景里分出来
//    3. 闭运算，把钢丝上被反光/细丝打断的小缺口连起来
//    4. 连通域分析：一个回形针的钢丝是一条连续的线，正好是一个连通域
//    5. 按面积和“是否贴图像边缘”过滤掉边框、灰尘等伪目标
//
//  编译：见 README.md（CMake 或 VS2022 手配都行）
//  运行：paperclip_counter [输入图片] [输出图片] [--debug]
// -----------------------------------------------------------------------------
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Windows 下 cv::imread 内部用的是 ANSI 路径，遇到中文目录会直接失败。
// 这里统一改成“二进制读入 + imdecode”，中文、空格路径都没问题。
cv::Mat readImage(const std::string& path)
{
    std::ifstream fs(path, std::ios::binary);
    if (!fs) {
        std::cerr << "无法打开图片: " << path << "\n";
        return {};
    }
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(fs)),
                                   std::istreambuf_iterator<char>());
    if (buf.empty()) {
        std::cerr << "图片内容为空: " << path << "\n";
        return {};
    }
    return cv::imdecode(buf, cv::IMREAD_COLOR);
}

bool writeImage(const std::string& path, const cv::Mat& img)
{
    std::error_code ec;
    fs::path p(path);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path(), ec);  // 目录不存在就建一个
    }
    std::vector<unsigned char> buf;
    if (!cv::imencode(".png", img, buf)) {
        return false;
    }
    std::ofstream fs(path, std::ios::binary);
    if (!fs) {
        return false;
    }
    fs.write(reinterpret_cast<const char*>(buf.data()),
             static_cast<std::streamsize>(buf.size()));
    return fs.good();
}

struct Params {
    int  blurSize  = 5;    // 高斯核，必须奇数
    int  closeSize = 5;    // 闭运算核
    int  closeIter = 2;    // 闭运算次数
    int  minArea   = 300;  // 小于这个面积的连通域当噪点丢掉
    bool debug     = false;
};

struct Blob {
    cv::Rect box;
    int      area = 0;
};

// 返回检测到的回形针数量，同时输出标注图和二值掩膜
int countPaperclips(const cv::Mat& src, const Params& prm,
                    cv::Mat& annotated, cv::Mat& maskOut)
{
    cv::Mat gray, blurred, binary, closed;

    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, blurred, cv::Size(prm.blurSize, prm.blurSize), 0);

    // 背景亮、回形针暗 -> 反相 Otsu，阈值由直方图自动决定，不用手调
    double otsu = cv::threshold(blurred, binary, 0, 255,
                                cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
    std::cout << "Otsu 阈值: " << otsu << "\n";

    // 闭运算：钢丝被反光切成好几段时，这一步能把它们重新接上
    cv::Mat kernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE, cv::Size(prm.closeSize, prm.closeSize));
    cv::morphologyEx(binary, closed, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1),
                     prm.closeIter);

    cv::Mat labels, stats, centroids;
    int n = cv::connectedComponentsWithStats(closed, labels, stats, centroids, 8, CV_32S);

    // ---- 过滤伪目标 ----
    std::vector<Blob> blobs;
    for (int i = 1; i < n; ++i) {  // 0 是背景
        int area = stats.at<int>(i, cv::CC_STAT_AREA);
        int x    = stats.at<int>(i, cv::CC_STAT_LEFT);
        int y    = stats.at<int>(i, cv::CC_STAT_TOP);
        int w    = stats.at<int>(i, cv::CC_STAT_WIDTH);
        int h    = stats.at<int>(i, cv::CC_STAT_HEIGHT);

        if (area < prm.minArea) {
            continue;  // 灰尘、噪点
        }
        // 贴边的多半是镜头暗角/边框线，不是回形针
        if (x <= 1 || y <= 1 || x + w >= closed.cols - 1 || y + h >= closed.rows - 1) {
            if (prm.debug) {
                std::cout << "  丢弃贴边连通域: (" << x << "," << y << ") "
                          << w << "x" << h << " area=" << area << "\n";
            }
            continue;
        }
        blobs.push_back({cv::Rect(x, y, w, h), area});
    }

    // 按“从上到下、从左到右”排序，编号看起来更自然
    std::sort(blobs.begin(), blobs.end(), [](const Blob& a, const Blob& b) {
        int rowA = a.box.y / 80;
        int rowB = b.box.y / 80;
        if (rowA != rowB) return rowA < rowB;
        return a.box.x < b.box.x;
    });

    // ---- 画结果 ----
    annotated = src.clone();
    for (size_t i = 0; i < blobs.size(); ++i) {
        const cv::Rect& r = blobs[i].box;
        cv::rectangle(annotated, r, cv::Scalar(0, 0, 255), 2);
        cv::circle(annotated, cv::Point(r.x + r.width / 2, r.y + r.height / 2), 4,
                   cv::Scalar(255, 0, 0), cv::FILLED);

        std::string idx = std::to_string(i + 1);
        cv::Point org(r.x, std::max(r.y - 8, 16));
        cv::putText(annotated, idx, org, cv::FONT_HERSHEY_SIMPLEX, 0.7,
                    cv::Scalar(0, 160, 0), 2, cv::LINE_AA);
    }

    std::string summary = "count = " + std::to_string(blobs.size());
    cv::putText(annotated, summary, cv::Point(12, annotated.rows - 18),
                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);

    maskOut = closed;
    return static_cast<int>(blobs.size());
}

void printUsage(const char* exe)
{
    std::cout << "用法: " << exe << " [输入图片] [输出图片] [--debug]\n"
              << "  默认输入: images/paperclips.png\n"
              << "  默认输出: results/annotated.png\n";
}

}  // namespace

int main(int argc, char** argv)
{
    std::string input  = "images/paperclips.png";
    std::string output = "results/annotated.png";
    Params prm;

    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") {
            prm.debug = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.size() >= 1) input  = positional[0];
    if (positional.size() >= 2) output = positional[1];

    cv::Mat src = readImage(input);
    if (src.empty()) {
        std::cerr << "读取失败，检查路径是否正确。\n";
        return 1;
    }
    std::cout << "图片: " << input << "  (" << src.cols << "x" << src.rows << ")\n";

    cv::Mat annotated, mask;
    int count = countPaperclips(src, prm, annotated, mask);

    if (!writeImage(output, annotated)) {
        std::cerr << "写结果图失败: " << output << "\n";
        return 1;
    }
    std::cout << "回形针数量: " << count << "\n";
    std::cout << "结果图已保存: " << output << "\n";

    if (prm.debug) {
        fs::path outPath(output);
        std::string maskPath =
            (outPath.parent_path() / (outPath.stem().string() + "_mask.png")).string();
        writeImage(maskPath, mask);
        std::cout << "二值掩膜已保存: " << maskPath << "\n";
    }
    return 0;
}
