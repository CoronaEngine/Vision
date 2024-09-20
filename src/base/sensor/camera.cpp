//
// Created by Zero on 2023/6/13.
//

#include "camera.h"
#include "base/mgr/pipeline.h"
#include "GUI/widgets.h"

namespace vision {

static float4x4 origin_matrix;

Camera::Camera(const SensorDesc &desc)
    : Sensor(desc) {
    init(desc);
    _update_resolution(film_->resolution());
}

void Camera::init(const SensorDesc &desc) noexcept {
    velocity_ = desc["velocity"].as_float(10.f);
    sensitivity_ = desc["sensitivity"].as_float(0.5f);
    set_fov_y(desc["fov_y"].as_float(20.f));
    update_mat(desc.transform_desc.mat);
    origin_matrix = desc.transform_desc.mat;
}

void Camera::render_sub_UI(ocarina::Widgets *widgets) noexcept {
    widgets->button_click("reset view", [&]{
        update_mat(origin_matrix);
    });
    changed_ |= widgets->input_float3("position", &position_);
    widgets->text("fov y: %.2f", fov_y_);
    changed_ |= widgets->input_float("yaw", &yaw_, 0.1, 5);
    changed_ |= widgets->input_float_limit("pitch", &pitch_, -pitch_max, pitch_max, 0.1, 5);
    widgets->input_float_limit("velocity", &velocity_, 0, 200, 1, 5);
}

RayState Camera::generate_ray(const SensorSample &ss) const noexcept {
    RayVar ray = generate_ray_in_camera_space(ss);
    Float4x4 c2w = *c2w_;
    ray = transform_ray(c2w, ray);
    return {.ray = ray, .ior = 1.f, .medium = *medium_id_};
}

RayVar Camera::generate_ray_in_camera_space(const vision::SensorSample &ss) const noexcept {
    Float3 p_film = make_float3(ss.p_film, 0.f);
    Float3 p_sensor = transform_point(*raster_to_camera_, p_film);
    RayVar ray = make_ray(make_float3(0.f), normalize(p_sensor));
    return ray;
}

void Camera::set_resolution(ocarina::uint2 res) noexcept {
    Sensor::set_resolution(res);
    _update_resolution(res);
}

void Camera::_update_resolution(uint2 res) noexcept {
    Box2f scrn = film()->screen_window();
    float2 span = scrn.span();
    float4x4 screen_to_raster = transform::scale(res.x, res.y, 1u) *
                                transform::scale(1 / span.x, 1 / -span.y, 1.f) *
                                transform::translation(-scrn.lower.x, -scrn.upper.y, 0.f);
    raster_to_screen_ = inverse(screen_to_raster);
    _update_raster();
}

void Camera::_update_raster() noexcept {
    camera_to_screen_ = transform::perspective<H>(fov_y(), z_near, z_far);
    raster_to_camera_ = inverse(camera_to_screen_) * raster_to_screen_;
}

void Camera::update_mat(float4x4 m) noexcept {
    pitch_ = degrees(std::atan2(m[1][2], m[1][1]));
    yaw_ = degrees(std::atan2(m[2][0], m[0][0]));
    position_ = make_float3(m[3]);
    c2w_ = camera_to_world();
}

void Camera::set_mat(ocarina::float4x4 m) noexcept {
    c2w_ = m;
    position_ = make_float3(m[3]);
}

void Camera::after_render() noexcept {
    store_prev_data();
}

void Camera::store_prev_data() noexcept {
    prev_c2r_ = inverse(raster_to_camera_.hv());
    prev_w2c_ = inverse(c2w_.hv());
    prev_pos_ = position();
}

Float3 Camera::prev_raster_coord(Float3 pos) const noexcept {
    pos = transform_point(*prev_w2c_, pos);
    pos /= pos.z;
    Float3 ret = transform_point(*prev_c2r_, pos);
    return ret;
}

Float3 Camera::raster_coord(ocarina::Float3 pos) const noexcept {
    Float4x4 w2c = inverse(device_c2w());
    pos = transform_point(w2c, pos);
    pos /= pos.z;
    Float4x4 c2r = inverse(raster_to_camera_.dv());
    Float3 ret = transform_point(c2r, pos);
    return ret;
}

void Camera::update_device_data() noexcept {
    c2w_ = camera_to_world();
    Sensor::update_data();
    upload_immediately();
}

void Camera::prepare() noexcept {
    Sensor::prepare();
    prepare_data();
    upload_immediately();
    store_prev_data();
}

float4x4 Camera::camera_to_world_rotation() const noexcept {
    float4x4 horizontal = rotation_y<H>(yaw());
    float4x4 vertical = rotation_x<H>(-pitch());
    return scale(1, 1, -1) * horizontal * vertical;
}

float4x4 Camera::camera_to_world() const noexcept {
    float4x4 translate = translation(position_);
    return translate * camera_to_world_rotation();
}

float3 Camera::forward() const noexcept {
    return c2w_.hv()[2].xyz();
}

float3 Camera::up() const noexcept {
    return c2w_.hv()[1].xyz();
}

float3 Camera::right() const noexcept {
    return c2w_.hv()[0].xyz();
}

Float4x4 Camera::device_c2w() const noexcept {
    return *c2w_;
}

Float Camera::linear_depth(const Float3 &world_pos) const noexcept {
    Float4x4 w2c = inverse(device_c2w());
    Float3 c_pos = transform_point(w2c, world_pos);
    return c_pos.z;
}

Float3 Camera::device_forward() const noexcept {
    return (*c2w_)[2].xyz();
}

Float3 Camera::device_up() const noexcept {
    return (*c2w_)[1].xyz();
}

Float3 Camera::device_right() const noexcept {
    return (*c2w_)[0].xyz();
}

Float3 Camera::device_position() const noexcept {
    return (*c2w_)[3].xyz();
}
}// namespace vision