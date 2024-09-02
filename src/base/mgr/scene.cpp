//
// Created by Zero on 06/09/2022.
//

#include "scene.h"
#include "core/dynamic_module.h"
#include "pipeline.h"
#include "base/scattering/interaction.h"
#include "mesh_registry.h"
#include "GUI/window.h"

namespace vision {

Pipeline *Scene::pipeline() noexcept { return Global::instance().pipeline(); }

void Scene::init(const SceneDesc &scene_desc) {
    TIMER(init_scene);
    warper_desc_ = scene_desc.warper_desc;
    render_setting_ = scene_desc.render_setting;
    materials().set_mode(render_setting_.polymorphic_mode);
    OC_INFO_FORMAT("polymorphic mode is {}", materials().mode());
    light_sampler_.init(scene_desc.light_sampler_desc);
    light_sampler_->set_mode(render_setting_.polymorphic_mode);
    spectrum_.init(scene_desc.spectrum_desc);
    load_materials(scene_desc.material_descs);
    load_mediums(scene_desc.mediums_desc);
    camera_.init(scene_desc.sensor_desc);
    load_shapes(scene_desc.shape_descs);
    integrator_.init(scene_desc.integrator_desc);
    sampler_.init(scene_desc.sampler_desc);
    min_radius_ = scene_desc.render_setting.min_world_radius;
    Interaction::set_ray_offset_factor(scene_desc.render_setting.ray_offset_factor);
}

void Scene::tidy_up() noexcept {
    light_sampler_->tidy_up();
    material_registry().tidy_up();
    MeshRegistry::instance().tidy_up();
    tidy_up_mediums();
    OC_INFO_FORMAT("This scene contains {} material types with {} material instances",
                   materials().type_num(),
                   materials().all_instance_num());
}

void Scene::tidy_up_materials() noexcept {
    materials().for_each_instance([&](SP<Material> material, uint i) {
        material->set_index(i);
    });
}

void Scene::tidy_up_mediums() noexcept {
    mediums_.for_each_instance([&](SP<Medium> medium, uint i) {
        medium->set_index(i);
    });
}

void Scene::mark_selected(ocarina::Hit hit) noexcept {
    if (hit.is_miss()) {
        pipeline()->set_cur_node(light_sampler()->env_light());
        return;
    }
    ShapeInstance *instance = get_instance(hit.inst_id);
    pipeline()->set_cur_node(instance);
}

SP<Material> Scene::obtain_black_body() noexcept {
    if (!black_body_) {
        MaterialDesc md;
        md.sub_type = "black_body";
        black_body_ = Node::create_shared<Material>(md);
        materials().push_back(black_body_);
    }
    return black_body_;
}

void Scene::update_runtime_object(const vision::IObjectConstructor *constructors) noexcept {
}

void Scene::prepare() noexcept {
    material_registry().remove_unused_materials();
    tidy_up();
    fill_instances();
    camera_->prepare();
    sampler_->prepare();
    integrator_->prepare();
    camera_->update_device_data();
    prepare_lights();
    prepare_materials();
    pipeline()->spectrum()->prepare();
}

void Scene::prepare_lights() noexcept {
    light_sampler_->prepare();
    auto &light = light_sampler_->lights();
    OC_INFO_FORMAT("This scene contains {} light types with {} light instances",
                   light.type_num(),
                   light.all_instance_num());
}

void Scene::add_material(SP<vision::Material> material) noexcept {
    materials().push_back(ocarina::move(material));
}

void Scene::add_light(TLight light) noexcept {
    light_sampler_->add_light(ocarina::move(light));
}

void Scene::load_materials(const vector<MaterialDesc> &material_descs) {
    for (const MaterialDesc &desc : material_descs) {
        add_material(ocarina::move(Node::create_shared<Material>(desc)));
    }
}

void Scene::add_shape(const SP<vision::ShapeGroup> &group, ShapeDesc desc) {
    groups_.push_back(group);
    aabb_.extend(group->aabb);
    group->for_each([&](SP<ShapeInstance> instance, uint i) {
        auto iter = materials().find_if([&](SP<Material> &material) {
            return material->name() == instance->material_name();
        });

        if (iter != materials().end() && !instance->has_material()) {
            instance->set_material(*iter);
        }

        if (desc.emission.valid()) {
            desc.emission.set_value("inst_id", instances_.size());
            TObject<IAreaLight> light = load_light<IAreaLight>(desc.emission);
            instance->set_emission(light);
            light->set_instance(instance.get());
        }
        if (has_medium()) {
            auto inside = mediums_.find_if([&](SP<Medium> &medium) {
                return medium->name() == instance->inside_name();
            });
            if (inside != mediums_.end()) {
                instance->set_inside(*inside);
            }
            auto outside = mediums_.find_if([&](SP<Medium> &medium) {
                return medium->name() == instance->outside_name();
            });
            if (outside != mediums_.end()) {
                instance->set_outside(*outside);
            }
        }
        instances_.push_back(instance);
    });
}

void Scene::clear_shapes() noexcept {
    instances_.clear();
    groups_.clear();
}

void Scene::load_shapes(const vector<ShapeDesc> &descs) {
    for (const auto &desc : descs) {
        SP<ShapeGroup> group = Node::create_shared<ShapeGroup>(desc);
        add_shape(group, desc);
    }
}

void Scene::fill_instances() {
    for (auto &instance : instances_) {
        if (instance->has_material()) {
            const Material *material = instance->material().get();
            instance->update_material_id(materials().encode_id(material->index(), material));
        }
        if (instance->has_emission()) {
            const Light *emission = instance->emission().get();
            instance->update_light_id(light_sampler_->lights().encode_id(emission->index(), emission));
        }
        instance->fill_mesh_id();
        if (has_medium()) {
            if (instance->has_inside()) {
                instance->update_inside_medium_id(instance->inside()->index());
            }
            if (instance->has_outside()) {
                instance->update_outside_medium_id(instance->outside()->index());
            }
        }
    }
}

void Scene::load_mediums(const MediumsDesc &md) {
    global_medium_.name = md.global;
    for (uint i = 0; i < md.mediums.size(); ++i) {
        const MediumDesc &desc = md.mediums[i];
        auto medium = Node::create_shared<Medium>(desc);
        medium->set_index(i);
        mediums_.push_back(medium);
    }
    uint index = mediums_.get_index([&](const SP<Medium> &medium) {
        return medium->name() == global_medium_.name;
    });
    if (index != InvalidUI32) {
        global_medium_.init(mediums_[index]);
    }
}

void Scene::prepare_materials() {
    materials().for_each_instance([&](const SP<Material> &material) noexcept {
        material->prepare();
    });
    auto rp = pipeline();
    materials().prepare(rp->bindless_array(), rp->device());
}

void Scene::upload_data() noexcept {
    camera_->update_device_data();
    integrator_->update_device_data();
    light_sampler_->update_device_data();
    material_registry_->upload_device_data();
    if (has_changed()) {
        pipeline()->upload_bindless_array();
    }
}

}// namespace vision