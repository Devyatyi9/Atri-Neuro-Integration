cmake_minimum_required(VERSION 3.30)

project(AtriNeuroIntegration LANGUAGES C CXX)

# Pinned rather than left to each compiler: MSVC builds this at C++14 by
# default while clang-cl, which clang-tidy runs, reads the same code as C++17.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "ATRI Neuro Integration targets Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "ATRI requires a 32-bit (Win32/x86) build. Configure with -A Win32.")
endif()

option(ATRI_BUILD_PROTOTYPE "Build the legacy integration prototype" OFF)
option(ATRI_CONFIGURE_ALL_TARGETS "Configure clean and prototype targets together" OFF)
# AddressSanitizer for the prototype only, and only in its own build directory:
# the instrumented DLL is for hunting memory corruption inside the game, not for
# everyday runs. The x86 runtime lives beside the x86 compiler
# (VC/Tools/MSVC/<ver>/bin/Hostx64/x86/clang_rt.asan_dynamic-i386.dll) and has to
# sit next to the game executable, which Tools/deploy-asan-runtime.ps1 does.
option(ATRI_SANITIZE "Build the prototype with AddressSanitizer" OFF)

function(atri_add_version_resource target original_filename file_description product_name version)
	set(ATRI_ORIGINAL_FILENAME "${original_filename}")
	set(ATRI_FILE_DESCRIPTION "${file_description}")
	set(ATRI_PRODUCT_NAME "${product_name}")
	string(REPLACE "." "," ATRI_FILE_VERSION "${version}")
	set(ATRI_FILE_VERSION_STRING "${version}")
    set(resource_file "${CMAKE_CURRENT_BINARY_DIR}/resources/${target}.rc")
    configure_file(resources/windows-version.rc.in "${resource_file}" @ONLY)
    target_sources(${target} PRIVATE "${resource_file}")
endfunction()

add_library(atri_version_proxy SHARED
    src/proxy/dllmain.cpp
    src/proxy/Source.def
    lib/krkr-sdk/tp_stub.cpp
)
set_target_properties(atri_version_proxy PROPERTIES
    OUTPUT_NAME version
)
target_compile_definitions(atri_version_proxy PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)
target_include_directories(atri_version_proxy PRIVATE
    lib/krkr-sdk
)
target_link_options(atri_version_proxy PRIVATE
    "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/proxy/Source.def"
)
atri_add_version_resource(
    atri_version_proxy
    "version.dll"
    "ATRI Neuro Bootstrap"
    "ATRI Neuro Bootstrap"
    "1.0.0.0"
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
        WIN32_LEAN_AND_MEAN
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
        "ATRI Neuro Integration"
        "0.1.0.0"
    )
endif()

if(ATRI_BUILD_PROTOTYPE)
    if(ATRI_SANITIZE AND MSVC)
        add_compile_options(/fsanitize=address /Zi)
        add_link_options(/DEBUG /INCREMENTAL:NO)
        message(STATUS "Prototype: AddressSanitizer enabled")
    endif()
	add_library(atri_neuro_prototype SHARED
        prototype/dllmain.cpp
        prototype/Logging.cpp
        prototype/plugin/PluginLifecycle.cpp
        prototype/plugin/TJSBindings.cpp
        prototype/plugin/TJSExecutor.cpp
        prototype/tooling/NeuroConsole.cpp
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
        WIN32_LEAN_AND_MEAN
        INTEGRATIONTEST_EXPORTS
        UNICODE
        _UNICODE
        # fopen and getenv are standard C; the deprecation is Microsoft's
        # opinion, and the _s replacements it names are neither standard nor
        # simpler. Set on the target so it does not drift into being a #define
        # in whichever file was edited last.
        _CRT_SECURE_NO_WARNINGS
    )
    target_include_directories(atri_neuro_prototype PRIVATE
        prototype
        prototype/plugin
        prototype/tooling
        lib/krkr-sdk
        lib/ncbind
    )
    target_link_options(atri_neuro_prototype PRIVATE
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/prototype/neuro.def"
        /NODEFAULTLIB:LIBCMT
    )
    atri_add_version_resource(
        atri_neuro_prototype
        "Neuro-Integration.dll"
        "ATRI Neuro Integration Prototype"
        "ATRI Neuro Integration Prototype"
        "0.1.0.0"
    )
endif()
