//
// Created by Zero on 2023/8/28.
//

#include "base/scattering/material.h"
#include "base/shader_graph/shader_node.h"
#include "base/mgr/scene.h"
#include "base/mgr/pipeline.h"

namespace vision {

class MultiLayeredMaterial : public Material {
private:
    static constexpr float float_min = std::numeric_limits<float>::min();

private:
    Serial<float> factor_{float_min};
    Slot thickness_;
    SP<Material> bottom_{};
    SP<Material> top_{};

public:
    explicit MultiLayeredMaterial(const MaterialDesc &desc)
        : Material(desc),
          thickness_(scene().create_slot(desc.slot("thickness_", 1.f, Number))),
          bottom_(scene().load<Material>(*desc.mat0)),
          top_(scene().load<Material>(*desc.mat1)) {}
    VS_MAKE_PLUGIN_NAME_FUNC
    void prepare() noexcept override {
        bottom_->prepare();
        top_->prepare();
        thickness_->prepare();
    }
};

}// namespace vision