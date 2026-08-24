cmake_minimum_required(VERSION 3.21)
project(SCMAutoContourNative64 VERSION 9.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SCM_V9_PRODUCTION "Build the complete production geometry pipeline" ON)

# Some third-party package exports contain the POSIX math library name `m`.
# CMake converts that item to m.lib for Visual Studio, but Windows has no such
# library (the CRT already supplies these functions).  Resolve plain `m` as an
# empty interface target before imported dependency graphs are evaluated.
if(MSVC AND NOT TARGET m)
    add_library(m INTERFACE)
    message(STATUS "SCM Windows link guard: POSIX libm mapped to the MSVC CRT")
endif()

function(scm_strip_posix_math_link target_name)
    if(NOT MSVC OR NOT TARGET "${target_name}")
        return()
    endif()
    foreach(property_name IN ITEMS INTERFACE_LINK_LIBRARIES INTERFACE_LINK_LIBRARIES_DIRECT)
        get_target_property(property_value "${target_name}" "${property_name}")
        if(NOT property_value OR property_value MATCHES "-NOTFOUND$")
            continue()
        endif()
        set(filtered_value "")
        foreach(link_item IN LISTS property_value)
            if(link_item STREQUAL "m" OR link_item STREQUAL "-lm" OR link_item STREQUAL "m.lib")
                message(STATUS "SCM Windows link guard: removed ${link_item} from ${target_name}")
            else()
                list(APPEND filtered_value "${link_item}")
            endif()
        endforeach()
        set_property(TARGET "${target_name}" PROPERTY "${property_name}" "${filtered_value}")
    endforeach()
endfunction()

if(SCM_V9_PRODUCTION)
    message(STATUS "SCM dependency 1/5: OpenCV")
    find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc)
    message(STATUS "SCM dependency 2/5: Eigen3")
    find_package(Eigen3 CONFIG REQUIRED)
    message(STATUS "SCM dependency 3/5: Ceres")
    find_package(Ceres CONFIG REQUIRED)
    message(STATUS "SCM dependency 4/5: boost_geometry config")
    find_package(boost_geometry CONFIG REQUIRED)
    message(STATUS "SCM dependency 5/5: Clipper2 config")
    find_package(Clipper2 CONFIG REQUIRED)
    if(TARGET Clipper2::Clipper2)
        set(SCM_CLIPPER_TARGET Clipper2::Clipper2)
    elseif(TARGET Clipper2)
        set(SCM_CLIPPER_TARGET Clipper2)
    else()
        message(FATAL_ERROR "Clipper2 package was found, but it did not export a supported CMake target.")
    endif()
    foreach(dependency_target IN ITEMS
        Ceres::ceres Eigen3::Eigen Boost::geometry
        Clipper2::Clipper2 Clipper2
        opencv_core opencv_imgproc)
        scm_strip_posix_math_link("${dependency_target}")
    endforeach()
endif()

add_library(scm_contour_engine STATIC
    src/contour_engine.cpp
    src/contour_engine.hpp
    src/geometry_planner.cpp
    src/geometry_planner.hpp
    src/production_pipeline.cpp
    src/production_pipeline.hpp
)
target_include_directories(scm_contour_engine PUBLIC src)
if(SCM_V9_PRODUCTION)
    target_compile_definitions(scm_contour_engine PUBLIC SCM_V9_PRODUCTION=1)
    target_link_libraries(scm_contour_engine PUBLIC
        ${OpenCV_LIBS}
        Eigen3::Eigen
        Ceres::ceres
        Boost::geometry
        ${SCM_CLIPPER_TARGET}
    )
endif()

if(WIN32 AND MSVC)
    add_library(SCMAutoContourNative64 SHARED
        src/plugin.cpp
        src/corel_bridge.cpp
        src/corel_bridge.hpp
        src/wic_image.cpp
        src/wic_image.hpp
        src/plugin.rc
        src/resource.h
    )
    target_include_directories(SCMAutoContourNative64 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/_generated")
    target_link_libraries(SCMAutoContourNative64 PRIVATE
        scm_contour_engine
        windowscodecs ole32 oleaut32 uuid comsuppw user32 gdi32 comctl32 advapi32
    )
    target_compile_definitions(SCMAutoContourNative64 PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(SCMAutoContourNative64 PRIVATE /W4 /permissive- /EHsc /utf-8)
    set_target_properties(SCMAutoContourNative64 PROPERTIES PREFIX "" SUFFIX ".cpg")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(scm_engine_tests tests/engine_tests.cpp)
    target_link_libraries(scm_engine_tests PRIVATE scm_contour_engine)
    add_test(NAME scm_engine_tests COMMAND scm_engine_tests)
    if(SCM_V9_PRODUCTION)
        add_executable(scm_production_tests tests/production_pipeline_tests.cpp)
        target_link_libraries(scm_production_tests PRIVATE scm_contour_engine)
        add_test(NAME scm_production_tests COMMAND scm_production_tests)
    endif()
endif()
