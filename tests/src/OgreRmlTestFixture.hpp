// tests/src/OgreRmlTestFixture.hpp
#pragma once

#include "ImageCompare.hpp"

#include <OgrePrerequisites.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#if defined(OGRE_STATIC_LIB) || defined(OGRE_STATIC)
#	define RML_TEST_STATIC_OGRE 1
#else
#	define RML_TEST_STATIC_OGRE 0
#endif
namespace nimble::RmlOgre {
	class RenderInterface;
}
namespace Ogre {
	class Root;
	class Window;
	class SceneManager;
	class CompositorPassProvider;
#if RML_TEST_STATIC_OGRE
	class VulkanPlugin;
#endif
}

namespace rml_tests {

	struct RenderTestSpec {
		std::string         name;
		std::string         rml_filename;
		std::string         reference_filename;
		ImageCompareOptions compare_options{};
		std::uint32_t       warmup_frames = 1u;
		std::uint32_t       worker_threads = 1u;
		bool                include_scene_pass = false;
	};

	class OgreRmlTestFixture {
	public:
		OgreRmlTestFixture();
		~OgreRmlTestFixture();

		OgreRmlTestFixture(const OgreRmlTestFixture&) = delete;
		OgreRmlTestFixture& operator=(const OgreRmlTestFixture&) = delete;

		static OgreRmlTestFixture& instance();

		ImageCompareResult run_render_test(const RenderTestSpec& spec);

		const std::filesystem::path& media_dir() const noexcept { return _media_dir; }
		const std::filesystem::path& output_dir() const noexcept { return _output_dir; }

	private:
		static OgreRmlTestFixture* _instance;

		std::filesystem::path _media_dir;
		std::filesystem::path _output_dir;

		std::unique_ptr<Ogre::Root> _root;
#if RML_TEST_STATIC_OGRE
		std::unique_ptr<Ogre::VulkanPlugin> _vulkan_plugin;
#endif
		std::unique_ptr<Ogre::CompositorPassProvider> _pass_provider;
		Ogre::Window* _window = nullptr;
		Ogre::SceneManager* _scene_mgr = nullptr;
		std::unique_ptr<nimble::RmlOgre::RenderInterface> _render_interface;
	};

} // namespace rml_tests