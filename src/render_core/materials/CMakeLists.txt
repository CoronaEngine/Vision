add_library(vision-material INTERFACE)
vision_add_plugin(diffuse CATEGORY material SOURCES diffuse.cpp)
vision_add_plugin(black_body CATEGORY material SOURCES black_body.cpp)
vision_add_plugin(glass CATEGORY material SOURCES glass.cpp)
vision_add_plugin(mirror CATEGORY material SOURCES mirror.cpp)
vision_add_plugin(metal CATEGORY material SOURCES metal.cpp)
vision_add_plugin(metallic CATEGORY material SOURCES metallic.cpp)
vision_add_plugin(substrate CATEGORY material SOURCES substrate.cpp)
vision_add_plugin(plastic CATEGORY material SOURCES plastic.cpp)
vision_add_plugin(subsurface CATEGORY material SOURCES subsurface.cpp)
vision_add_plugin(mix CATEGORY material SOURCES mix.cpp)
vision_add_plugin(add CATEGORY material SOURCES add.cpp)
vision_add_plugin(principled_bsdf CATEGORY material SOURCES principled_bsdf.cpp)
vision_add_plugin(multi_layered CATEGORY material SOURCES multi_layered.cpp)
vision_add_plugin(emission CATEGORY material SOURCES emission.cpp)
