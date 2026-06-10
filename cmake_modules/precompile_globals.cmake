set(SYMBOLS_FILE ${PROJECT_SOURCE_DIR}/symbols.syms)
set(GLOBALS_H ${CMAKE_CURRENT_BINARY_DIR}/generated/globals.h)
set(GLOBALS_C ${CMAKE_CURRENT_BINARY_DIR}/generated/globals.c)
set(GLOBALS_H_COG ${PROJECT_SOURCE_DIR}/src/globals.h.cog)
set(GLOBALS_C_COG ${PROJECT_SOURCE_DIR}/src/globals.c.cog)

make_directory(${CMAKE_CURRENT_BINARY_DIR}/generated)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)

if(NOT PLAT_MSVC)
    set(PYTHON_EXE "${CMAKE_CURRENT_BINARY_DIR}/cogapp_venv/bin/python3")
    set(COGAPP_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/cogapp_venv/bin/cog")
else()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    # Print the Python executable path
    message(STATUS "Python executable: ${Python3_EXECUTABLE}")
    set(PYTHON_EXE "${Python3_EXECUTABLE}")
    set(COGAPP_DEPENDS "${Python3_EXECUTABLE}")
endif()

list(JOIN EMBEDDED_RESOURCES "+" EMBEDDED_RESOURCES_SEPARATED)

# All of our pre-build steps
add_custom_command(
    OUTPUT ${GLOBALS_C}

	#COMMAND ${PYTHON_EXE} -m cogapp -d -D symbols_fpath="${SYMBOLS_FILE}" -D project_root="${PROJECT_SOURCE_DIR}" -o ${GLOBALS_C} ${GLOBALS_C_COG}
    #DEPENDS ${SYMBOLS_FILE} ${GLOBALS_C_COG} ${GLOBALS_H} ${PROJECT_SOURCE_DIR}/resource/hlsl/PresentCS.cso ${PROJECT_SOURCE_DIR}/resource/hlsl/FillCS.cso ${PROJECT_SOURCE_DIR}/resource/hlsl/BlitCS.cso ${PROJECT_SOURCE_DIR}/resource/hlsl/RasterCS.cso ${PROJECT_SOURCE_DIR}/resource/shader/water.asm ${PROJECT_SOURCE_DIR}/resource/shader/specular.asm ${PROJECT_SOURCE_DIR}/resource/shader/scope.asm ${PROJECT_SOURCE_DIR}/resource/shader/jkgm.asm ${PROJECT_SOURCE_DIR}/resource/shader/horizonsky.asm ${PROJECT_SOURCE_DIR}/resource/shader/default.asm ${PROJECT_SOURCE_DIR}/resource/shader/ceilingsky.asm ${PROJECT_SOURCE_DIR}/resource/shaders/includes/debug.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/defines.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/fastmath.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/math.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/sg.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/lighting.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/decals.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/occluders.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/uniforms.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/textures.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/texgen.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/framebuffer.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/clustering.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/isa.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/reg.gli ${PROJECT_SOURCE_DIR}/resource/shaders/includes/vm.gli ${PROJECT_SOURCE_DIR}/resource/shaders/world/world_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/world/world_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/world/depth_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/world/depth_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/default_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/default_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/software_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/software_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/menu_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/menu_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/decal_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/decal_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/decal_insert_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/decal_insert_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/stencil_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/stencil_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/light_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/light_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/occ_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/occ_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/texfbo_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/texfbo_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/blur_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/blur_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/postfx/bloom_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/postfx/bloom_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/deferred/deferred_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/deferred/deferred_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/postfx/postfx_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/postfx/postfx_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/ssao_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/ssao_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/ssao_mix_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/ssao_mix_v.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/ui_f.glsl ${PROJECT_SOURCE_DIR}/resource/shaders/ui_v.glsl ${PROJECT_SOURCE_DIR}/resource/ui/bm/crosshair.bm ${PROJECT_SOURCE_DIR}/resource/ui/bm/crosshair16.bm ${PROJECT_SOURCE_DIR}/resource/ssl/cacert.pem ${PROJECT_SOURCE_DIR}/resource/ui/openjkdf2.uni ${PROJECT_SOURCE_DIR}/resource/ui/openjkdf2_i8n.uni

    COMMAND ${PYTHON_EXE} -m cogapp -d -D symbols_fpath="${SYMBOLS_FILE}" -D project_root="${PROJECT_SOURCE_DIR}" -D embedded_resources="${EMBEDDED_RESOURCES_SEPARATED}" -o ${GLOBALS_C} ${GLOBALS_C_COG}
    DEPENDS ${SYMBOLS_FILE} ${GLOBALS_C_COG} ${GLOBALS_H} ${EMBEDDED_RESOURCES} ${PYTHON_EXE} ${COGAPP_DEPENDS}
)

if(NOT PLAT_MSVC)
    add_custom_command(
        OUTPUT ${PYTHON_EXE}
        COMMAND python3 -m venv ${CMAKE_CURRENT_BINARY_DIR}/cogapp_venv
    )
    add_custom_command(
        OUTPUT ${COGAPP_DEPENDS}
        COMMAND ${PYTHON_EXE} -m pip install cogapp
        DEPENDS ${PYTHON_EXE}
    )
endif()

add_custom_command(
    OUTPUT ${GLOBALS_H}
    COMMAND ${PYTHON_EXE} -m cogapp -d -D symbols_fpath="${SYMBOLS_FILE}" -D project_root="${PROJECT_SOURCE_DIR}" -D embedded_resources="${EMBEDDED_RESOURCES_SEPARATED}" -o ${GLOBALS_H} ${GLOBALS_H_COG}
    DEPENDS ${SYMBOLS_FILE} ${GLOBALS_H_COG} ${PYTHON_EXE} ${COGAPP_DEPENDS} ${EMBEDDED_RESOURCES}
)

add_custom_command(
    PRE_BUILD
    OUTPUT ${BIN_NAME}
    DEPENDS ${GLOBALS_C} ${GLOBALS_H}
)

# HACK
list(REMOVE_ITEM ENGINE_SOURCE_FILES ${GLOBALS_C})
list(APPEND ENGINE_SOURCE_FILES ${GLOBALS_C})