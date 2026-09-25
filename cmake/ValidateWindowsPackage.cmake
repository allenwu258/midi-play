cmake_minimum_required(VERSION 3.24)

if (NOT IS_DIRECTORY "${PACKAGE_DIR}" OR NOT EXISTS "${DUMPBIN_EXE}")
    message(FATAL_ERROR "PACKAGE_DIR and DUMPBIN_EXE must identify the package and MSVC dumpbin")
endif()
foreach (required IN ITEMS midi_play.exe midi_play_cli.exe Qt6Core.dll Qt6Gui.dll
         Qt6Widgets.dll Qt6Xml.dll platforms/qwindows.dll libfluidsynth-3.dll
         libwebp.dll
         vcruntime140.dll vcruntime140_1.dll msvcp140.dll)
    if (NOT EXISTS "${PACKAGE_DIR}/${required}")
        message(FATAL_ERROR "Incomplete Windows package: missing ${required}")
    endif()
endforeach()

# Musical SoundFonts are user-provided. Catch stale or accidentally copied
# resources; the tiny procedural metronome remains embedded in the executable.
file(GLOB_RECURSE unexpected_soundfonts "${PACKAGE_DIR}/*.sf2" "${PACKAGE_DIR}/*.sf3")
if (unexpected_soundfonts)
    message(FATAL_ERROR "Release packages must not bundle SoundFonts: ${unexpected_soundfonts}")
endif()

# Scan every plugin as a root, including FluidSynth which is loaded via QLibrary.
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "dumpbin")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${DUMPBIN_EXE}")
file(GLOB_RECURSE package_libraries "${PACKAGE_DIR}/*.dll")
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${PACKAGE_DIR}/midi_play.exe" "${PACKAGE_DIR}/midi_play_cli.exe"
    LIBRARIES ${package_libraries}
    DIRECTORIES "${PACKAGE_DIR}"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    CONFLICTING_DEPENDENCIES_PREFIX conflicts
    PRE_EXCLUDE_REGEXES "^api-ms-.*" "^ext-ms-.*" "^vulkan-1\\.dll$"
    POST_INCLUDE_REGEXES ".*[/\\\\](msvcp140[^/\\\\]*|vcruntime140[^/\\\\]*|vccorlib140|concrt140)\\.dll$"
    # Do not traverse Windows internals or resolve their private API sets.
    POST_EXCLUDE_REGEXES ".*[/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*")
if (unresolved)
    message(FATAL_ERROR "Incomplete dependency closure: unresolved=${unresolved}")
endif()

# CMake resolves plugin imports against System32 before DIRECTORIES. The Windows
# application uses its app-local CRT; allow only that specific resolution conflict.
foreach (name IN LISTS conflicts_FILENAMES)
    string(TOLOWER "${name}" lower_name)
    if (NOT lower_name MATCHES "^(msvcp140.*|vcruntime140.*|vccorlib140|concrt140)\\.dll$"
        OR NOT EXISTS "${PACKAGE_DIR}/${name}")
        message(FATAL_ERROR "Conflicting dependency: ${name}: ${conflicts_${name}}")
    endif()
    list(APPEND resolved ${conflicts_${name}})
endforeach()

file(TO_CMAKE_PATH "$ENV{SystemRoot}" system_root)
string(TOLOWER "${system_root}/System32" system_dir)
file(TO_CMAKE_PATH "${PACKAGE_DIR}" package_root)
string(TOLOWER "${package_root}" package_root)
foreach (dependency IN LISTS resolved)
    file(TO_CMAKE_PATH "${dependency}" normalized)
    string(TOLOWER "${normalized}" normalized)
    cmake_path(IS_PREFIX package_root "${normalized}" NORMALIZE in_package)
    cmake_path(IS_PREFIX system_dir "${normalized}" NORMALIZE in_system)
    if (NOT in_package AND NOT in_system)
        message(FATAL_ERROR "Package depends on an external development file: ${dependency}")
    endif()
    get_filename_component(dependency_name "${normalized}" NAME)
    if (dependency_name MATCHES "^(msvcp140.*|vcruntime140.*|vccorlib140|concrt140)\\.dll$"
        AND NOT EXISTS "${PACKAGE_DIR}/${dependency_name}")
        message(FATAL_ERROR "MSVC runtime must be deployed app-local: ${dependency_name}")
    endif()
endforeach()
message(STATUS "Windows package dependency closure verified")
