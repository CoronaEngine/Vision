//
// Created by Zero on 21/12/2022.
//

#include "srgb2spec.h"
#include "base/color/spectrum.h"
#include "base/color/spd.h"
#include "base/mgr/pipeline.h"
#include "base/scattering/material.h"

namespace vision {

template<EPort p = EPort::D>
[[nodiscard]] oc_float<p> sample_visible_wavelength_impl(const oc_float<p> &u) noexcept {
    return 538 - 138.888889f * atanh(0.85691062f - 1.82750197f * u);
}
VS_MAKE_CALLABLE(sample_visible_wavelength)

template<EPort p = EPort::D>
[[nodiscard]] oc_float<p> visible_wavelength_PDF_impl(const oc_float<p> &lambda) noexcept {
    return 0.0039398042f / sqr(cosh(0.0072f * (lambda - 538)));
}
VS_MAKE_CALLABLE(visible_wavelength_PDF)

class RGBSigmoidPolynomial {
private:
    array<Float, 3> c_;

private:
    [[nodiscard]] static Float _s(Float x) noexcept {
        return select(ocarina::isinf(x), cast<float>(x > 0.0f),
                      0.5f * fma(x, rsqrt(fma(x, x, 1.f)), 1.f));
    }

    [[nodiscard]] Float _f(Float lambda) const noexcept {
        return fma(fma(c_[0], lambda, c_[1]), lambda, c_[2]);
    }

public:
    RGBSigmoidPolynomial() noexcept = default;
    RGBSigmoidPolynomial(Float c0, Float c1, Float c2) noexcept
        : c_{c0, c1, c2} {}
    explicit RGBSigmoidPolynomial(Float3 c) noexcept : c_{c[0], c[1], c[2]} {}
    [[nodiscard]] Float operator()(Float lambda) const noexcept {
        return _s(_f(lambda));// c0 * x * x + c1 * x + c2
    }
};

class RGBToSpectrumTable {
public:
    static constexpr auto res = 64u;
    using coefficient_table_type = const float[3][res][res][res][4];

private:
    const coefficient_table_type &coefficients_;
    Pipeline *rp_{};
    uint base_index_{InvalidUI32};
    Texture coefficient0_;
    Texture coefficient1_;
    Texture coefficient2_;

private:
    [[nodiscard]] inline static auto _inverse_smooth_step(auto x) noexcept {
        return 0.5f - sin(asin(1.0f - 2.0f * x) * (1.0f / 3.0f));
    }

public:
    explicit RGBToSpectrumTable(const coefficient_table_type &coefficients, Pipeline *rp) noexcept
        : coefficients_{coefficients}, rp_(rp) {}

    void init() noexcept {
        coefficient0_ = rp_->device().create_texture(make_uint3(res), PixelStorage::FLOAT4);
        coefficient1_ = rp_->device().create_texture(make_uint3(res), PixelStorage::FLOAT4);
        coefficient2_ = rp_->device().create_texture(make_uint3(res), PixelStorage::FLOAT4);
    }

    void prepare() noexcept {
        base_index_ = rp_->register_texture(coefficient0_);
        rp_->register_texture(coefficient1_);
        rp_->register_texture(coefficient2_);
        coefficient0_.upload_immediately(&coefficients_[0]);
        coefficient1_.upload_immediately(&coefficients_[1]);
        coefficient2_.upload_immediately(&coefficients_[2]);
    }

    [[nodiscard]] float4 decode_albedo(float3 rgb_in) const noexcept {
        float3 rgb = clamp(rgb_in, 0.f, 1.f);
        if (rgb[0] == rgb[1] && rgb[1] == rgb[2]) {
            auto s = (rgb[0] - 0.5f) / std::sqrt(rgb[0] * (1.0f - rgb[0]));
            return make_float4(0.0f, 0.0f, s, cie::linear_srgb_to_y(rgb));
        }

        // Find maximum component and compute remapped component values
        uint maxc = (rgb[0] > rgb[1]) ?
                        ((rgb[0] > rgb[2]) ? 0u : 2u) :
                        ((rgb[1] > rgb[2]) ? 1u : 2u);
        float z = rgb[maxc];
        float x = rgb[(maxc + 1u) % 3u] * (res - 1u) / z;
        float y = rgb[(maxc + 2u) % 3u] * (res - 1u) / z;

        float zz = _inverse_smooth_step(_inverse_smooth_step(z)) * (res - 1u);

        uint xi = std::min(static_cast<uint>(x), res - 2u);
        uint yi = std::min(static_cast<uint>(y), res - 2u);
        uint zi = std::min(static_cast<uint>(zz), res - 2u);
        float dx = x - static_cast<float>(xi);
        float dy = y - static_cast<float>(yi);
        float dz = zz - static_cast<float>(zi);

        auto c = make_float3(0.f);
        using ocarina::lerp;
        for (auto i = 0u; i < 3u; i++) {
            // Define _co_ lambda for looking up sigmoid polynomial coefficients
            auto co = [&](int dx, int dy, int dz) noexcept {
                return coefficients_[maxc][zi + dz][yi + dy][xi + dx][i];
            };
            c[i] = lerp(dz,
                        lerp(dy,
                             lerp(dx, co(0, 0, 0), co(1, 0, 0)),
                             lerp(dx, co(0, 1, 0), co(1, 1, 0))),
                        lerp(dy,
                             lerp(dx, co(0, 0, 1), co(1, 0, 1)),
                             lerp(dx, co(0, 1, 1), co(1, 1, 1))));
        }
        return make_float4(c, cie::linear_srgb_to_y(rgb));
    }

    [[nodiscard]] float4 decode_unbound(const float3 &rgb_in) const noexcept {
        float3 rgb = max(rgb_in, make_float3(0.f));
        float m = max_comp(rgb);
        float scale = 2.f * m;
        float4 c = decode_albedo(select(scale == 0.f, make_float3(0.f), rgb / scale));
        return make_float4(c.xyz(), scale);
    }

    [[nodiscard]] Float4 decode_albedo(const Float3 &rgb_in) const noexcept {
        Float3 rgb = clamp(rgb_in, make_float3(0.f), make_float3(1.f));
        static CALLABLE_TYPE decode = [](Var<BindlessArray> array, Uint base_index, Float3 rgb) noexcept -> Float3 {
            Float3 c = make_float3(0.0f, 0.0f, (rgb[0] - 0.5f) * rsqrt(rgb[0] * (1.0f - rgb[0])));
            $if(!(rgb[0] == rgb[1] & rgb[1] == rgb[2])) {
                Uint maxc = select(
                    rgb[0] > rgb[1],
                    select(rgb[0] > rgb[2], 0u, 2u),
                    select(rgb[1] > rgb[2], 1u, 2u));
                Float z = rgb[maxc];
                Float x = rgb[(maxc + 1u) % 3u] / z;
                Float y = rgb[(maxc + 2u) % 3u] / z;
                Float zz = _inverse_smooth_step(_inverse_smooth_step(z));
                Float3 coord = make_float3(x, y, zz);
                coord = dsl::fma(
                    coord,
                    make_float3((res - 1.0f) / res),
                    make_float3(0.5f / res));
                c = array.tex_var(base_index + maxc).sample(4, coord).as_vec4().xyz();
            };
            return c;
        };
        decode.function()->set_description("RGBToSpectrumTable::decode");
        return make_float4(decode(rp_->bindless_array().var(), base_index_, rgb),
                           cie::linear_srgb_to_y(rgb));
    }

    [[nodiscard]] Float4 decode_unbound(const Float3 &rgb_in) const noexcept {
        Float3 rgb = max(rgb_in, make_float3(0.f));
        Float m = max_comp(rgb);
        Float scale = 2.f * m;
        Float4 c = decode_albedo(select(scale == 0.f, make_float3(0.f), rgb / scale));
        return make_float4(c.xyz(), scale);
    }
};

class RGBAlbedoSpectrum {
protected:
    RGBSigmoidPolynomial rsp_;

public:
    explicit RGBAlbedoSpectrum(RGBSigmoidPolynomial rsp) noexcept : rsp_{ocarina::move(rsp)} {}
    [[nodiscard]] virtual Float eval(const Float &lambda) const noexcept { return rsp_(lambda); }
};

class RGBUnboundSpectrum : public RGBAlbedoSpectrum {
private:
    Float scale_;

public:
    using Super = RGBAlbedoSpectrum;

public:
    explicit RGBUnboundSpectrum(RGBSigmoidPolynomial rsp, Float scale) noexcept
        : Super{ocarina::move(rsp)}, scale_(scale) {}
    explicit RGBUnboundSpectrum(const Float4 &c) noexcept
        : RGBUnboundSpectrum(RGBSigmoidPolynomial(c.xyz()), c.w) {}
    [[nodiscard]] Float eval(const Float &lambda) const noexcept {
        return Super::eval(lambda) * scale_;
    }
};

class RGBIlluminationSpectrum : public RGBUnboundSpectrum {
private:
    const SPD &illuminant_;

public:
    using Super = RGBUnboundSpectrum;

public:
    explicit RGBIlluminationSpectrum(RGBSigmoidPolynomial rsp, Float scale, const SPD &wp) noexcept
        : Super(rsp, scale), illuminant_(wp) {}
    explicit RGBIlluminationSpectrum(const Float4 &c, const SPD &wp) noexcept
        : RGBIlluminationSpectrum(RGBSigmoidPolynomial(c.xyz()), c.w, wp) {}
    [[nodiscard]] Float eval(const Float &lambda) const noexcept {
        return Super::eval(lambda) * illuminant_.eval(lambda);
    }
};

//"spectrum" : {
//    "type" : "hero",
//    "param" : {
//        "dimension": 3
//    }
//}
class HeroWavelengthSpectrum : public SpectrumImpl {
private:
    uint dimension_{};
    SPD illuminant_d65_;
    SPD cie_x_;
    SPD cie_y_;
    SPD cie_z_;
    RGBToSpectrumTable rgb_to_spectrum_table_;

public:
    explicit HeroWavelengthSpectrum(const SpectrumDesc &desc)
        : SpectrumImpl(desc), dimension_(desc["dimension"].as_uint(3u)),
          rgb_to_spectrum_table_(sRGBToSpectrumTable_Data, pipeline()),
          illuminant_d65_(SPD::create_cie_d65(pipeline())),
          cie_x_(SPD::create_cie_x(pipeline())),
          cie_y_(SPD::create_cie_y(pipeline())),
          cie_z_(SPD::create_cie_z(pipeline())) {
        rgb_to_spectrum_table_.init();
    }
    void render_sub_UI(ocarina::Widgets *widgets) noexcept override {
        changed_ |= widgets->input_uint("dimension", &dimension_, 1, 1);
    }
    VS_MAKE_PLUGIN_NAME_FUNC
    void prepare() noexcept override {
        illuminant_d65_.prepare();
        rgb_to_spectrum_table_.prepare();
        cie_x_.prepare();
        cie_y_.prepare();
        cie_z_.prepare();
    }
    [[nodiscard]] uint dimension() const noexcept override { return dimension_; }
    [[nodiscard]] Float3 linear_srgb(const SampledSpectrum &sp, const SampledWavelengths &swl) const noexcept override {
        return cie::xyz_to_linear_srgb(cie_xyz(sp, swl));
    }

    [[nodiscard]] optional<Bool> is_dispersive(const MaterialEvaluator *evaluator) const noexcept override {
        return evaluator->is_dispersive();
    }

    [[nodiscard]] bool is_complete() const noexcept override { return true; }

    [[nodiscard]] Float cie_y(const SampledSpectrum &sp, const SampledWavelengths &swl) const noexcept override {
        Float sum = 0.f;

        constexpr auto safe_div = [](const Float &a, const Float &b) noexcept {
            return select(b == 0.0f, 0.0f, a / b);
        };

        for (uint i = 0; i < sp.dimension(); ++i) {
            sum += safe_div(cie_y_.eval(swl.lambda(i)) * sp[i], swl.pdf(i));
        }
        Float factor = 1.f / (swl.valid_dimension() * SPD::cie_y_integral());
        return sum * factor;
    }
    [[nodiscard]] Float3 cie_xyz(const SampledSpectrum &sp, const SampledWavelengths &swl) const noexcept override {
        Float3 sum = make_float3(0.f);
        constexpr auto safe_div = [](const Float &a, const Float &b) noexcept {
            return select(b == 0.0f, 0.0f, a / b);
        };
        for (uint i = 0; i < sp.dimension(); ++i) {
            sum += make_float3(safe_div(cie_x_.eval(swl.lambda(i)) * sp[i], swl.pdf(i)),
                               safe_div(cie_y_.eval(swl.lambda(i)) * sp[i], swl.pdf(i)),
                               safe_div(cie_z_.eval(swl.lambda(i)) * sp[i], swl.pdf(i)));
        }
        Float factor = 1.f / (swl.valid_dimension() * SPD::cie_y_integral());
        return sum * factor;
    }
    [[nodiscard]] SampledWavelengths sample_wavelength(Sampler *sampler) const noexcept override {
        uint n = dimension();
        SampledWavelengths swl{n};
        Float u = sampler->next_1d();
        for (uint i = 0; i < n; ++i) {
            float offset = static_cast<float>(i * (1.f / n));
            Float up = fract(u + offset);
            Float lambda = sample_visible_wavelength(up);
            swl.set_lambda(i, lambda);
            swl.set_pdf(i, visible_wavelength_PDF(lambda));
        }
        return swl;
    }
    [[nodiscard]] float4 albedo_params(float4 rgb) const noexcept override {
        return rgb_to_spectrum_table_.decode_albedo(rgb.xyz());
    }
    [[nodiscard]] float4 illumination_params(float4 rgb) const noexcept override {
        return rgb_to_spectrum_table_.decode_unbound(rgb.xyz());
    }
    [[nodiscard]] float4 unbound_params(float4 rgb) const noexcept override {
        return rgb_to_spectrum_table_.decode_unbound(rgb.xyz());
    }
    [[nodiscard]] Float luminance(const SampledSpectrum &sp, const SampledWavelengths &swl) const noexcept override {
        return sp.average();
    }
    [[nodiscard]] SampledSpectrum params_to_illumination(Float4 val, const SampledWavelengths &swl) const noexcept {
        RGBIlluminationSpectrum spec{val, illuminant_d65_};
        SampledSpectrum sp{dimension()};
        for (uint i = 0; i < dimension(); ++i) {
            sp[i] = spec.eval(swl.lambda(i));
        }
        return sp;
    }
    [[nodiscard]] SampledSpectrum params_to_unbound(Float4 val, const SampledWavelengths &swl) const noexcept {
        RGBUnboundSpectrum spec{val};
        SampledSpectrum sp{dimension()};
        for (uint i = 0; i < dimension(); ++i) {
            sp[i] = spec.eval(swl.lambda(i));
        }
        return sp;
    }
    [[nodiscard]] SampledSpectrum params_to_albedo(Float4 val, const SampledWavelengths &swl) const noexcept {
        RGBAlbedoSpectrum spec(RGBSigmoidPolynomial{val.xyz()});
        SampledSpectrum sp{dimension()};
        for (uint i = 0; i < dimension(); ++i) {
            sp[i] = spec.eval(swl.lambda(i));
        }
        return sp;
    }
    [[nodiscard]] ColorDecode decode_to_albedo(Float3 rgb, const SampledWavelengths &swl) const noexcept override {
        Float4 c = rgb_to_spectrum_table_.decode_albedo(rgb);
        return {params_to_albedo(c, swl), ocarina::luminance(rgb)};
    }
    [[nodiscard]] ColorDecode decode_to_illumination(Float3 rgb, const SampledWavelengths &swl) const noexcept override {
        Float4 c = rgb_to_spectrum_table_.decode_unbound(rgb);
        return {params_to_illumination(c, swl), ocarina::luminance(rgb)};
    }
    [[nodiscard]] ColorDecode decode_to_unbound_spectrum(Float3 rgb, const SampledWavelengths &swl) const noexcept override {
        Float4 c = rgb_to_spectrum_table_.decode_unbound(rgb);
        return {params_to_unbound(c, swl), ocarina::luminance(rgb)};
    }
};

}// namespace vision

VS_MAKE_CLASS_CREATOR(vision::HeroWavelengthSpectrum)