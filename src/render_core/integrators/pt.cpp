//
// Created by Zero on 12/09/2022.
//

#include "base/integral/integrator.h"
#include "base/mgr/pipeline.h"
#include "math/warp.h"
#include "base/color/spectrum.h"
#include "adaptive/inspector.h"

namespace vision {
using namespace ocarina;
class PathTracingIntegrator : public IlluminationIntegrator, public Observer {
private:
    SP<ConvergenceInspector> inspector_;

public:
    PathTracingIntegrator() = default;
    explicit PathTracingIntegrator(const IntegratorDesc &desc)
        : IlluminationIntegrator(desc),
          inspector_(make_shared<ConvergenceInspector>(desc.value("adaptive"))) {}
    VS_MAKE_PLUGIN_NAME_FUNC
    void render_sub_UI(ocarina::Widgets *widgets) noexcept override {
        inspector_->render_UI(widgets);
    }

    void prepare() noexcept override {
        IlluminationIntegrator::prepare();
        inspector_->prepare();
        frame_buffer().prepare_hit_buffer();
        frame_buffer().prepare_gbuffer();
        frame_buffer().prepare_albedo();
        frame_buffer().prepare_emission();
        frame_buffer().prepare_normal();
        frame_buffer().prepare_motion_vectors();
        frame_buffer().prepare_accumulation_buffer();
    }
    VS_HOTFIX_MAKE_RESTORE(IlluminationIntegrator, inspector_)
    OC_ENCODABLE_FUNC(IlluminationIntegrator, inspector_)
    VS_MAKE_GUI_STATUS_FUNC(IlluminationIntegrator, inspector_)
    void update_runtime_object(const vision::IObjectConstructor *constructor) noexcept override {
        std::tuple tp = {addressof(inspector_)};
        HotfixSystem::replace_objects(constructor, tp);
    }
    
    void add_sample(const Uint2 &pixel, Float3 val, const Uint &frame_index) noexcept {
        val = frame_buffer().add_sample(pixel, val, frame_index);
        if (inspector_->on()) {
            inspector_->add_sample(pixel, val, frame_index);
        }
    }

    void compile() noexcept override {
        TSensor &camera = scene().sensor();
        TSampler &sampler = scene().sampler();
        ocarina::Kernel<signature> kernel = [&](Uint frame_index) -> void {
            Env::instance().clear_global_vars();
            Uint2 pixel = dispatch_idx().xy();
            RenderEnv render_env;
            sampler->load_data();
            camera->load_data();
            frame_buffer().load_data();
            load_data();
            if (inspector_->on()) {
                $if(inspector_->is_convergence(frame_index)) {
                    return_();
                };
                $condition_info("is convergence {}", inspector_->is_convergence(frame_index).cast<uint>());
            }
            render_env.initial(sampler, frame_index, spectrum());
            sampler->start(pixel, frame_index, 0);
            SensorSample ss = sampler->sensor_sample(pixel, camera->filter());
            Float scatter_pdf = 1e16f;
            RayState rs = camera->generate_ray(ss);
            Float3 L = Li(rs, scatter_pdf, *max_depth_, spectrum()->one(), max_depth_.hv() < 2, {}, render_env);
            add_sample(dispatch_idx().xy(), L, frame_index);
        };
        shader_ = device().compile(kernel, "path tracing integrator");

        compile_path_tracing();
    }

    RealTimeDenoiseInput denoise_input() const noexcept {
        RealTimeDenoiseInput ret;
        TSensor &camera = scene().sensor();
        ret.frame_index = frame_index_;
        ret.resolution = pipeline()->resolution();
        ret.gbuffer = frame_buffer().cur_gbuffer_view(frame_index_);
        ret.prev_gbuffer = frame_buffer().prev_gbuffer_view(frame_index_);
        ret.motion_vec = frame_buffer().motion_vectors();
        ret.radiance = frame_buffer().rt_buffer();
        ret.output = frame_buffer().output_buffer();
        return ret;
    }

    void render() const noexcept override {
        const Pipeline *rp = pipeline();
        Stream &stream = rp->stream();
        if (frame_index_ == 0) {
            stream << inspector_->reset();
        }
        stream << frame_buffer().compute_GBuffer(frame_index_);
        PTParam param;
        param.frame_index = frame_index_;
        param.rays = frame_buffer().rays().descriptor();
        param.colors = frame_buffer().rt_buffer().descriptor();
        stream << path_tracing(param, frame_buffer().resolution());
        stream << frame_buffer().accumulate(frame_buffer().rt_buffer(),
                                            frame_buffer().accumulation_buffer(),
                                            frame_index_);
        stream << frame_buffer().tone_mapping(frame_buffer().accumulation_buffer(),
                                              frame_buffer().output_buffer());
//                stream << shader_(frame_index_).dispatch(rp->resolution());
        RealTimeDenoiseInput input = denoise_input();
        increase_frame_index();
    }
};
}// namespace vision

VS_MAKE_CLASS_CREATOR_HOTFIX(vision, PathTracingIntegrator)