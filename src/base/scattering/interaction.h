//
// Created by Zero on 03/10/2022.
//

#pragma once

#include "core/basic_types.h"
#include "dsl/dsl.h"
#include "math/geometry.h"
#include "base/sample.h"

namespace vision {
using namespace ocarina;
struct SurfaceData {
    Hit hit{};
    float4 normal_t{};
    uint mat_id{};
};
}// namespace vision
// clang-format off
OC_STRUCT(vision::SurfaceData, hit, normal_t, mat_id) {
    void set_normal(const Float3 &n) {
        normal_t = make_float4(n, normal_t.w);
    }
    [[nodiscard]] Float3 normal() const noexcept { return normal_t.xyz();}
    void set_t_max(const Float &t) { normal_t.w = t; }
    [[nodiscard]] Bool valid() const { return t_max() > 0.f; }
    [[nodiscard]] Float t_max() const noexcept { return normal_t.w;}
};
// clang-format on

namespace vision {
using namespace ocarina;
struct HitBSDF {
    ocarina::Ray next_ray{};
    ocarina::Hit next_hit{};
    array<float, 3> bsdf{};
    float pdf{-1};
};
}// namespace vision

// clang-format off
OC_STRUCT(vision::HitBSDF, next_ray, next_hit,bsdf, pdf) {
    [[nodiscard]] Float3 throughput() const noexcept {
        return bsdf.as_vec3() / pdf;
    }
    [[nodiscard]] Float3 safe_throughput() const noexcept {
        return ocarina::zero_if_nan_inf(throughput());
    }
    [[nodiscard]] Bool valid() const noexcept {
        return pdf > 0.f;
    }
};
// clang-format on

namespace vision {

using namespace ocarina;

using OCHitBSDF = Var<HitBSDF>;

template<typename T>
requires is_vector3_expr_v<T>
struct PartialDerivative : Frame<T, false> {
public:
    using vec_ty = T;

public:
    vec_ty dn_du;
    vec_ty dn_dv;

public:
    void set_frame(Frame<T> frame) {
        this->x = frame.x;
        this->y = frame.y;
        this->z = frame.z;
    }
    void update(const vec_ty &norm) noexcept {
        this->z = norm;
        this->x = normalize(cross(norm, this->y)) * length(this->x);
        this->y = normalize(cross(norm, this->x)) * length(this->y);
    }
    [[nodiscard]] vec_ty dp_du() const noexcept { return this->x; }
    [[nodiscard]] vec_ty dp_dv() const noexcept { return this->y; }
    [[nodiscard]] vec_ty normal() const noexcept { return this->z; }
    [[nodiscard]] boolean_t<T> valid() const noexcept { return nonzero(normal()); }
};

struct MediumInterface {
public:
    Uint inside{InvalidUI32};
    Uint outside{InvalidUI32};

public:
    MediumInterface() = default;
    MediumInterface(Uint in, Uint out) : inside(in), outside(out) {}
    explicit MediumInterface(Uint medium_id) : inside(medium_id), outside(medium_id) {}
    [[nodiscard]] Bool is_transition() const noexcept { return inside != outside; }
    [[nodiscard]] Bool has_inside() const noexcept { return inside != InvalidUI32; }
    [[nodiscard]] Bool has_outside() const noexcept { return outside != InvalidUI32; }
};

template<EPort p = D>
[[nodiscard]] Float phase_HG(Float cos_theta, Float g) {
    Float denom = 1 + sqr(g) + 2 * g * cos_theta;
    return Inv4Pi * (1 - sqr(g)) / (denom * sqrt(denom));
}

class Sampler;

class PhaseFunction {
protected:
    const SampledWavelengths *_swl{};

public:
    virtual void init(Float g, const SampledWavelengths &swl) noexcept = 0;
    [[nodiscard]] virtual Bool valid() const noexcept = 0;

    [[nodiscard]] virtual ScatterEval evaluate(Float3 wo, Float3 wi) const noexcept {
        Float val = f(wo, wi);
        return {{_swl->dimension(), val}, val, 0};
    }
    [[nodiscard]] virtual PhaseSample sample(Float3 wo, Sampler *sampler) const noexcept = 0;
    [[nodiscard]] virtual Float f(Float3 wo, Float3 wi) const noexcept = 0;
};

class HenyeyGreenstein : public PhaseFunction {
private:
    static constexpr float InvalidG = 10;
    Float _g{InvalidG};

public:
    HenyeyGreenstein() = default;
    void init(Float g, const SampledWavelengths &swl) noexcept override {
        _g = g;
        _swl = &swl;
    }
    [[nodiscard]] Float f(Float3 wo, Float3 wi) const noexcept override;
    [[nodiscard]] PhaseSample sample(Float3 wo, Sampler *sampler) const noexcept override;
    [[nodiscard]] Bool valid() const noexcept override { return InvalidG != _g; }
};

template<typename Pos, typename Normal, typename W>
inline auto offset_ray_origin(Pos &&p_in, Normal n_in, W w) noexcept {
    n_in = ocarina::select(ocarina::dot(w, n_in) > 0, n_in, -n_in);
    return offset_ray_origin(OC_FORWARD(p_in), n_in);
}

struct Interaction {
private:
    static float s_ray_offset_factor;

public:
    [[nodiscard]] static float ray_offset_factor() noexcept;
    static void set_ray_offset_factor(float value) noexcept;
    template<typename Pos, typename Normal>
    static auto custom_offset_ray_origin(Pos &&p_in, Normal &&n_in) noexcept {
        float factor = ray_offset_factor();
        return offset_ray_origin(OC_FORWARD(p_in), OC_FORWARD(n_in) * factor);
    }

    template<typename Pos, typename Normal, typename W>
    static auto custom_offset_ray_origin(Pos &&p_in, Normal &&n_in, W w) noexcept {
        float factor = ray_offset_factor();
        return offset_ray_origin(OC_FORWARD(p_in), OC_FORWARD(n_in) * factor, w);
    }

public:
    Float3 pos;
    Float3 wo;
    Float3 time;
    Float3 ng;

    Float2 uv;
    Float2 lightmap_uv;
    PartialDerivative<Float3> shading;
    Float prim_area{0.f};
    Uint prim_id{InvalidUI32};
    Uint lightmap_id{InvalidUI32};
    Float du_dx;
    Float du_dy;
    Float dv_dx;
    Float dv_dy;

private:
    Uint _mat_id{InvalidUI32};
    Uint _light_id{InvalidUI32};

private:
    // todo optimize volpt and pt
    optional<MediumInterface> _mi{};
    optional<HenyeyGreenstein> _phase{};

public:
    Interaction(bool has_medium);
    Interaction(Float3 pos, Float3 wo, bool has_medium);
    void init_volumetric_param(bool has_medium) noexcept;
    void init_phase(Float g, const SampledWavelengths &swl);
    [[nodiscard]] Bool has_phase();
    void update_wo(const Float3 &view_pos) noexcept { wo = normalize(view_pos - pos); }
    void set_medium(const Uint &inside, const Uint &outside);
    void set_material(const Uint &mat) noexcept { _mat_id = mat; }
    void set_light(const Uint &light) noexcept { _light_id = light; }
    [[nodiscard]] HenyeyGreenstein phase() const noexcept { return *_phase; }
    [[nodiscard]] MediumInterface mi() const noexcept { return *_mi; }
    [[nodiscard]] Bool has_emission() const noexcept { return _light_id != InvalidUI32; }
    [[nodiscard]] Bool has_material() const noexcept { return _mat_id != InvalidUI32; }
    [[nodiscard]] Bool has_lightmap() const noexcept { return lightmap_id != InvalidUI32; }
    [[nodiscard]] Uint material_inst_id() const noexcept;
    [[nodiscard]] Uint material_type_id() const noexcept;
    [[nodiscard]] Uint material_id() const noexcept { return _mat_id; }
    [[nodiscard]] Uint light_inst_id() const noexcept;
    [[nodiscard]] Uint light_type_id() const noexcept;
    [[nodiscard]] Uint light_id() const noexcept { return _light_id; }
    [[nodiscard]] Bool valid() const noexcept { return prim_id != InvalidUI32; }
    [[nodiscard]] OCRay spawn_ray(const Float3 &dir) const noexcept;
    [[nodiscard]] OCRay spawn_ray(const Float3 &dir, const Float &t) const noexcept;
    [[nodiscard]] RayState spawn_ray_state(const Float3 &dir) const noexcept;
    [[nodiscard]] RayState spawn_ray_state_to(const Float3 &p) const noexcept;
    [[nodiscard]] OCRay spawn_ray_to(const Float3 &p) const noexcept;
    [[nodiscard]] Float3 robust_position() const noexcept;
    [[nodiscard]] Float3 robust_position(const Float3 &w) const noexcept;
};

struct HitContext {
public:
    mutable optional<OCHit> hit{};
    mutable optional<Interaction> it{};

public:
    HitContext() = default;
    HitContext(const Interaction &it) {
        this->it.emplace(it);
    }
    HitContext(const OCHit &hit) {
        this->hit.emplace(hit);
    }
    HitContext(const OCHit &hit, const Interaction &it) {
        this->hit.emplace(hit);
        this->it.emplace(it);
    }
};

template<typename T>
[[nodiscard]] ray_t<T> spawn_ray(T pos, T normal, T dir) {
    normal *= select(dot(normal, dir) > 0, 1.f, -1.f);
    T org = offset_ray_origin(pos, normal);
    return make_ray(org, dir);
}

template<typename T, typename U>
[[nodiscard]] ray_t<T> spawn_ray(T pos, T normal, T dir, U t_max) {
    normal *= select(dot(normal, dir) > 0, 1.f, -1.f);
    T org = offset_ray_origin(pos, normal);
    return make_ray(org, dir, t_max);
}

template<typename T>
[[nodiscard]] ray_t<T> spawn_ray_to(T p_start, T n_start, T p_target) {
    T dir = p_target - p_start;
    n_start *= select(dot(n_start, dir) > 0, 1.f, -1.f);
    T org = offset_ray_origin(p_start, n_start);
    return make_ray(org, dir, 1 - ShadowEpsilon);
}

template<typename T>
[[nodiscard]] ray_t<T> spawn_ray_to(T p_start, T n_start, T p_target, T n_target) {
    T dir = p_target - p_start;
    n_target *= select(dot(n_target, -dir) > 0, 1.f, -1.f);
    p_target = offset_ray_origin(p_target, n_target);
    n_start *= select(dot(n_start, dir) > 0, 1.f, -1.f);
    T org = offset_ray_origin(p_start, n_start);
    return make_ray(org, dir, 1 - ShadowEpsilon);
}

struct SpacePoint {
    Float3 pos;
    Float3 ng;
    SpacePoint() = default;
    explicit SpacePoint(const Float3 &p) : pos(p) {}
    SpacePoint(const Float3 &p, const Float3 &n)
        : pos(p), ng(n) {}
    explicit SpacePoint(const Interaction &it)
        : pos(it.pos), ng(it.ng) {}

    [[nodiscard]] Float3 robust_pos(const Float3 &dir) const noexcept {
        Float factor = select(dot(ng, dir) > 0, 1.f, -1.f);
        return Interaction::custom_offset_ray_origin(pos, ng * factor);
    }

    [[nodiscard]] OCRay spawn_ray(const Float3 &dir) const noexcept {
        return vision::spawn_ray(pos, ng, dir);
    }
    [[nodiscard]] OCRay spawn_ray_to(const Float3 &p) const noexcept {
        return vision::spawn_ray_to(pos, ng, p);
    }
    [[nodiscard]] OCRay spawn_ray_to(const SpacePoint &lsc) const noexcept {
        return vision::spawn_ray_to(pos, ng, lsc.pos, lsc.ng);
    }
};

struct GeometrySurfacePoint : public SpacePoint {
    Float2 uv{};
    GeometrySurfacePoint() = default;
    using SpacePoint::SpacePoint;
    explicit GeometrySurfacePoint(const Interaction &it, Float2 uv)
        : SpacePoint(it), uv(uv) {}
    GeometrySurfacePoint(Float3 p, Float3 ng, Float2 uv)
        : SpacePoint{p, ng}, uv(uv) {}
};

/**
 * A point on light
 * used to eval light PDF or lighting to LightSampleContext
 */
struct LightEvalContext : public GeometrySurfacePoint {
    Float PDF_pos{};
    using GeometrySurfacePoint::GeometrySurfacePoint;
    LightEvalContext() = default;
    LightEvalContext(const GeometrySurfacePoint &gsp, Float PDF_pos)
        : GeometrySurfacePoint(gsp), PDF_pos(PDF_pos) {}
    LightEvalContext(Float3 p, Float3 ng, Float2 uv, Float PDF_pos)
        : GeometrySurfacePoint{p, ng, uv}, PDF_pos(PDF_pos) {}
    LightEvalContext(const Interaction &it)
        : GeometrySurfacePoint{it, it.uv}, PDF_pos(1.f / it.prim_area) {}
};

struct LightSampleContext : public SpacePoint {
    Float3 ns;
    LightSampleContext() = default;
    LightSampleContext(const Interaction &it)
        : SpacePoint(it), ns(it.shading.normal()) {}
    LightSampleContext(Float3 p, Float3 ng, Float3 ns)
        : SpacePoint{p, ng}, ns(ns) {}
};

struct AttrEvalContext {
    Float3 pos;
    Float2 uv;
    AttrEvalContext() = default;
    AttrEvalContext(const Float3 &pos)
        : pos(pos) {}
    AttrEvalContext(const Interaction &it)
        : pos(it.pos), uv(it.uv) {}
    AttrEvalContext(const Float2 &uv)
        : uv(uv) {}
};

struct MaterialEvalContext : public AttrEvalContext {
    Float3 wo;
    Float3 ng, ns;
    Float3 dp_dus;
    MaterialEvalContext() = default;
    MaterialEvalContext(const Interaction &it)
        : AttrEvalContext(it),
          wo(it.wo),
          ng(it.ng),
          ns(it.shading.normal()),
          dp_dus(it.shading.dp_du()) {}
};

}// namespace vision