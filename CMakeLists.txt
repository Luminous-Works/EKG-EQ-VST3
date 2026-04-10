cmake_minimum_required(VERSION 3.25.0)
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.13 CACHE STRING "")

# ── Locate the VST3 SDK ─────────────────────────────────────────────
if(NOT vst3sdk_SOURCE_DIR)
    set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vst3sdk")
endif()

# ── Project ─────────────────────────────────────────────────────────
project(ekgeq-vst3
    VERSION 1.0.0.0
    DESCRIPTION "EKG-EQ Master EQ VST3 Plugin — Lumina Aerospace"
)

# ── SDK options: no examples, no VSTGUI ────────────────────────────
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES 0 CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES 0 CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT       0 CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR           0 CACHE BOOL "" FORCE)
set(SMTG_CREATE_MODULE_INFO          0 CACHE BOOL "" FORCE)

# ── Pull in the SDK ─────────────────────────────────────────────────
add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

# ── Plugin sources ──────────────────────────────────────────────────
set(EKGEQ_SOURCES
    ekgeq-plugin/source/biquad.h
    ekgeq-plugin/source/ekgeq_ids.h
    ekgeq-plugin/source/version.h
    ekgeq-plugin/source/ekgeq_processor.h
    ekgeq-plugin/source/ekgeq_processor.cpp
    ekgeq-plugin/source/ekgeq_controller.h
    ekgeq-plugin/source/ekgeq_controller.cpp
    ekgeq-plugin/source/ekgeq_editor.h
    ekgeq-plugin/source/ekgeq_editor.cpp
    ekgeq-plugin/source/ekgeq_factory.cpp
)

smtg_add_vst3plugin(ekgeq
    PACKAGE_NAME "EKG-EQ"
    SOURCES_LIST ${EKGEQ_SOURCES}
)

target_compile_features(ekgeq PUBLIC cxx_std_17)

# WebView2 SDK
target_include_directories(ekgeq PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ekgeq-plugin/webview2/include
)

target_link_libraries(ekgeq PRIVATE
    sdk
    comctl32
    gdiplus
    ${CMAKE_CURRENT_SOURCE_DIR}/ekgeq-plugin/webview2/lib/libWebView2Loader.a
)

# Static runtime — no external DLL dependencies
if(MINGW)
    target_link_options(ekgeq PRIVATE -static-libgcc -static-libstdc++ -static)
elseif(MSVC)
    set_property(TARGET ekgeq PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Copy UI files (HTML + JS) into the VST3 bundle Resources/ui/
if(WIN32)
    add_custom_command(TARGET ekgeq POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:ekgeq>/../../Resources/ui"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/ekgeq-plugin/ui/index.html"
            "$<TARGET_FILE_DIR:ekgeq>/../../Resources/ui/index.html"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/ekgeq-plugin/ui/ekgeq_dsp_engine.js"
            "$<TARGET_FILE_DIR:ekgeq>/../../Resources/ui/ekgeq_dsp_engine.js"
        COMMENT "Copying EKG-EQ UI files to VST3 bundle"
    )
endif()

# Version header (generates projectversion.h in build dir)
smtg_target_configure_version_file(ekgeq)

# Windows: version resource + export
if(SMTG_WIN)
    target_sources(ekgeq PRIVATE
        ekgeq-plugin/resource/ekgeq.rc
    )
endif()

# ── Standalone render test (Windows only) ───────────────────────────────
if(WIN32)
    add_executable(ekgeq_test WIN32 standalone_test/main.cpp)
    target_compile_features(ekgeq_test PUBLIC cxx_std_17)
    target_link_libraries(ekgeq_test PRIVATE gdiplus)
    if(MSVC)
        set_property(TARGET ekgeq_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(MINGW)
        target_link_options(ekgeq_test PRIVATE -static-libgcc -static-libstdc++ -static -mwindows)
    endif()
endif()

# macOS bundle
if(SMTG_MAC)
    smtg_target_set_bundle(ekgeq
        BUNDLE_IDENTIFIER "com.luminousworks.auratone.ekgeq"
        COMPANY_NAME "Lumina Aerospace"
    )
endif()
