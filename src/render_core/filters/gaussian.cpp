//
// Created by Zero on 2023/7/7.
//

#include "fitted_curve.h"
#include "math/util.h"

namespace vision {

class GaussianFilter : public FittedCurveFilter {
private:
    float sigma_{};
    float exp_x_{};
    float exp_y_{};

public:
    GaussianFilter() = default;
    VS_HOTFIX_MAKE_RESTORE(FittedCurveFilter, sigma_, exp_x_, exp_y_)
    explicit GaussianFilter(const FilterDesc &desc)
        : FittedCurveFilter(desc),
          sigma_(desc["sigma"].as_float(1.f)),
          exp_x_(gaussian<H>(radius_.hv().x, 0, sigma_)),
          exp_y_(gaussian<H>(radius_.hv().y, 0, sigma_)) {}
    VS_MAKE_PLUGIN_NAME_FUNC
    void render_sub_UI(ocarina::Widgets *widgets) noexcept override {
        changed_ |= widgets->drag_float("sigma", &sigma_, 0.01, 0.01, 5);
    }
    [[nodiscard]] float evaluate(ocarina::float2 p) const noexcept override {
        float vx = gaussian<H>(p.x, 0, sigma_) - exp_x_;
        float vy = gaussian<H>(p.y, 0, sigma_) - exp_y_;
        return ocarina::max(0.f, vx) * ocarina::max(0.f, vy);
    }
};

}// namespace vision

VS_MAKE_CLASS_CREATOR_HOTFIX(vision, GaussianFilter)
VS_REGISTER_CURRENT_PATH(0, "vision-filter-gaussian.dll")