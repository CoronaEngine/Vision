file(GLOB_RECURSE SOURCE_FILES *.c*)
vision_add_plugin(sharc CATEGORY radiancecache SOURCES ${SOURCE_FILES})