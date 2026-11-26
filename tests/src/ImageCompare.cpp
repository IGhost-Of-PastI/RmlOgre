// tests/src/ImageCompare.cpp
#include "ImageCompare.hpp"

#include <boost/gil.hpp>
#include <boost/gil/extension/io/png.hpp>

#include <OgreImage2.h>
#include <OgreTextureBox.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace gil = boost::gil;

namespace rml_tests {

	namespace {

		// Zero-copy view поверх Ogre::TextureBox с поддержкой RGBA8 и BGRA8
		template <typename Fn>
		void with_rgba8_view(const Ogre::Image2& img, Fn&& fn) {
			const Ogre::TextureBox box = img.getData(0u);
			const auto w = static_cast<std::ptrdiff_t>(box.width);
			const auto h = static_cast<std::ptrdiff_t>(box.height);
			const auto pitch = static_cast<std::ptrdiff_t>(box.bytesPerRow);

			switch (img.getPixelFormat()) {
			case Ogre::PFG_RGBA8_UNORM:
			case Ogre::PFG_RGBA8_UNORM_SRGB: {
				auto view = gil::interleaved_view(
					w, h, static_cast<const gil::rgba8_pixel_t*>(box.data), pitch);
				fn(view);
				return;
			}
			case Ogre::PFG_BGRA8_UNORM:
			case Ogre::PFG_BGRA8_UNORM_SRGB: {
				auto raw = gil::interleaved_view(
					w, h, static_cast<const gil::bgra8_pixel_t*>(box.data), pitch);
				fn(gil::color_converted_view<gil::rgba8_pixel_t>(raw));
				return;
			}
			default:
				throw std::runtime_error("Unsupported TextureGpu format for GIL comparison");
			}
		}

	} // namespace

	std::pair<std::uint32_t, std::uint32_t> read_png_dimensions(const std::filesystem::path& path) {
		const auto info = gil::read_image_info(path.string(), gil::png_tag());
		return { info._info._width, info._info._height };
	}

	ImageCompareResult compare_texture_with_png(
		Ogre::TextureGpu* texture,
		const std::filesystem::path& reference_path,
		const ImageCompareOptions& options,
		const std::string& test_name,
		const std::filesystem::path& output_dir)
	{
		gil::rgba8_image_t refImg;
		gil::read_and_convert_image(reference_path.string(), refImg, gil::png_tag());
		const auto refView = gil::const_view(refImg);

		Ogre::Image2 actualOgre;
		actualOgre.convertFromTexture(texture, 0u, 0u, true);

		ImageCompareResult res{};
		res.width = static_cast<std::uint32_t>(refView.width());
		res.height = static_cast<std::uint32_t>(refView.height());
		res.total_pixels = static_cast<std::size_t>(res.width) * res.height;

		if (actualOgre.getWidth() != res.width || actualOgre.getHeight() != res.height) {
			std::ostringstream oss;
			oss << "Resolution mismatch: actual=" << actualOgre.getWidth() << "x" << actualOgre.getHeight()
				<< ", reference=" << res.width << "x" << res.height;
			res.error_message = oss.str();
			return res;
		}

		gil::rgba8_image_t diffImg(refView.dimensions());
		auto diffView = gil::view(diffImg);
		const int channels = options.compare_alpha ? 4 : 3;
		double sumSqErr = 0.0;

		with_rgba8_view(actualOgre, [&](const auto& actView) {
			for (std::ptrdiff_t y = 0; y < refView.height(); ++y) {
				auto actRow = actView.row_begin(y);
				auto refRow = refView.row_begin(y);
				auto diffRow = diffView.row_begin(y);

				for (std::ptrdiff_t x = 0; x < refView.width(); ++x) {
					const gil::rgba8_pixel_t a = actRow[x];
					const gil::rgba8_pixel_t r = refRow[x];

					int maxDelta = 0;
					gil::rgba8_pixel_t d{ 0, 0, 0, 255 };

					for (int c = 0; c < channels; ++c) {
						const int delta = std::abs(static_cast<int>(a[c]) - static_cast<int>(r[c]));
						maxDelta = std::max(maxDelta, delta);
						sumSqErr += static_cast<double>(delta * delta);
						if (c < 3) {
							d[c] = static_cast<std::uint8_t>(std::min(255, delta * 4));
						}
					}

					res.max_channel_delta = std::max(res.max_channel_delta, maxDelta);
					if (maxDelta > static_cast<int>(options.channel_tolerance)) {
						++res.diff_pixels;
					}
					diffRow[x] = d;
				}
			}

			res.diff_ratio = res.total_pixels > 0
				? static_cast<double>(res.diff_pixels) / static_cast<double>(res.total_pixels)
				: 0.0;
			res.rmse = res.total_pixels > 0
				? std::sqrt(sumSqErr / static_cast<double>(res.total_pixels * channels))
				: 0.0;
			res.passed = (res.diff_ratio <= options.max_diff_ratio) && (res.rmse <= options.max_rmse);

			if (!res.passed) {
				std::filesystem::create_directories(output_dir);
				res.actual_dump_path = output_dir / (test_name + "_actual.png");
				res.diff_dump_path = output_dir / (test_name + "_diff.png");

				gil::write_view(res.actual_dump_path.string(), actView, gil::png_tag());
				gil::write_view(res.diff_dump_path.string(), diffView, gil::png_tag());
			}
			});

		if (!res.passed) {
			std::ostringstream oss;
			oss << "Image mismatch for '" << test_name << "': "
				<< "diff_pixels=" << res.diff_pixels << "/" << res.total_pixels
				<< " (" << (res.diff_ratio * 100.0) << "%), rmse=" << res.rmse
				<< ", max_delta=" << res.max_channel_delta
				<< ". Saved: " << res.actual_dump_path.string() << ", " << res.diff_dump_path.string();
			res.error_message = oss.str();
		}

		return res;
	}

} // namespace rml_tests