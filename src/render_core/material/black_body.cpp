//
// Created by Zero on 06/11/2022.
//

#include "base/scattering/material.h"
#include "base/mgr/scene.h"

namespace vision {

class BlackBodyLobe : public Lobe {
private:
    const SampledWavelengths *swl_{nullptr};

public:
    explicit BlackBodyLobe(const SampledWavelengths &swl, const optional<ShadingFrame> &shading_frame)
        : Lobe(shading_frame), swl_(&swl) {}
    [[nodiscard]] Uint flag() const noexcept override { return BxDFFlag::Diffuse; }
    [[nodiscard]] ScatterEval evaluate_local_impl(const Float3 &wo, const Float3 &wi,
                                                  MaterialEvalMode mode, const Uint &flag,
                                                  TransportMode tm) const noexcept override;
    [[nodiscard]] SampledDirection sample_wi_local_impl(const Float3 &wo, const Uint &flag,
                                                        TSampler &sampler) const noexcept override {
        return {};
    }
    [[nodiscard]] const SampledWavelengths *swl() const override;
    [[nodiscard]] BSDFSample sample_local(const Float3 &wo, const Uint &flag, TSampler &sampler,
                                          TransportMode tm) const noexcept override;
    [[nodiscard]] SampledSpectrum albedo(const Float &cos_theta) const noexcept override {
        return {swl_->dimension(), 0.f};
    }
    VS_MAKE_LOBE_ASSIGNMENT(BlackBodyLobe)
};

const SampledWavelengths *BlackBodyLobe::swl() const {
    return swl_;
}

ScatterEval BlackBodyLobe::evaluate_local_impl(const Float3 &wo, const Float3 &wi,
                                               MaterialEvalMode mode, const Uint &flag,
                                               TransportMode tm) const noexcept {
    ScatterEval ret{*swl_};
    ret.f = {swl_->dimension(), 0.f};
    ret.pdfs = 1.f;
    return ret;
}

BSDFSample BlackBodyLobe::sample_local(const Float3 &wo, const Uint &flag, TSampler &sampler,
                                       TransportMode tm) const noexcept {
    BSDFSample ret{*swl_};
    ret.eval.pdfs = 1.f;
    /// Avoid sample discarding due to hemispherical check
    ret.eval.flags = BxDFFlag::DiffRefl;
    ret.wi = wo;
    return ret;
}

class BlackBodyMaterial : public Material {
protected:
    VS_MAKE_MATERIAL_EVALUATOR(BlackBodyLobe)

public:
    [[nodiscard]] UP<Lobe> create_lobe_set(const Interaction &it, const SampledWavelengths &swl) const noexcept override {
        auto shading_frame = compute_shading_frame(it, swl);
        return make_unique<BlackBodyLobe>(swl, std::move(shading_frame));
    }
    [[nodiscard]] uint alignment() const noexcept override { return sizeof(float); }
    uint cal_offset(ocarina::uint prev_size) const noexcept override { return ocarina::max(prev_size, 1u); }
    void initialize_slots(const vision::Material::Desc &desc) noexcept override {}
    [[nodiscard]] bool enable_delta() const noexcept override { return false; }
    explicit BlackBodyMaterial(const MaterialDesc &desc)
        : Material(desc) {}
    VS_MAKE_PLUGIN_NAME_FUNC
};

}// namespace vision

VS_MAKE_CLASS_CREATOR(vision::BlackBodyMaterial)