//
// Created by Zero on 05/12/2022.
//

#include "interaction.h"
#include "base/scattering/medium.h"
#include "base/sampler.h"

namespace vision {
using namespace ocarina;

Float HenyeyGreenstein::f(Float3 wo, Float3 wi) const noexcept {
    return phase_HG(dot(wo, wi), g_);
}

PhaseSample HenyeyGreenstein::sample(Float3 wo, Sampler &sampler) const noexcept {
    Float2 u = sampler->next_2d();
    Float sqr_term = (1 - sqr(g_)) / (1 + g_ - 2 * g_ * u.x);
    Float cos_theta = -(1 + sqr(g_) - sqr(sqr_term)) / (2 * g_);
    cos_theta = select(abs(g_) < 1e-3f, 1 - 2 * u.x, cos_theta);

    Float sin_theta = safe_sqrt(1 - sqr(cos_theta));
    Float phi = 2 * Pi * u.y;
    Float3 v1, v2;
    coordinate_system(wo, v1, v2);
    Float3 wi = spherical_direction(sin_theta, cos_theta, phi, v1, v2, wo);
    Float f = phase_HG(cos_theta, g_);
    PhaseSample phase_sample{swl_->dimension()};
    phase_sample.eval = {{swl_->dimension(), f}, f, 0};
    phase_sample.wi = wi;
    return phase_sample;
}

float Interaction::s_ray_offset_factor = 1.f;

float Interaction::ray_offset_factor() noexcept {
    float factor = s_ray_offset_factor;
    return factor;
}

void Interaction::set_ray_offset_factor(float value) noexcept {
    s_ray_offset_factor = value;
}

Interaction::Interaction(bool has_medium) {
    init_volumetric_param(has_medium);
}

Interaction::Interaction(Float3 pos, Float3 wo, bool has_medium)
    : pos(pos), wo(wo) {
    init_volumetric_param(has_medium);
}

void Interaction::init_phase(Float g, const SampledWavelengths &swl) {
    if (!phase_) {
        return;
    }
    (*phase_).init(g, swl);
}

void Interaction::init_volumetric_param(bool has_medium) noexcept {
    if (!has_medium) {
        return;
    }
    phase_.emplace(HenyeyGreenstein{});
    mi_.emplace(MediumInterface{});
}

Bool Interaction::has_phase() {
    if (phase_) {
        return phase().valid();
    }
    return false;
}

Uint Interaction::material_inst_id() const noexcept {
    return decode_id<D>(mat_id_).first;
}

Uint Interaction::material_type_id() const noexcept {
    return decode_id<D>(mat_id_).second;
}

Uint Interaction::light_inst_id() const noexcept {
    return decode_id<D>(light_id_).first;
}

Uint Interaction::light_type_id() const noexcept {
    return decode_id<D>(light_id_).second;
}

RayVar Interaction::spawn_ray(const Float3 &dir) const noexcept {
    return vision::spawn_ray(pos, ng, dir);
}

RayVar Interaction::spawn_ray(const Float3 &dir, const Float &t) const noexcept {
    return vision::spawn_ray(pos, ng, dir, t);
}

RayState Interaction::spawn_ray_state(const Float3 &dir) const noexcept {
    RayVar ray = vision::spawn_ray(pos, ng, dir);
    Uint medium;
    if (mi_) {
        medium = select(dot(ng, dir) > 0, mi().outside, mi().inside);
    } else {
        medium = InvalidUI32;
    }
    return {.ray = ray, .ior = 1.f, .medium = medium};
}

RayState Interaction::spawn_ray_state_to(const Float3 &p) const noexcept {
    RayVar ray = vision::spawn_ray_to(pos, ng, p);
    Uint medium;
    if (mi_) {
        medium = select(dot(ng, ray->direction()) > 0, mi().outside, mi().inside);
    } else {
        medium = InvalidUI32;
    }
    return {.ray = ray, .ior = 1.f, .medium = medium};
}

Float3 Interaction::robust_position() const noexcept {
    return custom_offset_ray_origin(pos, ng);
}

Float3 Interaction::robust_position(const Float3 &w) const noexcept {
    return custom_offset_ray_origin(pos, ng, w);
}

RayVar Interaction::spawn_ray_to(const Float3 &p) const noexcept {
    return vision::spawn_ray_to(pos, ng, p);
}

void Interaction::set_medium(const Uint &inside, const Uint &outside) {
    if (!mi_) {
        return;
    }
    (*mi_).inside = inside;
    (*mi_).outside = outside;
}

}// namespace vision