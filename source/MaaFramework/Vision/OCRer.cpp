#include "OCRer.h"

#include <regex>

#include "Resource/ResourceMgr.h"
#include "Utils/ImageIo.h"
#include "Utils/Logger.h"
#include "Utils/Ranges.hpp"
#include "Utils/StringMisc.hpp"

MAA_VISION_NS_BEGIN

std::ostream& operator<<(std::ostream& os, const OCRer::Result& res)
{
    os << VAR_RAW(res.text) << VAR_RAW(res.box) << VAR_RAW(res.score);
    return os;
}

OCRer::ResultOpt OCRer::analyze() const
{
    auto results = traverse_rois();

    if (results.empty()) {
        return std::nullopt;
    }

    for (auto iter = results.begin(); iter != results.end();) {
        auto& res = *iter;

        postproc_trim_(res);
        postproc_replace_(res);

        if (!filter_by_required(res)) {
            iter = results.erase(iter);
            continue;
        }

        ++iter;
    }

    LogDebug << name_ << VAR(results) << VAR(param_.text);

    return results.empty() ? std::nullopt : std::make_optional(std::move(results));
}

OCRer::ResultsVec OCRer::traverse_rois() const
{
    if (!cache_.empty()) {
        return { predict_only_rec(cache_) };
    }

    if (param_.roi.empty()) {
        cv::Rect roi(0, 0, image_.cols, image_.rows);
        return predict(roi);
    }

    ResultsVec results;
    for (const cv::Rect& roi : param_.roi) {
        auto cur = predict(roi);
        results.insert(results.end(), std::make_move_iterator(cur.begin()), std::make_move_iterator(cur.end()));
    }
    return results;
}

OCRer::ResultsVec OCRer::predict(const cv::Rect& roi) const
{
    return param_.only_rec ? ResultsVec { predict_only_rec(roi) } : predict_det_and_rec(roi);
}

OCRer::ResultsVec OCRer::predict_det_and_rec(const cv::Rect& roi) const
{
    if (!resource()) {
        LogError << "Resource not binded";
        return {};
    }

    auto& inferencer = resource()->ocr_cfg().ocrer();
    if (!inferencer) {
        LogError << "resource()->ocr_cfg().ocrer() is null";
        return {};
    }
    auto start_time = std::chrono::steady_clock::now();

    auto image_roi = image_with_roi(roi);

    fastdeploy::vision::OCRResult ocr_result;
    bool ret = inferencer->Predict(image_roi, &ocr_result);
    if (!ret) {
        LogWarn << "inferencer return false" << VAR(inferencer) << VAR(image_) << VAR(roi) << VAR(image_roi);
        return {};
    }
    if (ocr_result.boxes.size() != ocr_result.text.size() || ocr_result.text.size() != ocr_result.rec_scores.size()) {
        LogError << "wrong ocr_result size" << VAR(ocr_result.boxes) << VAR(ocr_result.boxes.size())
                 << VAR(ocr_result.text) << VAR(ocr_result.text.size()) << VAR(ocr_result.rec_scores)
                 << VAR(ocr_result.rec_scores.size());
        return {};
    }

    ResultsVec results;

    cv::Mat image_draw;
    if (debug_draw_) {
        image_draw = draw_roi(roi);
    }

    for (size_t i = 0; i != ocr_result.text.size(); ++i) {
        // the raw_box rect like ↓
        // 0 - 1
        // 3 - 2
        const auto& raw_box = ocr_result.boxes.at(i);
        int x_collect[] = { raw_box[0], raw_box[2], raw_box[4], raw_box[6] };
        int y_collect[] = { raw_box[1], raw_box[3], raw_box[5], raw_box[7] };
        auto [left, right] = MAA_RNS::ranges::minmax(x_collect);
        auto [top, bottom] = MAA_RNS::ranges::minmax(y_collect);

        cv::Rect my_box(left + roi.x, top + roi.y, right - left, bottom - top);
        if (debug_draw_) {
            const auto color = cv::Scalar(0, 0, 255);
            cv::rectangle(image_draw, my_box, color, 1);
            std::string flag =
                MAA_FMT::format("{}: [{}, {}, {}, {}]", i, my_box.x, my_box.y, my_box.width, my_box.height);
            cv::putText(image_draw, flag, cv::Point(my_box.x, my_box.y - 5), cv::FONT_HERSHEY_PLAIN, 1.2, color, 1);
        }
        results.emplace_back(
            Result { .text = std::move(ocr_result.text.at(i)), .box = my_box, .score = ocr_result.rec_scores.at(i) });
    }

    auto costs = duration_since(start_time);
    LogDebug << VAR(results) << VAR(image_roi.size()) << VAR(costs);

    if (save_draw_) {
        save_image(image_draw);
    }

    return results;
}

OCRer::Result OCRer::predict_only_rec(const cv::Rect& roi) const
{
    if (!resource()) {
        LogError << "Resource not binded";
        return {};
    }

    auto& inferencer = resource()->ocr_cfg().recer();
    if (!inferencer) {
        LogError << "resource()->ocr_cfg().recer() is null";
        return {};
    }
    auto start_time = std::chrono::steady_clock::now();

    auto image_roi = image_with_roi(roi);

    std::string rec_text;
    float rec_score = 0;

    bool ret = inferencer->Predict(image_roi, &rec_text, &rec_score);
    if (!ret) {
        LogWarn << "inferencer return false" << VAR(inferencer) << VAR(image_) << VAR(roi) << VAR(image_roi);
        return {};
    }

    cv::Mat image_draw;
    if (debug_draw_) {
        image_draw = draw_roi(roi);
    }

    Result result { .text = std::move(rec_text), .box = roi, .score = rec_score };

    auto costs = duration_since(start_time);
    LogDebug << VAR(result) << VAR(image_roi.size()) << VAR(costs);

    if (save_draw_) {
        save_image(image_draw);
    }

    return result;
}

void OCRer::postproc_trim_(Result& res) const
{
    string_trim_(res.text);
}

void OCRer::postproc_replace_(Result& res) const
{
    for (const auto& [regex, new_str] : param_.replace) {
        res.text = std::regex_replace(res.text, std::regex(regex), new_str);
    }
}

bool OCRer::filter_by_required(const Result& res) const
{
    if (param_.text.empty()) {
        return true;
    }

    for (const auto& text : param_.text) {
        if (std::regex_search(res.text, std::regex(text))) {
            return true;
        }
    }

    return false;
}

MAA_VISION_NS_END
