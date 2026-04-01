file(GLOB_RECURSE SRC_FILES
     "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp")

# Force cmake refresh
idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "." "ui")

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
