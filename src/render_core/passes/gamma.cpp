//
// Created by Zero on 2023/6/29.
//

#include "render_graph/pass.h"
#include "base/mgr/pipeline.h"

namespace vision {

class GammaCorrectionPass : public RenderPass {
private:
    Shader<void(Buffer<float4>, Buffer<float4>)> _shader;

public:
    explicit GammaCorrectionPass(const RenderPassDesc &desc)
        : RenderPass(desc) {}

    void compile() noexcept override {
        Kernel kernel = [&](BufferVar<float4> input, BufferVar<float4> output) {
            Float4 pixel = input.read(dispatch_id());
            output.write(dispatch_id(), linear_to_srgb(pixel));
        };
        _shader = device().compile(kernel, "Gamma correction");
    }

    [[nodiscard]] Command *dispatch() noexcept override {
        return _shader(res<Buffer<float4>>("input"),
                       res<Buffer<float4>>("output"))
            .dispatch(pipeline()->resolution());
    }

    [[nodiscard]] ChannelList inputs() const noexcept override {
        return {
            {"input", "input", false, ResourceFormat::FLOAT4}};
    }

    [[nodiscard]] ChannelList outputs() const noexcept override {
        return {
            {"output", "output", false, ResourceFormat::FLOAT4}};
    }
};

}// namespace vision

VS_MAKE_CLASS_CREATOR(vision::GammaCorrectionPass)