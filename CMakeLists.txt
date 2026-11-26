cmake_minimum_required(VERSION 3.10)

project(RmlOgre VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Использовать чистый стандарт (-std=c++20 вместо -std=gnu++20)

option(BUILD_EXAMPLE "Build example executable" OFF)
option(BUILD_TESTS "Build tests executables" OFF)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
#list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(ogre-next CONFIG REQUIRED)
find_package(RmlUi REQUIRED REQUIRED)


add_library(RmlOgre)
add_library(RmlOgre::RmlOgre ALIAS RmlOgre)

set(RMLOGRE_MEDIA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/media)

target_compile_features(RmlOgre PUBLIC cxx_std_17)
target_compile_options(RmlOgre PRIVATE
	$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
		-Wall -Wextra -Wpedantic -Wno-unused-parameter>
	$<$<CXX_COMPILER_ID:MSVC>:
		/W4>
)

target_include_directories(RmlOgre
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(RmlOgre
	SYSTEM PRIVATE
		${OGRE_INCLUDE_DIR}
		${OGRE_INCLUDE_DIR}/Hlms/Common
)

target_sources(RmlOgre
	PRIVATE
		src/RmlOgre/Compositor/CompositorPassRml.cpp
		src/RmlOgre/Material.cpp
		src/RmlOgre/RenderInterface.cpp
		src/RmlOgre/RenderObject.cpp
		src/RmlOgre/filters.cpp
		src/RmlOgre/geometry.cpp
		src/RmlOgre/shaders.cpp
		
)

target_link_libraries(RmlOgre
	OGRE::OgreNextMain
	OGRE::OgreNextHlmsUnlit

	RmlUi::RmlUi
)

set(PUBLIC_HEADERS
	src/RmlOgre/CompositorPassGeometry.hpp
	src/RmlOgre/CompositorPassGeometryDef.hpp
	src/RmlOgre/RenderInterface.hpp
	src/RmlOgre/RenderObject.hpp
)
set_target_properties(RmlOgre
	PROPERTIES PUBLIC_HEADER "${PUBLIC_HEADERS}"
)

if(BUILD_EXAMPLE)
	add_subdirectory("example")
endif()
include(CTest)
if(BUILD_TESTS)
	add_subdirectory("tests")
endif()
