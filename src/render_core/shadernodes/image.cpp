//
// Created by Zero on 09/09/2022.
//

#include "base/shader_graph/shader_node.h"
#include "rhi/common.h"
#include "base/mgr/pipeline.h"
#include "base/mgr/global.h"
#include "GUI/window.h"

namespace vision {
using namespace ocarina;
class ImageNode : public ShaderNode {
private:
    RegistrableTexture *texture_;
    EncodedData<uint> tex_id_{};
    ShaderNodeDesc desc_;

public:
    ImageNode() = default;
    explicit ImageNode(const ShaderNodeDesc &desc)
        : ShaderNode(desc),
          desc_(desc),
          texture_(&Global::instance().pipeline()->image_pool().obtain_texture(desc)) {
        tex_id_ = texture_->index();
    }
    OC_ENCODABLE_FUNC(ShaderNode, tex_id_)
    VS_MAKE_PLUGIN_NAME_FUNC
    VS_HOTFIX_MAKE_RESTORE(ShaderNode, texture_, tex_id_, desc_)

    void reload(ocarina::Widgets *widgets) noexcept {
        fs::path path = texture_->host_tex().path();
        if (Widgets::open_file_dialog(path)) {
            desc_.set_value("fn", path.string());
            desc_.reset_hash();
            texture_ = &Global::instance().pipeline()->image_pool().obtain_texture(desc_);
            texture_->upload_immediately();
            tex_id_.hv() = texture_->index().hv();
            changed_ = true;
        }
    }

    void render_sub_UI(ocarina::Widgets *widgets) noexcept override {
        widgets->button_click("reload", [&] {
            reload(widgets);
        });
        widgets->image(texture_->host_tex());
    }

    bool render_UI(ocarina::Widgets *widgets) noexcept override {
        widgets->text(name_.c_str());
        widgets->same_line();
        widgets->use_tree("open",[&] {
            render_sub_UI(widgets);
        });
        return true;
    }

    [[nodiscard]] DynamicArray<float> evaluate(const AttrEvalContext &ctx,
                                        const SampledWavelengths &swl) const noexcept override {
        return pipeline()->tex_var(*tex_id_).sample(texture_->host_tex().channel_num(), ctx.uv);
    }
    [[nodiscard]] ocarina::vector<float> average() const noexcept override {
        return texture_->host_tex().average_vector();
    }
    [[nodiscard]] uint2 resolution() const noexcept override {
        return texture_->device_tex()->resolution().xy();
    }
    void for_each_pixel(const function<Image::foreach_signature> &func) const noexcept override {
        texture_->host_tex().for_each_pixel(func);
    }
};
}// namespace vision

VS_MAKE_CLASS_CREATOR_HOTFIX(vision, ImageNode)
VS_REGISTER_CURRENT_PATH(0, "vision-shadernode-image.dll")