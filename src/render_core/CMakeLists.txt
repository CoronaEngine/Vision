add_subdirectory(radiancecollectors)
add_subdirectory(filters)
add_subdirectory(samplers)
add_subdirectory(lights)
add_subdirectory(lightsamplers)
add_subdirectory(integrators)
add_subdirectory(materials)
add_subdirectory(mediums)
add_subdirectory(sensors)
add_subdirectory(shadernodes)
add_subdirectory(shapes)
add_subdirectory(warpers)
add_subdirectory(spectra)
add_subdirectory(tonemappers)
add_subdirectory(denoisers)
add_subdirectory(uvunwrappers)
add_subdirectory(rasterizers)
add_subdirectory(passes)
add_subdirectory(framebuffers)

add_library(vision-plugins INTERFACE)
target_link_libraries(vision-plugins INTERFACE
        vision-filter
        vision-shape
        vision-tonemapper
        vision-denoiser
        vision-uvunwrapper
        vision-pass
        vision-lightsampler
        vision-sensor
        vision-light
        vision-radiancecollector
        vision-rasterizer
        vision-framebuffer
        vision-shadernode
        vision-integrator
        vision-warper
        vision-spectrum
        vision-material
        vision-medium
        vision-sampler)
