//
// Created by Zero on 28/10/2022.
//

#include "base/scattering/material.h"
#include "base/shader_graph/shader_node.h"
#include "base/mgr/scene.h"
#include "metal_ior.h"

namespace vision {

class FresnelConductor : public Fresnel {
private:
    SampledSpectrum eta_, k_;

public:
    FresnelConductor(const SampledSpectrum &eta, const SampledSpectrum &k,
                     const SampledWavelengths &swl)
        : Fresnel(swl), eta_(eta), k_(k) {}
    [[nodiscard]] SampledSpectrum evaluate(Float abs_cos_theta) const noexcept override {
        return fresnel_complex(abs_cos_theta, eta_, k_);
    }
    [[nodiscard]] SP<Fresnel> clone() const noexcept override {
        return make_shared<FresnelConductor>(eta_, k_, *swl_);
    }
    VS_MAKE_Fresnel_ASSIGNMENT(FresnelConductor)
};

class ConductorBxDFSet : public MicrofacetBxDFSet {
public:
    using MicrofacetBxDFSet::MicrofacetBxDFSet;
};

//    "type" : "metal",
//    "param" : {
//        "material_name" : "Cu",
//        "roughness" : [0.001, 0.001]
//    }
class MetalMaterial : public Material {
private:
    VS_MAKE_SLOT(eta);
    VS_MAKE_SLOT(k);
    VS_MAKE_SLOT(roughness);
    VS_MAKE_SLOT(anisotropic);
    bool remapping_roughness_{false};
    float alpha_threshold_{0.022};
    int metal_index_{0};
    string metal_name_;

protected:
    VS_MAKE_MATERIAL_EVALUATOR(MicrofacetBxDFSet)

public:
    MetalMaterial() = default;
    explicit MetalMaterial(const MaterialDesc &desc)
        : Material(desc),
          remapping_roughness_(desc["remapping_roughness"].as_bool(true)) {
        roughness_.set(Slot::create_slot(desc.slot("roughness", 0.0001f)));
        anisotropic_.set(Slot::create_slot(desc.slot("anisotropic", 0.f)))->set_range(-1, 1);
        init_ior(desc);
        init_slot_cursor(&eta_, &anisotropic_);
    }
    void render_sub_UI(ocarina::Widgets *widgets) noexcept override {
        widgets->input_float("alpha_threshold", &alpha_threshold_, 0.001, 0.002);
        vector<const char *> names = all_metal_names();
        changed_ |= widgets->combo("metal type", &metal_index_, names);
        Material::render_sub_UI(widgets);
        check_metal_type();
    }

    void check_metal_type() {
        string new_metal = all_metal_names()[metal_index_];
        if (new_metal == metal_name_) {
            return;
        }
        metal_name_ = new_metal;
        const ComplexIor &complex_ior = ComplexIorTable::instance()->get_ior(metal_name_);
        if (spectrum()->is_complete()) {
            eta_->update_value(complex_ior.eta);
            k_->update_value(complex_ior.k);
        } else {
            SPD spd_eta = SPD(complex_ior.eta, nullptr);
            SPD spd_k = SPD(complex_ior.k, nullptr);
            float3 eta = spd_eta.eval(rgb_spectrum_peak_wavelengths);
            float3 k = spd_k.eval(rgb_spectrum_peak_wavelengths);
            eta_->update_value({eta.x, eta.y, eta.z});
            k_->update_value({k.x, k.y, k.z});
        }
        eta_->set_range(0, 20);
        k_->set_range(0, 20);
    }

    [[nodiscard]] static vector<const char *> all_metal_names() noexcept {
        static vector<const char *> names = ComplexIorTable::instance()->all_keys();
        return names;
    }

    VS_MAKE_PLUGIN_NAME_FUNC
    void init_ior(const MaterialDesc &desc) noexcept {
        metal_name_ = desc["material_name"].as_string();
        const ComplexIor &complex_ior = ComplexIorTable::instance()->get_ior(metal_name_);
        auto names = all_metal_names();
        metal_index_ = std::find(names.begin(), names.end(), metal_name_) - names.begin();
        metal_index_ = metal_index_ >= names.size() ? 0 : metal_index_;
        metal_name_ = names[metal_index_];
        SlotDesc eta_slot;
        SlotDesc k_slot;
        if (spectrum()->is_complete()) {
            eta_slot = SlotDesc(ShaderNodeDesc{ShaderNodeType::ESPD, "spd"}, 0);
            eta_slot.node.set_value("value", complex_ior.eta);
            k_slot = SlotDesc(ShaderNodeDesc{ShaderNodeType::ESPD, "spd"}, 0);
            k_slot.node.set_value("value", complex_ior.k);
        } else {
            SPD spd_eta = SPD(complex_ior.eta, nullptr);
            SPD spd_k = SPD(complex_ior.k, nullptr);
            float3 eta = spd_eta.eval(rgb_spectrum_peak_wavelengths);
            float3 k = spd_k.eval(rgb_spectrum_peak_wavelengths);
            eta_slot = desc.slot("", eta);
            k_slot = desc.slot("", k);
        }

        eta_.set(Slot::create_slot(eta_slot));
        k_.set(Slot::create_slot(k_slot));
    }

    void prepare() noexcept override {
        eta_->prepare();
        k_->prepare();
    }

    [[nodiscard]] UP<BxDFSet> create_lobe_set(Interaction it, const SampledWavelengths &swl) const noexcept override {
        SampledSpectrum kr{swl.dimension(), 1.f};
        Float roughness = ocarina::clamp(roughness_.evaluate(it, swl).as_scalar(), 0.0001f, 1.f);
        Float anisotropic = ocarina::clamp(anisotropic_.evaluate(it, swl).as_scalar(), -0.9f, 0.9f);

        roughness = remapping_roughness_ ? roughness_to_alpha(roughness) : roughness;
        Float2 alpha = calculate_alpha<D>(roughness, anisotropic);
        auto microfacet = make_shared<GGXMicrofacet>(alpha.x, alpha.y);

        SampledSpectrum eta = SampledSpectrum{eta_.evaluate(it, swl)};
        SampledSpectrum k = SampledSpectrum{k_.evaluate(it, swl)};
        auto fresnel = make_shared<FresnelConductor>(eta, k, swl);

        UP<MicrofacetReflection> refl = make_unique<MicrofacetReflection>(kr, swl, microfacet);
        return make_unique<ConductorBxDFSet>(fresnel, std::move(refl));
    }
};

}// namespace vision

VS_MAKE_CLASS_CREATOR_HOTFIX(vision, MetalMaterial)
VS_REGISTER_CURRENT_PATH(0, "vision-material-metal.dll")