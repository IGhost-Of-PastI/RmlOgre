#pragma once

#include <OgreTextureGpu.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace rml_tests {

	struct ImageCompareOptions {
		std::uint8_t channel_tolerance = 3;
		double       max_diff_ratio = 0.005;
		double       max_rmse = 4.0;
		bool         compare_alpha = false;
	};

	struct ImageCompareResult {
		bool        passed = false;
		std::string error_message;

		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::size_t   total_pixels = 0;
		std::size_t   diff_pixels = 0;
		double        diff_ratio = 0.0;
		double        rmse = 0.0;
		int           max_channel_delta = 0;

		std::filesystem::path actual_dump_path;
		std::filesystem::path diff_dump_path;
	};

	std::pair<std::uint32_t, std::uint32_t> read_png_dimensions(
		const std::filesystem::path& path);

	ImageCompareResult compare_texture_with_png(
		Ogre::TextureGpu* texture,
		const std::filesystem::path& reference_path,
		const ImageCompareOptions& options,
		const std::string& test_name,
		const std::filesystem::path& output_dir);

} // namespace rml_tests