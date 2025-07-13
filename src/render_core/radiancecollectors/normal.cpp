//
// Created by Zero on 13/10/2022.
//

#include "base/sensor/radiance_collector.h"
#include "base/mgr/pipeline.h"
#include "math/base.h"

namespace vision {
using namespace ocarina;

class NormalRadianceCollector : public RadianceCollector {
private:
    RegistrableManaged<float4> rt_buffer_;
    RegistrableManaged<float4> accumulation_buffer_;
    SP<ScreenBuffer> output_buffer_{make_shared<ScreenBuffer>(FrameBuffer::final_result)};

public:
    NormalRadianceCollector() = default;
    explicit NormalRadianceCollector(const Desc &desc)
        : RadianceCollector(desc),
          rt_buffer_(pipeline()->bindless_array()) {}

    OC_ENCODABLE_FUNC(RadianceCollector, rt_buffer_, accumulation_buffer_, output_buffer_)
    VS_MAKE_PLUGIN_NAME_FUNC
    VS_HOTFIX_MAKE_RESTORE(RadianceCollector, rt_buffer_, accumulation_buffer_, output_buffer_)

    void on_resize(uint2 res) noexcept override {
        prepare_buffers();
    }

    bool render_UI(ocarina::Widgets *widgets) noexcept override {
        tone_mapper_->render_UI(widgets);
        return widgets->use_folding_header(ocarina::format("{} rad_collector", impl_type().data()), [&] {
            render_sub_UI(widgets);
        });
    }

    void render_sub_UI(ocarina::Widgets *widgets) noexcept override {
        RadianceCollector::render_sub_UI(widgets);
        changed_ |= widgets->check_box("accumulate", reinterpret_cast<bool *>(addressof(accumulation_.hv())));
    }

    void prepare_buffers() noexcept {
        auto prepare = [&](RegistrableManaged<float4> &managed, const string &desc = "") noexcept {
            managed.set_bindless_array(pipeline()->bindless_array());
            managed.reset_all(device(), pixel_num(), desc);
            managed.reset_immediately();
            managed.register_self();
        };
        prepare(rt_buffer_, "RGBFilm::rt_buffer_");
        prepare(accumulation_buffer_, "RGBFilm::accumulation_buffer_");
    }
    void prepare() noexcept override {
        prepare_buffers();
        frame_buffer().prepare_screen_buffer(output_buffer_);
    }
    Float3 add_sample(const Uint2 &pixel, Float4 val, const Uint &frame_index) noexcept override {
        Float a = 1.f / (frame_index + 1);
        Uint index = dispatch_id(pixel);
        val = Env::instance().zero_if_nan_inf(val);
        if (accumulation_.hv()) {
            Float4 accum_prev = rt_buffer_.read(index);
            val = lerp(make_float4(a), accum_prev, val);
        }
        rt_buffer_.write(index, val);
        val = apply_exposure(val);
        val = tone_mapper_->apply(val);
        val.w = 1.f;
        output_buffer_->write(index, val);
        return val.xyz();
    }

    [[nodiscard]] const RegistrableManaged<float4> &output_buffer() const noexcept override { return *output_buffer_; }
    [[nodiscard]] RegistrableManaged<float4> &output_buffer() noexcept override { return *output_buffer_; }
    [[nodiscard]] const RegistrableManaged<float4> &rt_buffer() const noexcept override { return rt_buffer_; }
    [[nodiscard]] RegistrableManaged<float4> &rt_buffer() noexcept override { return rt_buffer_; }
};

}// namespace vision

VS_MAKE_CLASS_CREATOR_HOTFIX(vision, NormalRadianceCollector)