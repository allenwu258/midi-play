cmake_minimum_required(VERSION 3.24)
project(midi_play VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml)
find_package(WebP CONFIG REQUIRED)
option(MIDI_PLAY_ENABLE_VULKAN "Build the experimental Vulkan renderer" ON)
option(MIDI_PLAY_REQUIRE_VULKAN "Fail configuration if the Vulkan renderer is unavailable" OFF)
set(MIDI_PLAY_HAS_VULKAN OFF)
set(MIDI_PLAY_VULKAN_STATUS "disabled by configuration")
if (MIDI_PLAY_ENABLE_VULKAN)
    get_target_property(MIDI_PLAY_QT_GUI_FEATURES Qt6::Gui QT_ENABLED_PUBLIC_FEATURES)
    if (NOT "vulkan" IN_LIST MIDI_PLAY_QT_GUI_FEATURES)
        set(MIDI_PLAY_VULKAN_STATUS "Qt Gui was built without Vulkan support")
    else()
        find_package(Vulkan QUIET)
        if (NOT Vulkan_FOUND)
            set(MIDI_PLAY_VULKAN_STATUS "Vulkan SDK not found")
        else()
            find_program(GLSLANG_VALIDATOR glslangValidator
                HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
            if (NOT GLSLANG_VALIDATOR OR NOT EXISTS "${GLSLANG_VALIDATOR}")
                set(MIDI_PLAY_VULKAN_STATUS "glslangValidator not found")
            else()
                set(MIDI_PLAY_HAS_VULKAN ON)
                set(MIDI_PLAY_VULKAN_STATUS "enabled")
            endif()
        endif()
    endif()
    if (NOT MIDI_PLAY_HAS_VULKAN)
        message(WARNING "${MIDI_PLAY_VULKAN_STATUS}; building with the traditional renderer only")
    endif()
endif()
if (MIDI_PLAY_REQUIRE_VULKAN AND NOT MIDI_PLAY_HAS_VULKAN)
    message(FATAL_ERROR "Vulkan renderer required: ${MIDI_PLAY_VULKAN_STATUS}")
endif()
message(STATUS "MIDI Play Vulkan: ${MIDI_PLAY_VULKAN_STATUS} (requested: ${MIDI_PLAY_ENABLE_VULKAN})")

if (MIDI_PLAY_HAS_VULKAN)
    set(VULKAN_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/presentation/visualization/shaders")
    set(VULKAN_SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-vulkan-shaders")
    file(MAKE_DIRECTORY "${VULKAN_SHADER_BINARY_DIR}")
    set(VULKAN_SHADER_BINARIES)
    foreach(shader note.vert note.frag)
        add_custom_command(
            OUTPUT "${VULKAN_SHADER_BINARY_DIR}/${shader}.spv"
            COMMAND "${GLSLANG_VALIDATOR}" -V "${VULKAN_SHADER_SOURCE_DIR}/${shader}"
                    -o "${VULKAN_SHADER_BINARY_DIR}/${shader}.spv"
            DEPENDS "${VULKAN_SHADER_SOURCE_DIR}/${shader}"
            VERBATIM)
        list(APPEND VULKAN_SHADER_BINARIES "${VULKAN_SHADER_BINARY_DIR}/${shader}.spv")
    endforeach()
    add_custom_target(midi_play_vulkan_shaders DEPENDS ${VULKAN_SHADER_BINARIES})
endif()

set(MIDI_PLAY_COMMON_SOURCES
    src/app/playerapplicationservice.h
    src/app/playerapplicationservice.cpp
    src/app/isettingsstore.h
    src/app/settingsservice.h
    src/app/settingsservice.cpp
    src/domain/music/musicdocument.h
    src/domain/music/musicdocument.cpp
    src/domain/music/musicrhythmgrid.h
    src/domain/music/musicrhythmgrid.cpp
    src/domain/music/playbacktimeline.h
    src/domain/music/playbacktimeline.cpp
    src/domain/music/musicanalysis.h
    src/domain/music/musicanalysis.cpp
    src/domain/music/musicreadresult.h
    src/domain/music/scoreplaybackdom.h
    src/domain/music/scoreplaybackdom.cpp
    src/domain/visualization/visualchart.h
    src/domain/visualization/visualchart.cpp
    src/domain/visualization/activenotelookup.h
    src/domain/visualization/activenotelookup.cpp
    src/domain/visualization/playbackvisualizationprojector.h
    src/domain/visualization/playbackvisualizationprojector.cpp
    src/domain/visualization/noteoverlaplayout.h
    src/domain/visualization/noteoverlaplayout.cpp
    src/domain/visualization/visiblenoteindex.h
    src/domain/visualization/visiblenoteindex.cpp
    src/domain/visualization/visiblenotewindowcache.h
    src/domain/visualization/visiblenotewindowcache.cpp
    src/domain/visualization/playbackscenestate.h
    src/domain/playback/playbacktypes.h
    src/domain/playback/iplaybackaudioservice.h
    src/domain/playback/playbackmodel.h
    src/domain/playback/playbackmodel.cpp
    src/domain/playback/playbackcontext.h
    src/domain/playback/playbackcontext.cpp
    src/domain/playback/playbackeventsrenderer.h
    src/domain/playback/playbackeventsrenderer.cpp
    src/domain/playback/playbackeventscheduler.h
    src/domain/playback/playbackeventscheduler.cpp
    src/domain/playback/soundprofilerepository.h
    src/domain/playback/soundprofilerepository.cpp
    src/domain/playback/playbacksession.h
    src/domain/playback/playbacksession.cpp
    src/domain/playback/playbackpositionthrottler.h
    src/domain/playback/playbackpositionthrottler.cpp
    src/domain/playback/playbackclock.h
    src/domain/playback/playbackclock.cpp
    src/domain/playback/itransporteventsdispatcher.h
    src/domain/playback/playbackplayhead.h
    src/domain/playback/playbackplayhead.cpp
    src/domain/playback/playbackcontroller.h
    src/domain/playback/playbackcontroller.cpp
    src/domain/playback/metronometimeline.h
    src/domain/playback/metronometimeline.cpp
    src/domain/settings/playersettings.h
    src/domain/settings/playersettings.cpp
    src/domain/settings/graphicsmode.h
    src/domain/settings/thememode.h
    src/domain/settings/notecolormode.h
    src/domain/settings/titlebarmode.h
    src/infrastructure/musicxml/musicxmlreader.h
    src/infrastructure/musicxml/musicxmlreader.cpp
    src/infrastructure/audio/fluidsynthengine.h
    src/infrastructure/audio/fluidsynthengine.cpp
    src/infrastructure/audio/soundfontinspector.h
    src/infrastructure/audio/soundfontinspector.cpp
    src/infrastructure/audio/fluidsynthaudioservice.h
    src/infrastructure/audio/fluidsynthaudioservice.cpp
    src/infrastructure/audio/threadedplaybackaudioservice.h
    src/infrastructure/audio/threadedplaybackaudioservice.cpp
    src/infrastructure/resources/projectaudiosettings.h
    src/infrastructure/resources/projectaudiosettings.cpp
    src/infrastructure/settings/qsettingsstore.h
    src/infrastructure/settings/qsettingsstore.cpp
    src/infrastructure/readers/imusicreader.h
    src/infrastructure/readers/musicreaderregistry.h
    src/infrastructure/readers/musicreaderregistry.cpp
    src/infrastructure/readers/musicxmlreaderadapter.h
    src/infrastructure/readers/musicxmlreaderadapter.cpp
    src/infrastructure/readers/midireaderadapter.h
    src/infrastructure/readers/midireaderadapter.cpp
    src/infrastructure/midi/midireader.h
    src/infrastructure/midi/midireader.cpp
    src/infrastructure/midi/miditypes.h
    src/infrastructure/midi/midifileparser.h
    src/infrastructure/midi/midifileparser.cpp
    src/infrastructure/midi/midinormalizer.h
    src/infrastructure/midi/midinormalizer.cpp
    src/infrastructure/midi/mididocumentbuilder.h
    src/infrastructure/midi/mididocumentbuilder.cpp
    src/presentation/mainwindow.h
    src/presentation/mainwindow.cpp
    src/presentation/transport/playbackratecontrol.h
    src/presentation/transport/playbackratecontrol.cpp
    src/presentation/playbackmetadatapresenter.h
    src/presentation/playbackmetadatapresenter.cpp
    src/presentation/settings/settingsdialog.h
    src/presentation/settings/settingsdialog.cpp
    src/presentation/settings/soundfontsetup.h
    src/presentation/settings/soundfontsetup.cpp
    src/presentation/theme/apptheme.h
    src/presentation/theme/apptheme.cpp
    src/presentation/theme/themecontroller.h
    src/presentation/theme/themecontroller.cpp
    src/presentation/theme/themeicons.h
    src/presentation/theme/themeicons.cpp
    src/presentation/theme/widgetstyles.h
    src/presentation/theme/widgetstyles.cpp
    src/presentation/windowchrome/customtitlebar.h
    src/presentation/windowchrome/customtitlebar.cpp
    src/presentation/visualization/scenegeometry.h
    src/presentation/visualization/scenelayoutengine.h
    src/presentation/visualization/scenelayoutengine.cpp
    src/presentation/visualization/fallingnotesrenderer.h
    src/presentation/visualization/fallingnotesrenderer.cpp
    src/presentation/visualization/noterendercache.h
    src/presentation/visualization/noterendercache.cpp
    src/presentation/visualization/notematerial.h
    src/presentation/visualization/notematerial.cpp
    src/presentation/visualization/noteappearance.h
    src/presentation/visualization/noteappearance.cpp
    src/presentation/visualization/notecolorpalette.h
    src/presentation/visualization/notecolorpalette.cpp
    src/presentation/visualization/noteframestate.h
    src/presentation/visualization/noteframestate.cpp
    src/presentation/visualization/noterastercache.h
    src/presentation/visualization/noterastercache.cpp
    src/presentation/visualization/textlayoutcache.h
    src/presentation/visualization/textlayoutcache.cpp
    src/presentation/visualization/playbackoverlaytimeline.h
    src/presentation/visualization/playbackoverlaytimeline.cpp
    src/presentation/visualization/rasterrenderpolicy.h
    src/presentation/visualization/rasterrenderpolicy.cpp
    src/presentation/visualization/fallingnotesview.h
    src/presentation/visualization/fallingnotesview.cpp
    src/presentation/visualization/backgroundimageloader.h
    src/presentation/visualization/backgroundimageloader.cpp
)
if (MIDI_PLAY_HAS_VULKAN)
    list(APPEND MIDI_PLAY_COMMON_SOURCES
        src/presentation/visualization/fallingnotesvulkanwindow.h
        src/presentation/visualization/fallingnotesvulkanwindow.cpp
        src/presentation/visualization/vulkanscene.h
        src/presentation/visualization/vulkanscene.cpp)
endif()

add_library(midi_play_core STATIC ${MIDI_PLAY_COMMON_SOURCES})
qt_add_resources(midi_play_core midi_play_metronome_resources
    PREFIX "/midi_play/audio"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    FILES assets/metronome.sf2)
if (MIDI_PLAY_HAS_VULKAN)
    set_source_files_properties(${VULKAN_SHADER_BINARIES} PROPERTIES GENERATED TRUE)
    qt_add_resources(midi_play_core midi_play_vulkan_resources
        PREFIX "/midi_play/shaders"
        BASE "${VULKAN_SHADER_BINARY_DIR}"
        FILES ${VULKAN_SHADER_BINARIES})
endif()
target_include_directories(midi_play_core PUBLIC src)
target_link_libraries(midi_play_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Xml)
target_link_libraries(midi_play_core PRIVATE WebP::webp)
if (MIDI_PLAY_HAS_VULKAN)
    add_dependencies(midi_play_core midi_play_vulkan_shaders)
    target_link_libraries(midi_play_core PUBLIC Vulkan::Vulkan)
    target_include_directories(midi_play_core PUBLIC "${Vulkan_INCLUDE_DIRS}")
endif()
# This definition affects the public FallingNotesView layout and must reach
# consumers and AUTOMOC with the same value as the library itself.
target_compile_definitions(midi_play_core PUBLIC MIDI_PLAY_HAS_VULKAN=$<BOOL:${MIDI_PLAY_HAS_VULKAN}>)

qt_add_executable(midi_play src/main.cpp)
target_link_libraries(midi_play PRIVATE midi_play_core)
qt_add_resources(midi_play midi_play_branding_resources
    PREFIX "/midi_play/branding"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding"
    FILES assets/branding/midiplay.ico)
if (WIN32)
    enable_language(RC)
    set(MIDI_PLAY_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/midiplay.ico")
    set(MIDI_PLAY_ICON_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/midi_play_icon.rc")
    configure_file(cmake/WindowsAppIcon.rc.in "${MIDI_PLAY_ICON_RESOURCE}" @ONLY)
    set_source_files_properties("${MIDI_PLAY_ICON_RESOURCE}" PROPERTIES
        OBJECT_DEPENDS "${MIDI_PLAY_APP_ICON}")
    target_sources(midi_play PRIVATE "${MIDI_PLAY_ICON_RESOURCE}")
endif()

qt_add_executable(midi_play_cli src/cli/main.cpp)
target_link_libraries(midi_play_cli PRIVATE midi_play_core)
set_target_properties(midi_play PROPERTIES WIN32_EXECUTABLE TRUE)

set(FLUIDSYNTH_DLL "" CACHE FILEPATH "FluidSynth runtime DLL to copy beside the executable")
set(VCPKG_INSTALLED_DIR "" CACHE PATH
    "Optional vcpkg installed directory containing the x64-windows FluidSynth runtime")
if (NOT FLUIDSYNTH_DLL AND WIN32 AND DEFINED ENV{VCPKG_ROOT})
    set(FLUIDSYNTH_DLL "$ENV{VCPKG_ROOT}/installed/x64-windows/bin/libfluidsynth-3.dll")
endif()
if (NOT FLUIDSYNTH_DLL AND WIN32 AND VCPKG_INSTALLED_DIR)
    set(FLUIDSYNTH_DLL "${VCPKG_INSTALLED_DIR}/x64-windows/bin/libfluidsynth-3.dll")
endif()
if (EXISTS "${FLUIDSYNTH_DLL}")
    foreach (runtime_target IN ITEMS midi_play midi_play_cli)
        add_custom_command(TARGET ${runtime_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-DSOURCE_LIBRARY=${FLUIDSYNTH_DLL}"
                "-DDESTINATION_DIR=$<TARGET_FILE_DIR:${runtime_target}>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployFluidSynthRuntime.cmake"
            VERBATIM)
    endforeach()
elseif (WIN32)
    message(STATUS
        "FluidSynth runtime deployment disabled: set FLUIDSYNTH_DLL or VCPKG_ROOT "
        "(or VCPKG_INSTALLED_DIR) to an SF3-capable libfluidsynth-3.dll")
endif()

if (WIN32)
    target_compile_definitions(midi_play_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(GNUInstallDirs)
if (POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()
install(TARGETS midi_play midi_play_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if (WIN32 AND EXISTS "${FLUIDSYNTH_DLL}")
    install(CODE
        "execute_process(COMMAND \"${CMAKE_COMMAND}\"
            \"-DSOURCE_LIBRARY=${FLUIDSYNTH_DLL}\"
            \"-DDESTINATION_DIR=\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\"
            -P \"${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployFluidSynthRuntime.cmake\"
            COMMAND_ERROR_IS_FATAL ANY)")
endif()

if (WIN32)
    # The WebP target links sharpyuv; deploy both imported runtimes.
    add_custom_command(TARGET midi_play_core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:WebP::webp>" "$<TARGET_FILE:WebP::sharpyuv>"
            "$<TARGET_FILE_DIR:midi_play_core>"
        VERBATIM)
    install(FILES "$<TARGET_FILE:WebP::webp>" "$<TARGET_FILE:WebP::sharpyuv>"
        DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(midi_play_soundfont_setup_tests tests/soundfontsetuptests.cpp)
    target_link_libraries(midi_play_soundfont_setup_tests PRIVATE midi_play_core)
    add_test(NAME soundfont_setup COMMAND midi_play_soundfont_setup_tests -platform offscreen
        --fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/deployment.musicxml")
    if (TARGET Qt6::QOffscreenIntegrationPlugin)
        set_tests_properties(soundfont_setup PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
    endif()
    # External test data is opt-in and is never installed or bundled.
    set(MIDI_PLAY_TEST_SOUNDFONT "" CACHE FILEPATH "External SF2/SF3 for SoundFont setup integration tests")
    if (EXISTS "${MIDI_PLAY_TEST_SOUNDFONT}")
        add_test(NAME soundfont_setup_audio COMMAND midi_play_soundfont_setup_tests -platform offscreen
            --fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/deployment.musicxml"
            --soundfont "${MIDI_PLAY_TEST_SOUNDFONT}")
        if (TARGET Qt6::QOffscreenIntegrationPlugin)
            set_tests_properties(soundfont_setup_audio PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
        endif()
    endif()
    add_executable(midi_play_note_color_tests tests/notecolortests.cpp)
    target_link_libraries(midi_play_note_color_tests PRIVATE midi_play_core)
    add_test(NAME note_color_modes COMMAND midi_play_note_color_tests -platform offscreen)
    if (TARGET Qt6::QOffscreenIntegrationPlugin)
        set_tests_properties(note_color_modes PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
    endif()
    add_executable(midi_play_theme_tests tests/themetests.cpp)
    target_link_libraries(midi_play_theme_tests PRIVATE midi_play_core)
    add_test(NAME theme_persistence_and_runtime COMMAND midi_play_theme_tests -platform offscreen)
    if (TARGET Qt6::QOffscreenIntegrationPlugin)
        set_tests_properties(theme_persistence_and_runtime PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
    endif()
    add_executable(midi_play_note_visual_tests tests/notevisualtests.cpp)
    target_link_libraries(midi_play_note_visual_tests PRIVATE midi_play_core)
    target_link_libraries(midi_play_note_visual_tests PRIVATE WebP::webp)
    add_test(NAME note_visual_semantics_and_rendering COMMAND midi_play_note_visual_tests -platform offscreen)
    if (TARGET Qt6::QOffscreenIntegrationPlugin)
        set_tests_properties(note_visual_semantics_and_rendering PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
    endif()
    add_executable(midi_play_metronome_tests tests/metronometests.cpp)
    target_link_libraries(midi_play_metronome_tests PRIVATE midi_play_core)
    add_test(NAME metronome_timeline_and_readers COMMAND midi_play_metronome_tests)
    if (WIN32 AND EXISTS "${FLUIDSYNTH_DLL}")
        add_custom_command(TARGET midi_play_metronome_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-DSOURCE_LIBRARY=${FLUIDSYNTH_DLL}"
                "-DDESTINATION_DIR=$<TARGET_FILE_DIR:midi_play_metronome_tests>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployFluidSynthRuntime.cmake"
            VERBATIM)
        add_test(NAME metronome_fluidsynth_audio COMMAND midi_play_metronome_tests --audio)
    endif()
    add_executable(midi_play_presentation_tests tests/presentationtests.cpp)
    target_link_libraries(midi_play_presentation_tests PRIVATE midi_play_core)
    add_test(NAME presentation_backend_configuration
        COMMAND midi_play_presentation_tests -platform offscreen)
    if (TARGET Qt6::QOffscreenIntegrationPlugin)
        set_tests_properties(presentation_backend_configuration PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
    endif()

    add_executable(midi_play_soundfont_tests
        tests/soundfontinspectortests.cpp
        src/infrastructure/audio/soundfontinspector.h
        src/infrastructure/audio/soundfontinspector.cpp
    )
    target_include_directories(midi_play_soundfont_tests PRIVATE src)
    target_link_libraries(midi_play_soundfont_tests PRIVATE Qt6::Core)
    add_test(NAME soundfont_inspector COMMAND midi_play_soundfont_tests)

    add_executable(midi_play_visualization_tests
        src/presentation/theme/apptheme.h
        src/presentation/theme/apptheme.cpp
        tests/visualizationtests.cpp
        src/domain/music/musicdocument.h
        src/domain/music/musicdocument.cpp
        src/domain/music/musicrhythmgrid.h
        src/domain/music/musicrhythmgrid.cpp
        src/domain/music/musicanalysis.h
        src/domain/music/musicanalysis.cpp
        src/domain/visualization/visualchart.h
        src/domain/visualization/visualchart.cpp
        src/domain/visualization/activenotelookup.h
        src/domain/visualization/activenotelookup.cpp
        src/domain/visualization/playbackvisualizationprojector.h
        src/domain/visualization/playbackvisualizationprojector.cpp
        src/domain/visualization/noteoverlaplayout.h
        src/domain/visualization/noteoverlaplayout.cpp
        src/domain/visualization/visiblenoteindex.h
        src/domain/visualization/visiblenoteindex.cpp
        src/domain/visualization/visiblenotewindowcache.h
        src/domain/visualization/visiblenotewindowcache.cpp
        src/presentation/visualization/scenegeometry.h
        src/presentation/visualization/scenelayoutengine.h
        src/presentation/visualization/scenelayoutengine.cpp
        src/presentation/visualization/rasterrenderpolicy.h
        src/presentation/visualization/rasterrenderpolicy.cpp
        src/presentation/visualization/noterendercache.h
        src/presentation/visualization/noterendercache.cpp
        src/presentation/visualization/notematerial.h
        src/presentation/visualization/notematerial.cpp
        src/presentation/visualization/noteappearance.h
        src/presentation/visualization/noteappearance.cpp
        src/presentation/visualization/notecolorpalette.h
        src/presentation/visualization/notecolorpalette.cpp
        src/presentation/visualization/noteframestate.h
        src/presentation/visualization/noteframestate.cpp
        src/presentation/visualization/noterastercache.h
        src/presentation/visualization/noterastercache.cpp
        src/presentation/visualization/textlayoutcache.h
        src/presentation/visualization/textlayoutcache.cpp
        src/presentation/visualization/playbackoverlaytimeline.h
        src/presentation/visualization/playbackoverlaytimeline.cpp
        src/presentation/playbackmetadatapresenter.h
        src/presentation/playbackmetadatapresenter.cpp
    )
    target_include_directories(midi_play_visualization_tests PRIVATE src)
    target_link_libraries(midi_play_visualization_tests PRIVATE Qt6::Core Qt6::Gui)
    if (WIN32)
        target_compile_definitions(midi_play_visualization_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    add_test(NAME visualization_domain COMMAND midi_play_visualization_tests)

    add_executable(midi_play_playback_session_tests
        tests/playbacksessiontests.cpp
        src/domain/music/musicdocument.h
        src/domain/music/musicdocument.cpp
        src/domain/music/musicrhythmgrid.h
        src/domain/music/musicrhythmgrid.cpp
        src/domain/music/playbacktimeline.h
        src/domain/music/playbacktimeline.cpp
        src/domain/music/scoreplaybackdom.h
        src/domain/music/scoreplaybackdom.cpp
        src/domain/playback/playbacktypes.h
        src/domain/playback/iplaybackaudioservice.h
        src/domain/playback/playbackmodel.h
        src/domain/playback/playbackmodel.cpp
        src/domain/playback/playbackcontext.h
        src/domain/playback/playbackcontext.cpp
        src/domain/playback/playbackeventsrenderer.h
        src/domain/playback/playbackeventsrenderer.cpp
        src/domain/playback/playbackeventscheduler.h
        src/domain/playback/playbackeventscheduler.cpp
        src/domain/playback/playbackclock.h
        src/domain/playback/playbackclock.cpp
        src/domain/playback/playbackplayhead.h
        src/domain/playback/playbackplayhead.cpp
        src/domain/playback/playbacksession.h
        src/domain/playback/playbacksession.cpp
        src/domain/playback/playbackpositionthrottler.h
        src/domain/playback/playbackpositionthrottler.cpp
        src/domain/playback/playbackcontroller.h
        src/domain/playback/playbackcontroller.cpp
        src/domain/playback/metronometimeline.h
        src/domain/playback/metronometimeline.cpp
        src/domain/settings/playersettings.h
        src/domain/settings/playersettings.cpp
        src/app/isettingsstore.h
        src/app/settingsservice.h
        src/app/settingsservice.cpp
        src/infrastructure/settings/qsettingsstore.h
        src/infrastructure/settings/qsettingsstore.cpp
        src/infrastructure/audio/threadedplaybackaudioservice.h
        src/infrastructure/audio/threadedplaybackaudioservice.cpp
    )
    target_include_directories(midi_play_playback_session_tests PRIVATE src)
    target_link_libraries(midi_play_playback_session_tests PRIVATE Qt6::Core)
    if (WIN32)
        target_compile_definitions(midi_play_playback_session_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    add_test(NAME playback_session_transport COMMAND midi_play_playback_session_tests)
endif()
