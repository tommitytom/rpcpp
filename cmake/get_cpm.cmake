# Bootstrap CPM.cmake (https://github.com/cpm-cmake/CPM.cmake).
# Downloads a pinned CPM release into the build tree (or CPM_SOURCE_CACHE)
# and includes it. Honours CPM_SOURCE_CACHE so packages are cached across
# build trees.

set(CPM_DOWNLOAD_VERSION 0.40.2)

if(CPM_SOURCE_CACHE)
    set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
    set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
    set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()

get_filename_component(CPM_DOWNLOAD_LOCATION "${CPM_DOWNLOAD_LOCATION}" ABSOLUTE)

if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")
    message(STATUS "Downloading CPM.cmake v${CPM_DOWNLOAD_VERSION}")
    file(DOWNLOAD
        "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
        "${CPM_DOWNLOAD_LOCATION}"
    )
endif()

include("${CPM_DOWNLOAD_LOCATION}")
