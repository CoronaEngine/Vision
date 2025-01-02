//
// Created by Zero on 18/03/2023.
//

#include "light.h"
#include "base/mgr/scene.h"

namespace vision {

Light::Light(const LightDesc &desc, LightType light_type)
    : Node(desc), type_(light_type),
      scale_(desc["scale"].as_float(1.f)) {
    strength_.set(Slot::create_slot(desc.strength));
    color_.set(Slot::create_slot(desc.color));
}

bool Light::render_UI(ocarina::Widgets *widgets) noexcept {
    string label = format("{} {} light: {}", index_, impl_type().data(), name_.c_str());
    bool open = widgets->use_tree(label, [&] {
        changed_ |= widgets->check_box("turn on", reinterpret_cast<bool *>(addressof(switch_.hv())));
        changed_ |= widgets->drag_float("scale", &scale_.hv(), 0.05, 0, 1000);
        color_.render_UI(widgets);
        strength_.render_UI(widgets);
        render_sub_UI(widgets);
    });
    return open;
}

ShapeInstance *IAreaLight::instance() const noexcept {
    return scene().get_instance(inst_idx_.hv());
}

LightSample IPointLight::sample_wi(const LightSampleContext &p_ref, Float2 u,
                                   const SampledWavelengths &swl) const noexcept {
    LightSample ret{swl.dimension()};
    LightEvalContext p_light;
    p_light.ng = direction(p_ref);
    p_light.pos = position();
    ret.eval = evaluate_wi(p_ref, p_light, swl, LightEvalMode::All);
    ret.p_light = p_light.pos;
    return ret;
}
void IPointLight::render_sub_UI(ocarina::Widgets *widgets) noexcept {
    changed_ |= widgets->drag_float3("position", &host_position(),
                                     0.02, 0, 0);
}

}// namespace vision