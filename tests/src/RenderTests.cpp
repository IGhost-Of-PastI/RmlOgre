// tests/src/RenderTests.cpp
#define BOOST_TEST_MODULE RmlOgreRenderTests

#include <boost/test/unit_test.hpp>

#include "OgreRmlTestFixture.hpp"

using namespace rml_tests;

BOOST_GLOBAL_FIXTURE(OgreRmlTestFixture);

namespace {
	void check_render_test(const RenderTestSpec& spec) {
		const auto result = OgreRmlTestFixture::instance().run_render_test(spec);

		BOOST_TEST_MESSAGE(
			"[" << spec.name << "] "
			<< result.width << "x" << result.height
			<< " | diff=" << result.diff_pixels << "/" << result.total_pixels
			<< " (" << (result.diff_ratio * 100.0) << "%)"
			<< " | rmse=" << result.rmse
			<< " | max_delta=" << result.max_channel_delta);

		BOOST_CHECK_MESSAGE(result.passed, result.error_message);
	}

} // namespace

BOOST_AUTO_TEST_SUITE(RmlRenderSuite)

BOOST_AUTO_TEST_CASE(test_01_geometry) {
	check_render_test({
		.name = "render_test_01_geometry",
		.rml_filename = "render_test_01_geometry.rml",
		.reference_filename = "render_test_01_geometry.png",
		.compare_options = {.channel_tolerance = 2, .max_diff_ratio = 0.002, .max_rmse = 2.5 }
		});
}

BOOST_AUTO_TEST_CASE(test_02_textures) {
	check_render_test({
		.name = "render_test_02_textures",
		.rml_filename = "render_test_02_textures.rml",
		.reference_filename = "render_test_02_textures.png",
		.compare_options = {.channel_tolerance = 3, .max_diff_ratio = 0.005, .max_rmse = 3.5 }
		});
}

BOOST_AUTO_TEST_CASE(test_03_scissor_region) {
	check_render_test({
		.name = "render_test_03_scissor_region",
		.rml_filename = "render_test_03_scissor_region.rml",
		.reference_filename = "render_test_03_scissor_region.png",
		.compare_options = {.channel_tolerance = 2, .max_diff_ratio = 0.002, .max_rmse = 2.5 }
		});
}

BOOST_AUTO_TEST_CASE(test_04_compositor_rebuild) {
	check_render_test({
		.name = "render_test_04_compositor_rebuild",
		.rml_filename = "render_test_04_compositor_rebuild.rml",
		.reference_filename = "render_test_04_compositor_rebuild.png",
		.compare_options = {.channel_tolerance = 3, .max_diff_ratio = 0.005, .max_rmse = 3.5 },
		.warmup_frames = 3
		});
}

BOOST_AUTO_TEST_CASE(test_05_transform) {
	check_render_test({
		.name = "render_test_05_transform",
		.rml_filename = "render_test_05_transform.rml",
		.reference_filename = "render_test_05_transform.png",
		.compare_options = {.channel_tolerance = 4, .max_diff_ratio = 0.008, .max_rmse = 4.0 }
		});
}

BOOST_AUTO_TEST_CASE(test_06_clip_mask) {
	check_render_test({
		.name = "render_test_06_clip_mask",
		.rml_filename = "render_test_06_clip_mask.rml",
		.reference_filename = "render_test_06_clip_mask.png",
		.compare_options = {.channel_tolerance = 3, .max_diff_ratio = 0.005, .max_rmse = 3.5 }
		});
}

BOOST_AUTO_TEST_CASE(test_07_filters) {
	check_render_test({
		.name = "render_test_07_filters",
		.rml_filename = "render_test_07_filters.rml",
		.reference_filename = "render_test_07_filters.png",
		.compare_options = {.channel_tolerance = 6, .max_diff_ratio = 0.03, .max_rmse = 2.5 }
		});
}

BOOST_AUTO_TEST_CASE(test_08_render_to_texture) {
	check_render_test({
		.name = "render_test_08_render_to_texture",
		.rml_filename = "render_test_08_render_to_texture.rml",
		.reference_filename = "render_test_08_render_to_texture.png",
		.compare_options = {.channel_tolerance = 4, .max_diff_ratio = 0.008, .max_rmse = 4.0 }
		});
}

BOOST_AUTO_TEST_CASE(test_09_mask_image) {
	check_render_test({
		.name = "render_test_09_mask_image",
		.rml_filename = "render_test_09_mask_image.rml",
		.reference_filename = "render_test_09_mask_image.png",
		.compare_options = {.channel_tolerance = 4, .max_diff_ratio = 0.008, .max_rmse = 4.0 }
		});
}

BOOST_AUTO_TEST_CASE(test_10_shaders) {
	check_render_test({
		.name = "render_test_10_shaders",
		.rml_filename = "render_test_10_shaders.rml",
		.reference_filename = "render_test_10_shaders.png",
		.compare_options = {.channel_tolerance = 5, .max_diff_ratio = 0.01, .max_rmse = 5.0 }
		});
}

BOOST_AUTO_TEST_CASE(test_11_multithreaded_scene_manager) {
	check_render_test(RenderTestSpec{
		.name = "render_test_11_multithreaded_sm",
		.rml_filename = "render_test_07_filters.rml",
		.reference_filename = "render_test_07_filters.png",
		.compare_options = { .channel_tolerance = 6, .max_diff_ratio = 0.03, .max_rmse = 2.5 },
		.warmup_frames = 2u,
		.worker_threads = 2u,
		.include_scene_pass = true
		});
}

BOOST_AUTO_TEST_SUITE_END()