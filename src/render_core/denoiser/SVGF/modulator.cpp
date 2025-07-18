//
// Created by Zero on 2024/3/8.
//

#include "modulator.h"
#include "svgf.h"

namespace vision::svgf {

void Modulator::prepare() noexcept {
}

void Modulator::compile() noexcept {
    Kernel kernel = [&](Var<ModulatorParam> param) noexcept {
        Float3 albedo = param.albedo_buffer.read(dispatch_id()).xyz();
        Float3 emission = param.emission_buffer.read(dispatch_id()).xyz();
        SVGFDataVar svgf_data = param.svgf_buffer.read(dispatch_id());
        Float3 illumination = svgf_data->illumination();
        Float3 output = illumination * albedo + emission;
        param.radiance_buffer.write(dispatch_id(), make_float4(output, 1.f));
    };
    modulate_ = device().compile(kernel, "SVGF-modulate");

    Kernel kernel2 = [&](Var<ModulatorParam> param) noexcept {
        Float3 emission = param.emission_buffer.read(dispatch_id()).xyz();
        Float3 albedo = param.albedo_buffer.read(dispatch_id()).xyz();
        Float3 radiance = param.radiance_buffer.read(dispatch_id()).xyz();
        Float3 illumination = svgf::demodulate(radiance.xyz() - emission, albedo);
        SVGFDataVar svgf_data;
        svgf_data.illumi_v = make_float4(illumination, 0.f);
        param.svgf_buffer.write(dispatch_id(), svgf_data);
    };
    demodulate_ = device().compile(kernel2, "SVGF-demodulate");
}

CommandList Modulator::demodulate(vision::RealTimeDenoiseInput &input) noexcept {
    CommandList ret;
    ModulatorParam param;
    param.albedo_buffer = input.albedo.descriptor();
    param.emission_buffer = input.emission.descriptor();
    param.svgf_buffer = svgf_->cur_svgf_buffer(input.frame_index).descriptor();
    param.radiance_buffer = input.radiance.descriptor();
    ret << demodulate_(param).dispatch(input.resolution);
    return ret;
}

CommandList Modulator::modulate(vision::RealTimeDenoiseInput &input) noexcept {
    CommandList ret;
    ModulatorParam param;
    param.albedo_buffer = input.albedo.descriptor();
    param.emission_buffer = input.emission.descriptor();
    param.svgf_buffer = svgf_->cur_svgf_buffer(input.frame_index).descriptor();
    param.radiance_buffer = input.output.descriptor();
    ret << modulate_(param).dispatch(input.resolution);
    return ret;
}

}// namespace vision::svgf