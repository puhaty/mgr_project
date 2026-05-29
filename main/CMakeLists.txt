file(GLOB_RECURSE SRC_FILES
     "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(GEN_SCRIPT  "${CMAKE_CURRENT_LIST_DIR}/ai_model/gen_params.py")
set(GEN_INPUTS  "${CMAKE_CURRENT_LIST_DIR}/ai_model/feature_quantizer.json"
                "${CMAKE_CURRENT_LIST_DIR}/ai_model/driving_style_quantizer.json")
set(GEN_OUTPUT  "${CMAKE_CURRENT_LIST_DIR}/ai_model/feature_params.h")

# Always generate at configure time so the header exists for compilation
execute_process(
    COMMAND ${Python3_EXECUTABLE} "${GEN_SCRIPT}"
    RESULT_VARIABLE _gen_result
)
if(NOT _gen_result EQUAL 0)
    message(FATAL_ERROR "gen_params.py failed (exit ${_gen_result})")
endif()

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "." "ui" "ai_model")

# add_custom_command is not available during early expansion (requirements scan)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    add_custom_command(
        OUTPUT  "${GEN_OUTPUT}"
        COMMAND ${Python3_EXECUTABLE} "${GEN_SCRIPT}"
        DEPENDS ${GEN_INPUTS} "${GEN_SCRIPT}"
        COMMENT "Generating feature_params.h from the quantizer JSONs"
    )
    target_sources(${COMPONENT_LIB} PRIVATE "${GEN_OUTPUT}")
    set_source_files_properties("${GEN_OUTPUT}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)

    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
    target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
endif()
