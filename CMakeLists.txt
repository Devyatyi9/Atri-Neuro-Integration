cmake_minimum_required(VERSION 3.30)

project(AtriNeuroIntegration LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ATRI Neuro Integration targets Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "ATRI requires a 32-bit (Win32/x86) build. Configure with -A Win32.")
endif()

option(ATRI_BUILD_PROTOTYPE "Build the legacy integration prototype" OFF)
option(ATRI_CONFIGURE_ALL_TARGETS "Configure clean and prototype targets together" OFF)

function(atri_add_version_resource target original_filename file_description)
    set(ATRI_ORIGINAL_FILENAME "${original_filename}")
    set(ATRI_FILE_DESCRIPTION "${file_description}")
    set(resource_file "${CMAKE_CURRENT_BINARY_DIR}/resources/${target}.rc")
    configure_file(resources/windows-version.rc.in "${resource_file}" @ONLY)
    target_sources(${target} PRIVATE "${resource_file}")
endfunction()

set(NEURO_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(NEURO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NEURO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENABLE_PERMESSAGE_DEFLATE OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/neurosdk EXCLUDE_FROM_ALL)

add_library(atri_version_proxy SHARED
    src/proxy/dllmain.cpp
    src/proxy/Source.def
)
set_target_properties(atri_version_proxy PROPERTIES
    OUTPUT_NAME version
)
target_compile_definitions(atri_version_proxy PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)
target_link_options(atri_version_proxy PRIVATE
    "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/proxy/Source.def"
)

if(NOT ATRI_BUILD_PROTOTYPE OR ATRI_CONFIGURE_ALL_TARGETS)
    add_library(atri_neuro_integration SHARED
        src/integration/dllmain.cpp
        src/integration/PluginLifecycle.cpp
        src/integration/PluginLifecycle.h
        src/integration/SelfLink.cpp
        src/integration/SelfLink.h
        src/integration/neuro.def
        lib/krkr-sdk/tp_stub.cpp
    )
    set_target_properties(atri_neuro_integration PROPERTIES
        OUTPUT_NAME Neuro-Integration
    )
    target_compile_definitions(atri_neuro_integration PRIVATE
        UNICODE
        _UNICODE
    )
    target_include_directories(atri_neuro_integration PRIVATE
        lib/krkr-sdk
    )
    target_link_options(atri_neuro_integration PRIVATE
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/integration/neuro.def"
    )
    atri_add_version_resource(
        atri_neuro_integration
        "Neuro-Integration.dll"
        "ATRI Neuro Integration"
    )
endif()

if(ATRI_BUILD_PROTOTYPE)
    add_library(atri_neuro_prototype SHARED
        prototype/dllmain.cpp
        prototype/PluginLifecycle.cpp
        prototype/ContextMonitor.cpp
        prototype/Logging.cpp
        prototype/NeuroClient.cpp
        prototype/NeuroConsole.cpp
        prototype/NeuroTCPServer.cpp
        prototype/SelfLink.cpp
        prototype/ScriptingWindow.cpp
        prototype/TJSBindings.cpp
        prototype/TJSExecutor.cpp
        prototype/UtfConversion.cpp
        prototype/neuro.def
        lib/krkr-sdk/tp_stub.cpp
        lib/ncbind/ncbind_auto.cpp
    )
    set_target_properties(atri_neuro_prototype PROPERTIES
        OUTPUT_NAME Neuro-Integration
    )
    target_compile_definitions(atri_neuro_prototype PRIVATE
        WIN32
        INTEGRATIONTEST_EXPORTS
        UNICODE
        _UNICODE
    )
    target_include_directories(atri_neuro_prototype PRIVATE
        lib/krkr-sdk
        lib/ncbind
    )
    target_link_libraries(atri_neuro_prototype PRIVATE
        neurosdk
    )
    target_link_options(atri_neuro_prototype PRIVATE
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/prototype/neuro.def"
        /NODEFAULTLIB:LIBCMT
    )
    atri_add_version_resource(
        atri_neuro_prototype
        "Neuro-Integration.dll"
        "ATRI Neuro Integration Prototype"
    )
endif()
