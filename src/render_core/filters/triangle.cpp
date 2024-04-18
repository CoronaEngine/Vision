//
// Created by Zero on 22/02/2023.
//

#include "descriptions/node_desc.h"
#include "base/sensor/filter.h"
#include "math/warp.h"

namespace vision {

class TriangleFilter : public Filter {
public:
    explicit TriangleFilter(const FilterDesc &desc) : Filter(desc) {}
    [[nodiscard]] FilterSample sample(Float2 u) const noexcept override {
        return {make_float2(sample_tent(u.x, radius().x), sample_tent(u.y, radius().y)), 1.f};
    }
    VS_MAKE_PLUGIN_NAME_FUNC
    [[nodiscard]] float evaluate(float2 p) const noexcept override {
        return std::max(0.f, radius_.hv().x - std::abs(p.x)) *
               std::max(0.f, radius_.hv().y - std::abs(p.y));
    }
};
}// namespace vision

VS_MAKE_CLASS_CREATOR(vision::TriangleFilter)