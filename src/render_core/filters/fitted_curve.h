//
// Created by Zero on 2023/7/7.
//

#pragma once

#include "math/warp.h"
#include "base/warper.h"
#include "base/mgr/global.h"
#include "base/sensor/filter.h"
#include "base/mgr/pipeline.h"
#include "rhi/common.h"

namespace vision {
using namespace ocarina;
class FilterSampler : public Context {
public:
    static constexpr int table_size = 20;

private:
    SP<Warper2D> warper_{};
    RegistrableManaged<float> lut_{pipeline()->bindless_array()};

public:
    FilterSampler()
        : warper_(scene().load_warper2d()) {}

    void prepare(const Filter *filter) {
        int len = ocarina::sqr(table_size);
        lut_.resize(len);
        vector<float> func;
        func.resize(len);
        float2 r = filter->radius<H>();
        for (int i = 0; i < len; ++i) {
            int x = i % table_size;
            int y = i / table_size;
            float2 p = make_float2((x + 0.5f) / table_size * r.x,
                                   (y + 0.5f) / table_size * r.y);
            float val = filter->evaluate(p);
            func[i] = ocarina::abs(val);
            lut_.host_buffer().at(i) = val;
        }
        float sum = std::accumulate(lut_.begin(), lut_.end(), 0.f);
        float integral = sum / len;
        std::transform(lut_.cbegin(), lut_.cend(), lut_.begin(), [&](float val) {
            return val / integral;
        });
        lut_.reset_device_buffer_immediately(filter->device());
        lut_.upload_immediately();
        lut_.register_self();
        warper_->build(ocarina::move(func), make_uint2(table_size));
        warper_->prepare();
    }

    template<typename X, typename Y>
    requires is_all_integral_expr_v<X, Y>
    [[nodiscard]] Float lut_at(const X &x, const Y &y) const noexcept {
        Var index = y * table_size + x;
        return lut_.read(index);
    }

    template<typename V2>
    [[nodiscard]] Float lut_at(const V2 &v) const noexcept {
        return lut_at(v.x, v.y);
    }

    [[nodiscard]] FilterSample sample(Float2 u) const noexcept {
        u = u * 2.f - make_float2(1.f);
        Float pdf = 0.f;
        Uint2 offset;
        Float2 p = warper_->sample_continuous(abs(u), std::addressof(pdf), std::addressof(offset));
        p = p * sign(u);
        return FilterSample{p, lut_at(offset) / pdf};
    }
};

class FittedCurveFilter : public Filter {
protected:
    FilterSampler sampler_{};

public:
    explicit FittedCurveFilter(const FilterDesc &desc)
        : Filter(desc) {}

    void prepare() noexcept override {
        sampler_.prepare(this);
    }

    [[nodiscard]] FilterSample sample(Float2 u) const noexcept override {
        FilterSample fs = sampler_.sample(u);
        fs.p = fs.p * radius();
        return fs;
    }
};

}// namespace vision