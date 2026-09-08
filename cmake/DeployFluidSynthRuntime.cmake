if (NOT DEFINED SOURCE_LIBRARY OR NOT EXISTS "${SOURCE_LIBRARY}")
    message(FATAL_ERROR "SOURCE_LIBRARY must identify an existing FluidSynth runtime")
endif()
if (NOT DEFINED DESTINATION_DIR OR DESTINATION_DIR STREQUAL "")
    message(FATAL_ERROR "DESTINATION_DIR is required")
endif()

get_filename_component(runtime_dir "${SOURCE_LIBRARY}" DIRECTORY)
get_filename_component(runtime_name "${SOURCE_LIBRARY}" NAME)
file(MAKE_DIRECTORY "${DESTINATION_DIR}")
file(COPY_FILE "${SOURCE_LIBRARY}"
     "${DESTINATION_DIR}/${runtime_name}" ONLY_IF_DIFFERENT)

file(GET_RUNTIME_DEPENDENCIES
    LIBRARIES "${SOURCE_LIBRARY}"
    DIRECTORIES "${runtime_dir}"
    RESOLVED_DEPENDENCIES_VAR resolved_dependencies
    UNRESOLVED_DEPENDENCIES_VAR unresolved_dependencies
    PRE_EXCLUDE_REGEXES
        "^api-ms-win-.*"
        "^ext-ms-win-.*"
    POST_EXCLUDE_REGEXES
        ".*[\\/]Windows[\\/]System32[\\/].*")

foreach (dependency IN LISTS resolved_dependencies)
    get_filename_component(dependency_dir "${dependency}" DIRECTORY)
    cmake_path(NORMAL_PATH dependency_dir OUTPUT_VARIABLE normalized_dependency_dir)
    string(TOLOWER "${normalized_dependency_dir}" normalized_dependency_dir_lower)
    get_filename_component(dependency_name "${dependency}" NAME)
    # GET_RUNTIME_DEPENDENCIES already returns the complete transitive closure.
    # Copy every non-system dependency, not only files beside FluidSynth: a
    # custom build may keep libsndfile/codecs in a separate directory.
    if (NOT normalized_dependency_dir_lower MATCHES "^[a-z]:[/\\\\]windows[/\\\\](system32|syswow64)([/\\\\]|$)")
        file(COPY_FILE "${dependency}"
             "${DESTINATION_DIR}/${dependency_name}" ONLY_IF_DIFFERENT)
    endif()
endforeach()

set(unresolved_codec_dependencies)
foreach (dependency IN LISTS unresolved_dependencies)
    string(TOLOWER "${dependency}" dependency_lower)
    if (dependency_lower MATCHES "^(lib)?(sndfile|ogg|vorbis|vorbisfile|vorbisenc|flac|opus|mpg123|mp3lame)[^/\\\\]*\\.dll$")
        list(APPEND unresolved_codec_dependencies "${dependency}")
    endif()
endforeach()
if (unresolved_codec_dependencies)
    list(JOIN unresolved_codec_dependencies ", " unresolved_text)
    message(FATAL_ERROR
        "FluidSynth has unresolved SF3 codec dependencies: ${unresolved_text}")
endif()
