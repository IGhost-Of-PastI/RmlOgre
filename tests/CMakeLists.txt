find_package(PNG REQUIRED)
find_package(Boost REQUIRED COMPONENTS unit_test_framework gil)


add_executable(RmlOgreRenderTests
	src/ImageCompare.hpp
	src/ImageCompare.cpp
	src/OgreRmlTestFixture.hpp
	src/OgreRmlTestFixture.cpp
	src/RenderTests.cpp
)

target_compile_features(RmlOgreRenderTests PUBLIC cxx_std_20)
target_compile_options(RmlOgreRenderTests PRIVATE
	$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
		-Wall -Wextra -Wpedantic -Wno-unused-parameter>
	$<$<CXX_COMPILER_ID:MSVC>:
		/W4>
)

target_include_directories(RmlOgreRenderTests
	PRIVATE
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(RmlOgreRenderTests
	PRIVATE
		RmlUi::RmlUi
		RmlOgre::RmlOgre
		Boost::unit_test_framework
		PNG::PNG
		Boost::gil
		OGRE::OgreNextMain
		OGRE::OgreNextHlmsPbs
		OGRE::OgreNextHlmsUnlit
		OGRE::RenderSystem_Vulkan
)

target_compile_definitions(RmlOgreRenderTests
	PRIVATE
		RMLOGRE_MEDIA_DIR="${RMLOGRE_MEDIA_DIR}"
		RMLOGRE_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test_output"
)

# Если скрипты Rml.compositor / Rml.material лежат не в media, а в отдельной папке библиотеки:
if(DEFINED RMLOGRE_RESOURCES_DIR)
	target_compile_definitions(RmlOgreRenderTests
		PRIVATE
			RMLOGRE_RESOURCES_DIR="${RMLOGRE_RESOURCES_DIR}"
	)
endif()

set(RML_RENDER_TEST_CASES
	test_01_geometry
	test_02_textures
	test_03_scissor_region
	test_04_compositor_rebuild
	test_05_transform
	test_06_clip_mask
	test_07_filters
	test_08_render_to_texture
	test_09_mask_image
	test_10_shaders
)

foreach(TEST_CASE ${RML_RENDER_TEST_CASES})
	add_test(
		NAME "RmlOgre.${TEST_CASE}"
		COMMAND $<TARGET_FILE:RmlOgreRenderTests>
			--run_test=RmlRenderSuite/${TEST_CASE}
			--log_level=message
		WORKING_DIRECTORY $<TARGET_FILE_DIR:RmlOgreRenderTests>
	)
endforeach()