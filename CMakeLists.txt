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

set(NEURO_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(NEURO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NEURO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENABLE_PERMESSAGE_DEFLATE OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/neurosdk EXCLUDE_FROM_ALL)

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
        prototype/actions/ActionRouter.cpp
        prototype/actions/ActionRouter.h
        prototype/actions/common/ActionCommon.cpp
        prototype/actions/common/ActionCommon.h
        prototype/actions/common/ActionJson.cpp
        prototype/actions/common/ActionJson.h
        prototype/actions/common/ActionSchemas.h
        prototype/actions/common/ActionSet.h
        prototype/actions/common/ParseNumber.cpp
        prototype/actions/common/SlotPayload.cpp
        prototype/actions/common/ParseNumber.h
        prototype/actions/probes/ActionProbes.cpp
        prototype/actions/probes/ActionProbes.h
        prototype/actions/probes/TJSQuery.cpp
        prototype/actions/probes/TJSQuery.h
        prototype/actions/screens/ScreenBacklog.cpp
        prototype/actions/screens/ScreenBacklog.h
        prototype/actions/screens/ScreenGallery.cpp
        prototype/actions/screens/ScreenGallery.h
        prototype/actions/screens/ScreenMenu.cpp
        prototype/actions/screens/ScreenMenu.h
        prototype/actions/screens/ScreenPlay.cpp
        prototype/actions/screens/ScreenPlay.h
        prototype/actions/screens/ScreenSlots.cpp
        prototype/actions/screens/ScreenSlots.h
        prototype/actions/screens/ScreenTitle.cpp
        prototype/actions/screens/ScreenTitle.h
        prototype/plugin/PluginLifecycle.cpp
        prototype/neuro/ContextPayload.cpp
        prototype/neuro/ContextPayload.h
        prototype/Logging.cpp
        prototype/neuro/NeuroClient.cpp
        prototype/tooling/GameDebugLog.cpp
        prototype/tooling/NeuroConsole.cpp
        prototype/tooling/NeuroTCPServer.cpp
        prototype/tooling/ScriptingWindow.cpp
        prototype/plugin/TJSBindings.cpp
        prototype/plugin/TJSExecutor.cpp
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
    )
    target_include_directories(atri_neuro_prototype PRIVATE
        prototype
        prototype/actions
        prototype/actions/common
        prototype/actions/probes
        prototype/actions/screens
        prototype/plugin
        prototype/neuro
        prototype/tooling
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
        "ATRI Neuro Integration Prototype"
        "0.1.0.0"
    )

    # Only the parts that need neither the game nor the engine are tested here.
    # That is the whole point of keeping them apart: these run in milliseconds
    # from a plain build, with no install, no window, and no waiting, which is
    # what makes it reasonable to run them on every change. Anything that talks
    # to the game is checked by prototype/tooling/neuro_harness_test.py against
    # a running install instead.
    enable_testing()
    add_executable(atri_prototype_tests
        prototype/actions/common/ActionJson.cpp
        prototype/actions/common/ParseNumber.cpp
        prototype/actions/common/SlotPayload.cpp
        prototype/tests/ActionJsonTests.cpp
        prototype/tests/ParseNumberTests.cpp
        prototype/tests/SlotPayloadTests.cpp
        prototype/tests/TestMain.cpp
        prototype/tests/TestMain.h
    )
    set_target_properties(atri_prototype_tests PROPERTIES
        OUTPUT_NAME prototype-tests
    )
    target_include_directories(atri_prototype_tests PRIVATE
        prototype/actions/common
        prototype/tests
    )
    add_test(NAME prototype-units COMMAND atri_prototype_tests)
endif()
