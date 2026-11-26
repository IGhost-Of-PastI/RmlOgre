cmake_minimum_required(VERSION 3.25)
project(RmlOgre VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RMLOGRE_BUILD_TESTS "Build RmlOgre render tests" OFF)
option(RMLOGRE_BUILD_EXAMPLE "Build RmlOgre example executable" OFF)
option(RMLOGRE_INSTALL_MEDIA "Install RmlOgre shaders and materials" OFF)

find_package(ogre-next CONFIG REQUIRED)
find_package(RmlUi CONFIG REQUIRED)

# ==============================================================================
# Таргет: RmlOgre::RmlOgre (строго STATIC)
# ==============================================================================
add_library(RmlOgre STATIC)
add_library(RmlOgre::RmlOgre ALIAS RmlOgre)

target_compile_features(RmlOgre PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(RmlOgre PUBLIC /utf-8)
endif()

target_compile_options(RmlOgre PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -Wall -Wextra -Wpedantic -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

target_sources(RmlOgre
    PRIVATE
        src/RmlOgre/Compositor/CompositorPassRml.cpp
        src/RmlOgre/Material.cpp
        src/RmlOgre/RenderInterface.cpp
        src/RmlOgre/RenderObject.cpp
        src/RmlOgre/filters.cpp
        src/RmlOgre/geometry.cpp
        src/RmlOgre/geometry.hpp
        src/RmlOgre/shaders.cpp
        src/RmlOgre/shaders.hpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS src
        FILES
            src/RmlOgre/Compositor/CompositorPassRml.hpp
            src/RmlOgre/Compositor/CompositorPassRmlDef.hpp
            src/RmlOgre/FilterMaker.hpp
            src/RmlOgre/Material.hpp
            src/RmlOgre/ObjectIndex.hpp
            src/RmlOgre/RenderInterface.hpp
            src/RmlOgre/RenderObject.hpp
            src/RmlOgre/RmlCommandBuffer.hpp
            src/RmlOgre/ShaderMaker.hpp
            src/RmlOgre/filters.hpp
)

target_link_libraries(RmlOgre
    PUBLIC
        OGRE::OgreNextMain
        OGRE::OgreNextHlmsUnlit
        RmlUi::RmlUi
)

# Для tests/ и example/ внутри репозитория
set(RMLOGRE_MEDIA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/media")

if(RMLOGRE_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()

if(RMLOGRE_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

# ==============================================================================
# Установка (Install) для vcpkg overlay
# ==============================================================================
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS RmlOgre
    EXPORT RmlOgreTargets
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(RMLOGRE_INSTALL_MEDIA)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/media/scripts/materials/Rml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/RmlOgre/media/scripts/materials"
    )
endif()

install(EXPORT RmlOgreTargets
    NAMESPACE RmlOgre::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/RmlOgre"
)

set(RMLOGRE_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/RmlOgre-config.cmake")
set(RMLOGRE_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/RmlOgre-config-version.cmake")

file(WRITE "${RMLOGRE_CONFIG_FILE}"
"include(CMakeFindDependencyMacro)\n"
"if(NOT TARGET OGRE::OgreNextMain)\n"
"    find_dependency(ogre-next CONFIG)\n"
"endif()\n"
"if(NOT TARGET RmlUi::Core AND NOT TARGET RmlUi::RmlUi)\n"
"    find_dependency(RmlUi CONFIG)\n"
"endif()\n"
"include(\"\${CMAKE_CURRENT_LIST_DIR}/RmlOgreTargets.cmake\")\n"
"\n"
"if(EXISTS \"\${CMAKE_CURRENT_LIST_DIR}/media\")\n"
"    set(RMLOGRE_MEDIA_DIR \"\${CMAKE_CURRENT_LIST_DIR}/media\")\n"
"elseif(EXISTS \"\${CMAKE_CURRENT_LIST_DIR}/../../../${CMAKE_INSTALL_DATADIR}/RmlOgre/media\")\n"
"    get_filename_component(RMLOGRE_MEDIA_DIR \"\${CMAKE_CURRENT_LIST_DIR}/../../../${CMAKE_INSTALL_DATADIR}/RmlOgre/media\" ABSOLUTE)\n"
"endif()\n"
)

write_basic_package_version_file("${RMLOGRE_VERSION_FILE}"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${RMLOGRE_CONFIG_FILE}"
    "${RMLOGRE_VERSION_FILE}"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/RmlOgre"
)