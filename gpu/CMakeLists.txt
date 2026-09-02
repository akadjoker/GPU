cmake_minimum_required(VERSION 3.21)

set(GPU_VERSION 0.1.0)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(gpu VERSION ${GPU_VERSION} LANGUAGES CXX)
    set(GPU_OPENGL_DEFAULT OFF)
else()
    set(GPU_OPENGL_DEFAULT ON)
endif()

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# The library sources live in gpu/, while shared demos and tests live at the
# repository root. Keep these paths centralised so the build remains resilient
# to the repository layout.
set(GPU_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(GPU_DEMOS_DIR "${GPU_REPOSITORY_ROOT}/demos")
set(GPU_TESTS_DIR "${GPU_REPOSITORY_ROOT}/tests")

option(GPU_BUILD_OPENGL "Build the OpenGL backend" ${GPU_OPENGL_DEFAULT})
option(GPU_BUILD_OPENGLES "Build the OpenGL ES backend" OFF)
option(GPU_BUILD_VULKAN "Build the Vulkan backend" OFF)
if(GPU_BUILD_VULKAN)
    # Needed to compile the vendored SPIRV-Reflect library (plain C) used by
    # VulkanDevice's reflectPipeline(). Safe to call even when a parent
    # project already enabled C.
    enable_language(C)
endif()
option(GPU_BUILD_NULL "Build the Null backend" ON)
option(GPU_BUILD_STATIC_LIBRARIES "Build static backend libraries" ON)
option(GPU_BUILD_SHARED_LIBRARIES "Build shared backend libraries" OFF)
option(GPU_BUILD_DOCS "Build HTML, Markdown and RST API documentation when tooling is available" OFF)
option(GPU_BUILD_DEMOS "Build GPU demos" OFF)
option(GPU_BUILD_OPENGL_TESTS "Build tests requiring a real OpenGL context" OFF)
option(GPU_BUILD_OPENGLES_TESTS "Build tests requiring a real OpenGL ES context" OFF)
option(GPU_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(GPU_BUILD_SDL "Build gpu_sdl, the SDL2 window and presentation-surface helper" OFF)

# Generated documentation is build-local by default; callers can override this
# path to create a repository-level snapshot such as `documentation/`.
set(GPU_DOCS_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/docs" CACHE PATH
    "Directory receiving generated HTML, Markdown, RST and XML documentation")

function(gpu_enable_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    endif()
endfunction()

function(gpu_enable_assertions target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -UNDEBUG)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    endif()
endfunction()

function(gpu_enable_sanitizers target)
    if(GPU_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

if(GPU_BUILD_DOCS)
    find_package(Doxygen QUIET)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Doxygen_FOUND AND Python3_Interpreter_FOUND)
        set(GPU_DOCS_XML_DIR "${GPU_DOCS_OUTPUT_DIR}/xml")
        set(GPU_DOCS_GENERATED_DIR "${GPU_DOCS_OUTPUT_DIR}/generated")
        set(GPU_DOCS_REFERENCE "${GPU_DOCS_GENERATED_DIR}/api-reference.md")
        set(GPU_DOCS_RST_REFERENCE "${GPU_DOCS_GENERATED_DIR}/api-reference.rst")
        set(GPU_DOCS_HTML_INDEX "${GPU_DOCS_OUTPUT_DIR}/html/index.html")
        set(GPU_DOCS_GUIDE_DIR "${GPU_REPOSITORY_ROOT}/docs/api")
        set(GPU_DOCS_GUIDE_FILES
            ${GPU_REPOSITORY_ROOT}/docs/README.md
            ${GPU_REPOSITORY_ROOT}/docs/API_REVIEW.md
            ${GPU_DOCS_GUIDE_DIR}/overview.md
            ${GPU_DOCS_GUIDE_DIR}/device.md
            ${GPU_DOCS_GUIDE_DIR}/resources.md
            ${GPU_DOCS_GUIDE_DIR}/commands.md
            ${GPU_DOCS_GUIDE_DIR}/types-and-capabilities.md
            ${GPU_DOCS_GUIDE_DIR}/diagnostics.md
            ${GPU_DOCS_GUIDE_DIR}/backends.md)
        file(GLOB GPU_PUBLIC_HEADERS CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/include/gpu/*.h)
        configure_file(
            ${GPU_REPOSITORY_ROOT}/docs/Doxyfile.in
            ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)

        add_custom_command(
            OUTPUT ${GPU_DOCS_REFERENCE} ${GPU_DOCS_RST_REFERENCE}
            BYPRODUCTS ${GPU_DOCS_HTML_INDEX}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${GPU_DOCS_OUTPUT_DIR}
            COMMAND Doxygen::doxygen ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            COMMAND ${Python3_EXECUTABLE}
                ${GPU_REPOSITORY_ROOT}/docs/generate_api_reference.py
                ${GPU_DOCS_XML_DIR} ${GPU_DOCS_REFERENCE}
                ${GPU_REPOSITORY_ROOT}/docs/public_api_manifest.txt
            COMMAND ${Python3_EXECUTABLE}
                ${GPU_REPOSITORY_ROOT}/docs/generate_api_rst.py
                ${GPU_DOCS_XML_DIR} ${GPU_DOCS_RST_REFERENCE}
                ${GPU_REPOSITORY_ROOT}/docs/public_api_manifest.txt
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${GPU_DOCS_GUIDE_DIR} ${GPU_DOCS_OUTPUT_DIR}/api
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${GPU_REPOSITORY_ROOT}/docs/README.md
                ${GPU_DOCS_OUTPUT_DIR}/README.md
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${GPU_REPOSITORY_ROOT}/docs/API_REVIEW.md
                ${GPU_DOCS_OUTPUT_DIR}/API_REVIEW.md
            DEPENDS ${GPU_PUBLIC_HEADERS}
                ${GPU_REPOSITORY_ROOT}/docs/Doxyfile.in
                ${GPU_REPOSITORY_ROOT}/docs/generate_api_reference.py
                ${GPU_REPOSITORY_ROOT}/docs/generate_api_rst.py
                ${GPU_REPOSITORY_ROOT}/docs/public_api_manifest.txt
                ${GPU_DOCS_GUIDE_FILES}
            COMMENT "Generating HTML, Markdown and reStructuredText API documentation")
        add_custom_target(docs DEPENDS ${GPU_DOCS_REFERENCE} ${GPU_DOCS_RST_REFERENCE})
    else()
        message(WARNING
            "GPU_BUILD_DOCS is ON, but Doxygen and Python3 are required; "
            "the docs target will not be available")
    endif()
endif()

add_library(gpu_common src/GPUError.cpp src/GPUDiagnostics.cpp src/GPUProfiler.cpp)
target_compile_features(gpu_common PUBLIC cxx_std_14)
set_target_properties(gpu_common PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME common)
target_include_directories(gpu_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
gpu_enable_warnings(gpu_common)
gpu_enable_sanitizers(gpu_common)

set(GPU_INSTALL_TARGETS gpu_common)

function(gpu_add_backend_libraries backend definition)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})

    foreach(library_type IN ITEMS STATIC SHARED)
        if(library_type STREQUAL "STATIC" AND NOT GPU_BUILD_STATIC_LIBRARIES)
            continue()
        endif()
        if(library_type STREQUAL "SHARED" AND NOT GPU_BUILD_SHARED_LIBRARIES)
            continue()
        endif()

        string(TOLOWER "${library_type}" library_kind)
        set(target gpu_${backend}_${library_kind})
        add_library(${target} ${library_type}
            src/GPUBackend.cpp
            ${ARG_SOURCES})
        target_compile_features(${target} PUBLIC cxx_std_14)
        target_include_directories(${target} PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
        target_compile_definitions(${target} PRIVATE ${definition}=1)

        # A static library must propagate its link requirements. Shared
        # libraries keep their implementation dependencies private.
        if(library_type STREQUAL "STATIC")
            target_link_libraries(${target} PUBLIC gpu_common ${ARG_LIBRARIES})
        else()
            target_link_libraries(${target} PRIVATE gpu_common ${ARG_LIBRARIES})
            set_target_properties(${target} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
        endif()

        set_target_properties(${target} PROPERTIES EXPORT_NAME "${backend}_${library_kind}")
        add_library(GPU::${backend}_${library_kind} ALIAS ${target})
        gpu_enable_warnings(${target})
        gpu_enable_sanitizers(${target})

        list(APPEND GPU_INSTALL_TARGETS ${target})
        set(GPU_INSTALL_TARGETS ${GPU_INSTALL_TARGETS} PARENT_SCOPE)
    endforeach()
endfunction()

set(GPU_SOURCES src/GPUBackend.cpp)
if(GPU_BUILD_NULL)
    list(APPEND GPU_SOURCES backends/null/NullDevice.cpp)
endif()
if(GPU_BUILD_OPENGL)
    list(APPEND GPU_SOURCES backends/gl/GLLoader.cpp backends/gl/PortableGLDevice.cpp)
endif()
if(GPU_BUILD_OPENGLES)
    list(APPEND GPU_SOURCES backends/gles/GLESDevice.cpp)
endif()
set(GPU_SPIRV_REFLECT_SOURCE backends/vulkan/thirdparty/spirv-reflect/spirv_reflect.c)
if(GPU_BUILD_VULKAN)
    list(APPEND GPU_SOURCES backends/vulkan/VulkanDevice.cpp ${GPU_SPIRV_REFLECT_SOURCE})
    # Vendored third-party code: don't hold it to this project's own
    # -Wall -Wextra -Wpedantic / /W4 (see gpu_enable_warnings below).
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        set_source_files_properties(${GPU_SPIRV_REFLECT_SOURCE} PROPERTIES COMPILE_OPTIONS "-w")
    elseif(MSVC)
        set_source_files_properties(${GPU_SPIRV_REFLECT_SOURCE} PROPERTIES COMPILE_OPTIONS "/W0")
    endif()
endif()

add_library(gpu ${GPU_SOURCES})
target_compile_features(gpu PUBLIC cxx_std_14)
target_include_directories(gpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gpu PUBLIC gpu_common)
if(GPU_BUILD_NULL)
    target_compile_definitions(gpu PRIVATE GPU_HAS_NULL=1)
endif()
if(GPU_BUILD_OPENGL)
    target_compile_definitions(gpu PRIVATE GPU_HAS_OPENGL=1)
endif()
if(GPU_BUILD_OPENGLES)
    find_library(GPU_GLESV2_LIBRARY GLESv2 REQUIRED)
    target_compile_definitions(gpu PRIVATE GPU_HAS_OPENGLES=1)
    target_link_libraries(gpu PUBLIC ${GPU_GLESV2_LIBRARY})

    # Keep the GLES system library out of the installed static target's
    # absolute link interface. The package config recreates this target on the
    # consumer's machine, so GPU::gles_static remains relocatable.
    add_library(gpu_gles_runtime INTERFACE)
    set_target_properties(gpu_gles_runtime PROPERTIES EXPORT_NAME gles_runtime)
    target_link_libraries(gpu_gles_runtime INTERFACE
        "$<BUILD_INTERFACE:${GPU_GLESV2_LIBRARY}>"
        "$<INSTALL_INTERFACE:GPU::gles_runtime_system>")
    add_library(GPU::gles_runtime ALIAS gpu_gles_runtime)
    list(APPEND GPU_INSTALL_TARGETS gpu_gles_runtime)
endif()
if(GPU_BUILD_VULKAN)
    find_package(Vulkan REQUIRED)
    if(BUILD_TESTING OR GPU_BUILD_DEMOS)
        find_program(GPU_GLSLANG_VALIDATOR glslangValidator REQUIRED)
    endif()
    target_compile_definitions(gpu PRIVATE GPU_HAS_VULKAN=1)
    target_link_libraries(gpu PUBLIC Vulkan::Vulkan)
endif()
gpu_enable_warnings(gpu)
gpu_enable_sanitizers(gpu)

# Standalone distributable libraries. Each one contains exactly one backend,
# so applications must link one component (for example GPU::gl_shared), not
# both GPU::gl_* and GPU::vk_* in the same executable.
if(GPU_BUILD_OPENGL)
    gpu_add_backend_libraries(gl GPU_HAS_OPENGL
        SOURCES backends/gl/GLLoader.cpp backends/gl/PortableGLDevice.cpp)
endif()
if(GPU_BUILD_OPENGLES)
    gpu_add_backend_libraries(gles GPU_HAS_OPENGLES
        SOURCES backends/gles/GLESDevice.cpp
        LIBRARIES gpu_gles_runtime)
endif()
if(GPU_BUILD_VULKAN)
    gpu_add_backend_libraries(vk GPU_HAS_VULKAN
        SOURCES backends/vulkan/VulkanDevice.cpp ${GPU_SPIRV_REFLECT_SOURCE}
        LIBRARIES Vulkan::Vulkan)
endif()

# SDL2 window that owns the backend presentation surface (the GL context
# today - Vulkan isn't wired into this backend set yet), so applications
# never touch SDL_GL_* themselves. In-tree helper only: it links the
# umbrella `gpu` target.
if(GPU_BUILD_SDL)
    find_package(SDL2 REQUIRED)
    add_library(gpu_sdl src/GPUSDLWindow.cpp)
    target_compile_features(gpu_sdl PUBLIC cxx_std_14)
    target_include_directories(gpu_sdl
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(gpu_sdl PUBLIC gpu SDL2::SDL2)
    if(GPU_BUILD_OPENGL)
        target_compile_definitions(gpu_sdl PRIVATE GPU_HAS_OPENGL=1)
    endif()
    if(GPU_BUILD_OPENGLES)
        target_compile_definitions(gpu_sdl PRIVATE GPU_HAS_OPENGLES=1)
    endif()
    gpu_enable_warnings(gpu_sdl)
    gpu_enable_sanitizers(gpu_sdl)
endif()

if(BUILD_TESTING)
    add_executable(gpu_api_tests ${GPU_TESTS_DIR}/GPUAPITests.cpp)
    target_compile_features(gpu_api_tests PRIVATE cxx_std_14)
    target_include_directories(gpu_api_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME gpu_api_tests COMMAND gpu_api_tests)
    gpu_enable_warnings(gpu_api_tests)
    gpu_enable_sanitizers(gpu_api_tests)
    gpu_enable_assertions(gpu_api_tests)

    add_executable(gpu_error_tests ${GPU_TESTS_DIR}/GPUErrorTests.cpp src/GPUError.cpp)
    target_compile_features(gpu_error_tests PRIVATE cxx_std_14)
    target_include_directories(gpu_error_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME gpu_error_tests COMMAND gpu_error_tests)
    gpu_enable_warnings(gpu_error_tests)
    gpu_enable_sanitizers(gpu_error_tests)
    gpu_enable_assertions(gpu_error_tests)

    add_executable(gpu_diagnostics_tests ${GPU_TESTS_DIR}/GPUDiagnosticsTests.cpp src/GPUDiagnostics.cpp)
    target_compile_features(gpu_diagnostics_tests PRIVATE cxx_std_14)
    target_include_directories(gpu_diagnostics_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME gpu_diagnostics_tests COMMAND gpu_diagnostics_tests)
    gpu_enable_warnings(gpu_diagnostics_tests)
    gpu_enable_sanitizers(gpu_diagnostics_tests)
    gpu_enable_assertions(gpu_diagnostics_tests)

    if(GPU_BUILD_NULL)
        add_executable(gpu_null_tests ${GPU_TESTS_DIR}/NullDeviceTests.cpp)
        target_compile_features(gpu_null_tests PRIVATE cxx_std_14)
        target_link_libraries(gpu_null_tests PRIVATE gpu)
        add_test(NAME gpu_null_tests COMMAND gpu_null_tests)
        gpu_enable_warnings(gpu_null_tests)
        gpu_enable_sanitizers(gpu_null_tests)
        gpu_enable_assertions(gpu_null_tests)

        add_executable(gpu_profiler_tests ${GPU_TESTS_DIR}/GPUProfilerTests.cpp)
        target_compile_features(gpu_profiler_tests PRIVATE cxx_std_14)
        target_link_libraries(gpu_profiler_tests PRIVATE gpu)
        add_test(NAME gpu_profiler_tests COMMAND gpu_profiler_tests)
        gpu_enable_warnings(gpu_profiler_tests)
        gpu_enable_sanitizers(gpu_profiler_tests)
        gpu_enable_assertions(gpu_profiler_tests)

        add_executable(gpu_logging_tests ${GPU_TESTS_DIR}/GPULoggingTests.cpp)
        target_compile_features(gpu_logging_tests PRIVATE cxx_std_14)
        target_link_libraries(gpu_logging_tests PRIVATE gpu)
        add_test(NAME gpu_logging_tests COMMAND gpu_logging_tests)
        gpu_enable_warnings(gpu_logging_tests)
        gpu_enable_sanitizers(gpu_logging_tests)
        gpu_enable_assertions(gpu_logging_tests)
    endif()

    if(GPU_BUILD_VULKAN)
        set(GPU_VULKAN_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders)
        set(GPU_VULKAN_VERTEX_SHADER ${GPU_VULKAN_SHADER_DIR}/triangle.vert.spv)
        set(GPU_VULKAN_FRAGMENT_SHADER ${GPU_VULKAN_SHADER_DIR}/triangle.frag.spv)
        set(GPU_VULKAN_COMPUTE_IMAGE_SHADER ${GPU_VULKAN_SHADER_DIR}/vulkan_compute_image.comp.spv)
        set(GPU_VULKAN_COMPUTE_BUFFER_SHADER ${GPU_VULKAN_SHADER_DIR}/vulkan_compute_buffer.comp.spv)
        add_custom_command(
            OUTPUT ${GPU_VULKAN_VERTEX_SHADER} ${GPU_VULKAN_FRAGMENT_SHADER}
                ${GPU_VULKAN_COMPUTE_IMAGE_SHADER} ${GPU_VULKAN_COMPUTE_BUFFER_SHADER}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${GPU_VULKAN_SHADER_DIR}
            COMMAND ${GPU_GLSLANG_VALIDATOR} -V --target-env vulkan1.3 -S vert
                -o ${GPU_VULKAN_VERTEX_SHADER}
                ${GPU_TESTS_DIR}/shaders/vulkan_triangle.vert
            COMMAND ${GPU_GLSLANG_VALIDATOR} -V --target-env vulkan1.3 -S frag
                -o ${GPU_VULKAN_FRAGMENT_SHADER}
                ${GPU_TESTS_DIR}/shaders/vulkan_triangle.frag
            COMMAND ${GPU_GLSLANG_VALIDATOR} -V --target-env vulkan1.3 -S comp
                -o ${GPU_VULKAN_COMPUTE_IMAGE_SHADER}
                ${GPU_TESTS_DIR}/shaders/vulkan_compute_image.comp
            COMMAND ${GPU_GLSLANG_VALIDATOR} -V --target-env vulkan1.3 -S comp
                -o ${GPU_VULKAN_COMPUTE_BUFFER_SHADER}
                ${GPU_TESTS_DIR}/shaders/vulkan_compute_buffer.comp
            DEPENDS
                ${GPU_TESTS_DIR}/shaders/vulkan_triangle.vert
                ${GPU_TESTS_DIR}/shaders/vulkan_triangle.frag
                ${GPU_TESTS_DIR}/shaders/vulkan_compute_image.comp
                ${GPU_TESTS_DIR}/shaders/vulkan_compute_buffer.comp)
        add_custom_target(gpu_vulkan_shaders DEPENDS
            ${GPU_VULKAN_VERTEX_SHADER} ${GPU_VULKAN_FRAGMENT_SHADER}
            ${GPU_VULKAN_COMPUTE_IMAGE_SHADER} ${GPU_VULKAN_COMPUTE_BUFFER_SHADER})
        add_executable(gpu_vulkan_tests ${GPU_TESTS_DIR}/VulkanDeviceTests.cpp)
        target_compile_features(gpu_vulkan_tests PRIVATE cxx_std_14)
        target_link_libraries(gpu_vulkan_tests PRIVATE gpu)
        target_compile_definitions(gpu_vulkan_tests PRIVATE
            GPU_VULKAN_TEST_SHADER_DIR="${GPU_VULKAN_SHADER_DIR}")
        add_dependencies(gpu_vulkan_tests gpu_vulkan_shaders)
        add_test(NAME gpu_vulkan_tests COMMAND gpu_vulkan_tests)
        gpu_enable_warnings(gpu_vulkan_tests)
        gpu_enable_sanitizers(gpu_vulkan_tests)
        gpu_enable_assertions(gpu_vulkan_tests)
    endif()

    add_executable(gpu_resource_pool_tests ${GPU_TESTS_DIR}/ResourcePoolTests.cpp)
    target_compile_features(gpu_resource_pool_tests PRIVATE cxx_std_14)
    target_include_directories(gpu_resource_pool_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/backends)
    add_test(NAME gpu_resource_pool_tests COMMAND gpu_resource_pool_tests)
    gpu_enable_warnings(gpu_resource_pool_tests)
    gpu_enable_sanitizers(gpu_resource_pool_tests)
    gpu_enable_assertions(gpu_resource_pool_tests)

    if(GPU_BUILD_OPENGL AND GPU_BUILD_OPENGL_TESTS)
        find_package(SDL2 REQUIRED)
        add_executable(gpu_opengl_tests
            ${GPU_TESTS_DIR}/OpenGLDeviceTests.cpp
            ${GPU_TESTS_DIR}/RenderPathProbe.cpp
            ${GPU_DEMOS_DIR}/SDLGLSurface.cpp)
        target_compile_features(gpu_opengl_tests PRIVATE cxx_std_14)
        target_include_directories(gpu_opengl_tests PRIVATE
            ${GPU_TESTS_DIR})
        target_link_libraries(gpu_opengl_tests PRIVATE gpu SDL2::SDL2)
        add_test(NAME gpu_opengl_tests COMMAND gpu_opengl_tests)
        gpu_enable_warnings(gpu_opengl_tests)
        gpu_enable_sanitizers(gpu_opengl_tests)
        gpu_enable_assertions(gpu_opengl_tests)
    endif()

    if(GPU_BUILD_OPENGLES AND GPU_BUILD_OPENGLES_TESTS)
        find_package(SDL2 REQUIRED)
        add_executable(gpu_gles_tests
            ${GPU_TESTS_DIR}/GLESDeviceTests.cpp
            ${GPU_TESTS_DIR}/RenderPathProbe.cpp
            ${GPU_DEMOS_DIR}/SDLGLESSurface.cpp)
        target_compile_features(gpu_gles_tests PRIVATE cxx_std_14)
        target_include_directories(gpu_gles_tests PRIVATE ${GPU_TESTS_DIR})
        target_link_libraries(gpu_gles_tests PRIVATE gpu SDL2::SDL2)
        add_test(NAME gpu_gles_tests COMMAND gpu_gles_tests)
        gpu_enable_warnings(gpu_gles_tests)
        gpu_enable_sanitizers(gpu_gles_tests)
        gpu_enable_assertions(gpu_gles_tests)
    endif()

    if(GPU_BUILD_OPENGL AND GPU_BUILD_OPENGL_TESTS AND
       GPU_BUILD_OPENGLES AND GPU_BUILD_OPENGLES_TESTS)
        find_package(SDL2 REQUIRED)
        add_executable(gpu_gl_vs_gles_tests
            ${GPU_TESTS_DIR}/GLvsGLESImageTests.cpp
            ${GPU_DEMOS_DIR}/SDLGLSurface.cpp
            ${GPU_DEMOS_DIR}/SDLGLESSurface.cpp)
        target_compile_features(gpu_gl_vs_gles_tests PRIVATE cxx_std_14)
        target_link_libraries(gpu_gl_vs_gles_tests PRIVATE gpu SDL2::SDL2)
        add_test(NAME gpu_gl_vs_gles_tests COMMAND gpu_gl_vs_gles_tests)
        gpu_enable_warnings(gpu_gl_vs_gles_tests)
        gpu_enable_sanitizers(gpu_gl_vs_gles_tests)
        gpu_enable_assertions(gpu_gl_vs_gles_tests)
    endif()

endif()

if(GPU_BUILD_DEMOS)
    if(NOT GPU_BUILD_OPENGL AND NOT GPU_BUILD_OPENGLES AND NOT GPU_BUILD_VULKAN)
        message(FATAL_ERROR "GPU_BUILD_DEMOS requires GPU_BUILD_OPENGL, GPU_BUILD_OPENGLES, or GPU_BUILD_VULKAN")
    endif()
    find_package(SDL2 REQUIRED)

    set(GPU_DEMO_SURFACES ${GPU_DEMOS_DIR}/ShadowDemoCommon.cpp)
    if(GPU_BUILD_OPENGL)
        list(APPEND GPU_DEMO_SURFACES ${GPU_DEMOS_DIR}/SDLGLSurface.cpp)
    endif()
    if(GPU_BUILD_OPENGLES)
        list(APPEND GPU_DEMO_SURFACES ${GPU_DEMOS_DIR}/SDLGLESSurface.cpp)
    endif()
    if(GPU_BUILD_VULKAN)
        list(APPEND GPU_DEMO_SURFACES ${GPU_DEMOS_DIR}/SDLVulkanSurface.cpp)
    endif()

    function(gpu_add_demo target)
        add_executable(${target} ${ARGN} ${GPU_DEMO_SURFACES})
        target_compile_features(${target} PRIVATE cxx_std_14)
        target_include_directories(${target} PRIVATE ${GPU_DEMOS_DIR})
        target_link_libraries(${target} PRIVATE gpu SDL2::SDL2)
        if(GPU_BUILD_OPENGL)
            target_compile_definitions(${target} PRIVATE GPU_DEMO_HAS_OPENGL=1)
        endif()
        if(GPU_BUILD_OPENGLES)
            target_compile_definitions(${target} PRIVATE GPU_DEMO_HAS_OPENGLES=1)
        endif()
        if(GPU_BUILD_VULKAN)
            target_compile_definitions(${target} PRIVATE GPU_DEMO_HAS_VULKAN=1)
        endif()
        gpu_enable_warnings(${target})
        gpu_enable_sanitizers(${target})
    endfunction()

    gpu_add_demo(gpu_shadow_spot ${GPU_DEMOS_DIR}/SDLShadowSpot.cpp)
    gpu_add_demo(gpu_shadow_directional ${GPU_DEMOS_DIR}/SDLShadowDirectional.cpp)
    gpu_add_demo(gpu_shadow_point ${GPU_DEMOS_DIR}/SDLShadowPoint.cpp)
    gpu_add_demo(gpu_forward_lights ${GPU_DEMOS_DIR}/SDLForwardLights.cpp)
    gpu_add_demo(gpu_shadow_atlas ${GPU_DEMOS_DIR}/SDLShadowAtlas.cpp)
    gpu_add_demo(gpu_lights_shadows ${GPU_DEMOS_DIR}/SDLLightsShadows.cpp)
    gpu_add_demo(gpu_mini_engine ${GPU_DEMOS_DIR}/SDLMiniEngine.cpp)

    if(GPU_BUILD_OPENGL)
        add_executable(gpu_sdl_triangle ${GPU_DEMOS_DIR}/SDLGLSurface.cpp ${GPU_DEMOS_DIR}/SDLTriangle.cpp)
        target_compile_features(gpu_sdl_triangle PRIVATE cxx_std_14)
        target_link_libraries(gpu_sdl_triangle PRIVATE gpu SDL2::SDL2)
        gpu_enable_warnings(gpu_sdl_triangle)
        gpu_enable_sanitizers(gpu_sdl_triangle)
    endif()

    if(GPU_BUILD_VULKAN)
        set(GPU_VULKAN_DEMO_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/vulkan-demo-shaders)
        set(GPU_VULKAN_DEMO_VERTEX_SHADER ${GPU_VULKAN_DEMO_SHADER_DIR}/vulkan_demo_triangle.vert.spv)
        set(GPU_VULKAN_DEMO_FRAGMENT_SHADER ${GPU_VULKAN_DEMO_SHADER_DIR}/vulkan_demo_triangle.frag.spv)
        add_custom_command(
            OUTPUT ${GPU_VULKAN_DEMO_VERTEX_SHADER} ${GPU_VULKAN_DEMO_FRAGMENT_SHADER}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${GPU_VULKAN_DEMO_SHADER_DIR}
            COMMAND ${GPU_GLSLANG_VALIDATOR} -V --target-env vulkan1.3 -S vert
                -o ${GPU_VULKAN_DEMO_VERTEX_SHADER}
                ${GPU_DEMOS_DIR}/shaders/vulkan_demo_triangle.vert
            COMMAND ${GPU_GLSLANG_VALIDATOR} -V --target-env vulkan1.3 -S frag
                -o ${GPU_VULKAN_DEMO_FRAGMENT_SHADER}
                ${GPU_DEMOS_DIR}/shaders/vulkan_demo_triangle.frag
            DEPENDS
                ${GPU_DEMOS_DIR}/shaders/vulkan_demo_triangle.vert
                ${GPU_DEMOS_DIR}/shaders/vulkan_demo_triangle.frag)
        add_custom_target(gpu_vulkan_demo_shaders DEPENDS
            ${GPU_VULKAN_DEMO_VERTEX_SHADER} ${GPU_VULKAN_DEMO_FRAGMENT_SHADER})
        add_executable(gpu_vulkan_sdl_triangle
            ${GPU_DEMOS_DIR}/SDLVulkanSurface.cpp ${GPU_DEMOS_DIR}/SDLVulkanTriangle.cpp)
        target_compile_features(gpu_vulkan_sdl_triangle PRIVATE cxx_std_14)
        target_link_libraries(gpu_vulkan_sdl_triangle PRIVATE gpu SDL2::SDL2)
        target_compile_definitions(gpu_vulkan_sdl_triangle PRIVATE
            GPU_VULKAN_DEMO_SHADER_DIR="${GPU_VULKAN_DEMO_SHADER_DIR}")
        add_dependencies(gpu_vulkan_sdl_triangle gpu_vulkan_demo_shaders)
        gpu_enable_warnings(gpu_vulkan_sdl_triangle)
        gpu_enable_sanitizers(gpu_vulkan_sdl_triangle)

        # reflectPipeline() demo: binds its uniform block/sampler by name
        # (via reflection) instead of hardcoded slots.
        set(GPU_VULKAN_REFLECTION_VERTEX_SHADER ${GPU_VULKAN_DEMO_SHADER_DIR}/vulkan_demo_reflection.vert.spv)
        set(GPU_VULKAN_REFLECTION_FRAGMENT_SHADER ${GPU_VULKAN_DEMO_SHADER_DIR}/vulkan_demo_reflection.frag.spv)
        add_custom_command(
            OUTPUT ${GPU_VULKAN_REFLECTION_VERTEX_SHADER} ${GPU_VULKAN_REFLECTION_FRAGMENT_SHADER}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${GPU_VULKAN_DEMO_SHADER_DIR}
            COMMAND ${GPU_GLSLANG_VALIDATOR} -V --target-env vulkan1.3 -S vert
                -o ${GPU_VULKAN_REFLECTION_VERTEX_SHADER}
                ${GPU_DEMOS_DIR}/shaders/vulkan_demo_reflection.vert
            COMMAND ${GPU_GLSLANG_VALIDATOR} -V --target-env vulkan1.3 -S frag
                -o ${GPU_VULKAN_REFLECTION_FRAGMENT_SHADER}
                ${GPU_DEMOS_DIR}/shaders/vulkan_demo_reflection.frag
            DEPENDS
                ${GPU_DEMOS_DIR}/shaders/vulkan_demo_reflection.vert
                ${GPU_DEMOS_DIR}/shaders/vulkan_demo_reflection.frag)
        add_custom_target(gpu_vulkan_reflection_demo_shaders DEPENDS
            ${GPU_VULKAN_REFLECTION_VERTEX_SHADER} ${GPU_VULKAN_REFLECTION_FRAGMENT_SHADER})
        add_executable(gpu_vulkan_sdl_reflection
            ${GPU_DEMOS_DIR}/SDLVulkanSurface.cpp ${GPU_DEMOS_DIR}/SDLVulkanReflection.cpp)
        target_compile_features(gpu_vulkan_sdl_reflection PRIVATE cxx_std_14)
        target_link_libraries(gpu_vulkan_sdl_reflection PRIVATE gpu SDL2::SDL2)
        target_compile_definitions(gpu_vulkan_sdl_reflection PRIVATE
            GPU_VULKAN_DEMO_SHADER_DIR="${GPU_VULKAN_DEMO_SHADER_DIR}")
        add_dependencies(gpu_vulkan_sdl_reflection gpu_vulkan_reflection_demo_shaders)
        gpu_enable_warnings(gpu_vulkan_sdl_reflection)
        gpu_enable_sanitizers(gpu_vulkan_sdl_reflection)
    endif()
endif()

# Installation exports only the backend-specific components. The legacy `gpu`
# target remains available to existing in-tree users, but new consumers should
# select one installed component: GPU::gl_static, GPU::gl_shared,
# GPU::vk_static, or GPU::vk_shared.
if(GPU_BUILD_VULKAN)
    set(GPU_PACKAGE_HAS_VULKAN ON)
else()
    set(GPU_PACKAGE_HAS_VULKAN OFF)
endif()
if(GPU_BUILD_OPENGLES)
    set(GPU_PACKAGE_HAS_OPENGLES ON)
else()
    set(GPU_PACKAGE_HAS_OPENGLES OFF)
endif()

install(TARGETS ${GPU_INSTALL_TARGETS}
    EXPORT GPUTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES
    ${GPU_REPOSITORY_ROOT}/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/GPU)
install(FILES
    ${GPU_REPOSITORY_ROOT}/README.md
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/GPU)
install(DIRECTORY ${GPU_REPOSITORY_ROOT}/docs/
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/GPU/docs)
install(EXPORT GPUTargets
    FILE GPUTargets.cmake
    NAMESPACE GPU::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GPU)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GPUConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/GPUConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GPU)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/GPUConfigVersion.cmake
    VERSION ${GPU_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/GPUConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/GPUConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GPU)
