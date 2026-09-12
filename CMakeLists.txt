cmake_minimum_required(VERSION 3.24)
project(midi_play VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml)
find_package(Vulkan REQUIRED)
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if (NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator is required to build Vulkan shaders")
endif()

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

set(MIDI_PLAY_COMMON_SOURCES
    src/app/playerapplicationservice.h
    src/app/playerapplicationservice.cpp
    src/app/isettingsstore.h
    src/app/settingsservice.h
    src/app/settingsservice.cpp
    src/domain/music/musicdocument.h
    src/domain/music/musicdocument.cpp
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
    src/domain/settings/playersettings.h
    src/domain/settings/playersettings.cpp
    src/domain/settings/graphicsmode.h
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
    src/infrastructure/resources/defaultsoundfontlocator.h
    src/infrastructure/resources/defaultsoundfontlocator.cpp
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
    src/presentation/playbackmetadatapresenter.h
    src/presentation/playbackmetadatapresenter.cpp
    src/presentation/settings/settingsdialog.h
    src/presentation/settings/settingsdialog.cpp
    src/presentation/windowchrome/customtitlebar.h
    src/presentation/windowchrome/customtitlebar.cpp
    src/presentation/visualization/scenegeometry.h
    src/presentation/visualization/scenelayoutengine.h
    src/presentation/visualization/scenelayoutengine.cpp
    src/presentation/visualization/fallingnotesrenderer.h
    src/presentation/visualization/fallingnotesrenderer.cpp
    src/presentation/visualization/noterendercache.h
    src/presentation/visualization/noterendercache.cpp
    src/presentation/visualization/textlayoutcache.h
    src/presentation/visualization/textlayoutcache.cpp
    src/presentation/visualization/playbackoverlaytimeline.h
    src/presentation/visualization/playbackoverlaytimeline.cpp
    src/presentation/visualization/rasterrenderpolicy.h
    src/presentation/visualization/rasterrenderpolicy.cpp
    src/presentation/visualization/fallingnotesview.h
    src/presentation/visualization/fallingnotesview.cpp
    src/presentation/visualization/fallingnotesvulkanwindow.h
    src/presentation/visualization/fallingnotesvulkanwindow.cpp
    src/presentation/visualization/vulkanscene.h
    src/presentation/visualization/vulkanscene.cpp
)

add_library(midi_play_core STATIC ${MIDI_PLAY_COMMON_SOURCES})
set_source_files_properties(${VULKAN_SHADER_BINARIES} PROPERTIES GENERATED TRUE)
qt_add_resources(midi_play_core midi_play_vulkan_resources
    PREFIX "/midi_play/shaders"
    BASE "${VULKAN_SHADER_BINARY_DIR}"
    FILES ${VULKAN_SHADER_BINARIES})
target_include_directories(midi_play_core PUBLIC src)
target_link_libraries(midi_play_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Xml)
add_dependencies(midi_play_core midi_play_vulkan_shaders)
target_link_libraries(midi_play_core PUBLIC Vulkan::Vulkan)
target_include_directories(midi_play_core PUBLIC "${Vulkan_INCLUDE_DIRS}")

qt_add_executable(midi_play src/main.cpp)
target_link_libraries(midi_play PRIVATE midi_play_core)

qt_add_executable(midi_play_cli src/cli/main.cpp)
target_link_libraries(midi_play_cli PRIVATE midi_play_core)
set_target_properties(midi_play PROPERTIES WIN32_EXECUTABLE TRUE)

set(DEFAULT_SF2 "${CMAKE_CURRENT_SOURCE_DIR}/assets/midisound.sf2")
if (EXISTS "${DEFAULT_SF2}")
    foreach (runtime_target IN ITEMS midi_play midi_play_cli)
        add_custom_command(TARGET ${runtime_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${runtime_target}>/assets"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DEFAULT_SF2}"
                "$<TARGET_FILE_DIR:${runtime_target}>/assets/midisound.sf2")
    endforeach()
endif()

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
install(TARGETS midi_play midi_play_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if (EXISTS "${DEFAULT_SF2}")
    install(FILES "${DEFAULT_SF2}" DESTINATION "${CMAKE_INSTALL_BINDIR}/assets")
endif()
if (WIN32 AND EXISTS "${FLUIDSYNTH_DLL}")
    install(CODE
        "execute_process(COMMAND \"${CMAKE_COMMAND}\"
            \"-DSOURCE_LIBRARY=${FLUIDSYNTH_DLL}\"
            \"-DDESTINATION_DIR=\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\"
            -P \"${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployFluidSynthRuntime.cmake\"
            COMMAND_ERROR_IS_FATAL ANY)")
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(midi_play_soundfont_tests
        tests/soundfontinspectortests.cpp
        src/infrastructure/audio/soundfontinspector.h
        src/infrastructure/audio/soundfontinspector.cpp
    )
    target_include_directories(midi_play_soundfont_tests PRIVATE src)
    target_link_libraries(midi_play_soundfont_tests PRIVATE Qt6::Core)
    add_test(NAME soundfont_inspector COMMAND midi_play_soundfont_tests)

    add_executable(midi_play_visualization_tests
        tests/visualizationtests.cpp
        src/domain/music/musicdocument.h
        src/domain/music/musicdocument.cpp
        src/domain/music/musicanalysis.h
        src/domain/music/musicanalysis.cpp
        src/domain/visualization/visualchart.h
        src/domain/visualization/visualchart.cpp
        src/domain/visualization/activenotelookup.h
        src/domain/visualization/activenotelookup.cpp
        src/domain/visualization/playbackvisualizationprojector.h
        src/domain/visualization/playbackvisualizationprojector.cpp
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
