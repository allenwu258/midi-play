cmake_minimum_required(VERSION 3.24)
project(midi_play VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml)

qt_add_executable(midi_play
    src/main.cpp
    src/app/playerapplicationservice.h
    src/app/playerapplicationservice.cpp
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
    src/infrastructure/musicxml/musicxmlreader.h
    src/infrastructure/musicxml/musicxmlreader.cpp
    src/infrastructure/audio/fluidsynthengine.h
    src/infrastructure/audio/fluidsynthengine.cpp
    src/infrastructure/audio/fluidsynthaudioservice.h
    src/infrastructure/audio/fluidsynthaudioservice.cpp
    src/infrastructure/audio/threadedplaybackaudioservice.h
    src/infrastructure/audio/threadedplaybackaudioservice.cpp
    src/infrastructure/resources/projectaudiosettings.h
    src/infrastructure/resources/projectaudiosettings.cpp
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
    src/presentation/visualization/scenegeometry.h
    src/presentation/visualization/scenelayoutengine.h
    src/presentation/visualization/scenelayoutengine.cpp
    src/presentation/visualization/fallingnotesrenderer.h
    src/presentation/visualization/fallingnotesrenderer.cpp
    src/presentation/visualization/rasterrenderpolicy.h
    src/presentation/visualization/rasterrenderpolicy.cpp
    src/presentation/visualization/fallingnotesview.h
    src/presentation/visualization/fallingnotesview.cpp
)

target_include_directories(midi_play PRIVATE src)
target_link_libraries(midi_play PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Xml)

set(DEFAULT_SF2 "${CMAKE_CURRENT_SOURCE_DIR}/assets/midisound.sf2")
if (EXISTS "${DEFAULT_SF2}")
    add_custom_command(TARGET midi_play POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:midi_play>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DEFAULT_SF2}"
            "$<TARGET_FILE_DIR:midi_play>/assets/midisound.sf2")
endif()

set(FLUIDSYNTH_DLL "" CACHE FILEPATH "FluidSynth runtime DLL to copy beside the executable")
if (NOT FLUIDSYNTH_DLL AND WIN32 AND DEFINED ENV{VCPKG_ROOT})
    set(FLUIDSYNTH_DLL "$ENV{VCPKG_ROOT}/installed/x64-windows/bin/libfluidsynth-3.dll")
endif()
if (NOT FLUIDSYNTH_DLL AND WIN32)
    set(FLUIDSYNTH_DLL "C:/SDK/VC/vcpkg/installed/x64-windows/bin/libfluidsynth-3.dll")
endif()
if (EXISTS "${FLUIDSYNTH_DLL}")
    add_custom_command(TARGET midi_play POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FLUIDSYNTH_DLL}"
            "$<TARGET_FILE_DIR:midi_play>/libfluidsynth-3.dll")
endif()

if (WIN32)
    target_compile_definitions(midi_play PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(GNUInstallDirs)
install(TARGETS midi_play RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if (EXISTS "${DEFAULT_SF2}")
    install(FILES "${DEFAULT_SF2}" DESTINATION "${CMAKE_INSTALL_BINDIR}/assets")
endif()

include(CTest)
if (BUILD_TESTING)
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
        src/presentation/visualization/scenegeometry.h
        src/presentation/visualization/scenelayoutengine.h
        src/presentation/visualization/scenelayoutengine.cpp
        src/presentation/visualization/rasterrenderpolicy.h
        src/presentation/visualization/rasterrenderpolicy.cpp
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
