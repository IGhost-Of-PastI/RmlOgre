// tests/src/OgreRmlTestFixture.cpp
#include "OgreRmlTestFixture.hpp"

#include <OgreAbiUtils.h>
#include <OgreArchiveManager.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsUnlit.h>
#include <OgreRenderSystem.h>
#include <OgreResourceGroupManager.h>
#include <OgreRoot.h>
#include <OgreSceneManager.h>
#include <OgreTextureGpuManager.h>
#include <OgreWindow.h>

#include <Compositor/OgreCompositorManager2.h>
#include <Compositor/Pass/OgreCompositorPassProvider.h>

#if RML_TEST_STATIC_OGRE
#	include <RenderSystems/Vulkan/OgreVulkanPlugin.h>
#endif

#include <RmlUi/Core.h>
#include <RmlOgre/Compositor/CompositorPassGeometry.hpp>
#include <RmlOgre/Compositor/CompositorPassGeometryDef.hpp>
#include <RmlOgre/Compositor/CompositorPassRenderQuad.hpp>
#include <RmlOgre/Compositor/CompositorPassRenderQuadDef.hpp>
#include <RmlOgre/RenderInterface.hpp>

#include <stdexcept>
#include <string_view>

namespace rml_tests {

	namespace {
		struct ScopedCurrentPath {
			std::filesystem::path prev;

			explicit ScopedCurrentPath(const std::filesystem::path& target)
				: prev(std::filesystem::current_path())
			{
				std::filesystem::current_path(target);
			}

			~ScopedCurrentPath() {
				std::error_code ec;
				std::filesystem::current_path(prev, ec);
			}
		};
		struct TestCompositorPassProvider final : public Ogre::CompositorPassProvider {
			Ogre::CompositorPassDef* addPassDef(
				Ogre::CompositorPassType /*passType*/,
				Ogre::IdString customId,
				Ogre::CompositorTargetDef* parentTargetDef,
				Ogre::CompositorNodeDef* parentNodeDef) override
			{
				if (customId == "rml/geometry")
					return OGRE_NEW nimble::RmlOgre::CompositorPassGeometryDef(parentTargetDef);
				if (customId == "rml/render_quad")
					return OGRE_NEW nimble::RmlOgre::CompositorPassRenderQuadDef(parentNodeDef, parentTargetDef);
				return nullptr;
			}

			Ogre::CompositorPass* addPass(
				const Ogre::CompositorPassDef* definition,
				Ogre::Camera* defaultCamera,
				Ogre::CompositorNode* parentNode,
				const Ogre::RenderTargetViewDef* rtvDef,
				Ogre::SceneManager* /*sceneManager*/) override
			{
				if (auto* d = dynamic_cast<const nimble::RmlOgre::CompositorPassGeometryDef*>(definition))
					return OGRE_NEW nimble::RmlOgre::CompositorPassGeometry(d, defaultCamera, rtvDef, parentNode);
				if (auto* d = dynamic_cast<const nimble::RmlOgre::CompositorPassRenderQuadDef*>(definition))
					return OGRE_NEW nimble::RmlOgre::CompositorPassRenderQuad(d, defaultCamera, parentNode, rtvDef);

				OGRE_EXCEPT(
					Ogre::Exception::ERR_NOT_IMPLEMENTED,
					"Unknown RmlOgre compositor pass",
					"TestCompositorPassProvider::addPass");
			}
		};

		std::filesystem::path resolve_hlms_subpath(
			const std::filesystem::path& hlmsRoot,
			const Ogre::String& ogreRelPath)
		{
			// Вариант 1: media/hlms/Hlms/Unlit/Any
			const auto withHlmsPrefix = hlmsRoot / ogreRelPath;
			if (std::filesystem::exists(withHlmsPrefix)) {
				return withHlmsPrefix;
			}

			// Вариант 2: media/hlms/Unlit/Any (папка Hlms скопирована как media/hlms)
			constexpr std::string_view prefix = "Hlms/";
			if (ogreRelPath.starts_with(prefix)) {
				const auto withoutPrefix = hlmsRoot / ogreRelPath.substr(prefix.size());
				if (std::filesystem::exists(withoutPrefix)) {
					return withoutPrefix;
				}
			}

			throw std::runtime_error(
				"HLMS directory not found under '" + hlmsRoot.string() + "' for path '" + ogreRelPath + "'");
		}

		void register_hlms(const std::filesystem::path& mediaDir) {
			using namespace Ogre;

			const auto hlmsRoot = mediaDir / "hlms";
			if (!std::filesystem::exists(hlmsRoot)) {
				throw std::runtime_error("HLMS root directory not found: " + hlmsRoot.string());
			}

			String mainPath;
			StringVector libraryPaths;
			HlmsUnlit::getDefaultPaths(mainPath, libraryPaths);

			auto& archiveManager = ArchiveManager::getSingleton();
			auto* hlmsManager = Root::getSingleton().getHlmsManager();

			const auto resolvedMain = resolve_hlms_subpath(hlmsRoot, mainPath).generic_string();
			Archive* mainArchive = archiveManager.load(resolvedMain, "FileSystem", true);

			ArchiveVec libraryFolders;
			libraryFolders.reserve(libraryPaths.size());
			for (const auto& libPath : libraryPaths) {
				const auto resolvedLib = resolve_hlms_subpath(hlmsRoot, libPath).generic_string();
				libraryFolders.push_back(archiveManager.load(resolvedLib, "FileSystem", true));
			}

			auto* hlmsUnlit = OGRE_NEW HlmsUnlit(mainArchive, &libraryFolders);
			hlmsManager->registerHlms(hlmsUnlit);
			hlmsManager->useDefaultDatablockFrom(HlmsTypes::HLMS_UNLIT);
		}

		void add_dir_if_exists(
			const std::filesystem::path& dir,
			const Ogre::String& group = Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME)
		{
			if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
				return;
			}

			auto& rgm = Ogre::ResourceGroupManager::getSingleton();
			const Ogre::String pathStr = dir.generic_string();
			if (!rgm.resourceLocationExists(pathStr, group)) {
				rgm.addResourceLocation(pathStr, "FileSystem", group);
			}
		}

		void setup_resources(const std::filesystem::path& mediaDir) {
			// 1. Common-ресурсы Ogre (Copyback.material, Quad.program и их GLSL-шейдеры для Vulkan)
			const auto commonDir = mediaDir / "common";
			if (!std::filesystem::exists(commonDir)) {
				throw std::runtime_error("Common resources directory not found: " + commonDir.string());
			}
			add_dir_if_exists(commonDir);
			add_dir_if_exists(commonDir / "Any");
			add_dir_if_exists(commonDir / "GLSL");
			//add_dir_if_exists(commonDir / "GLSLVK");
			//add_dir_if_exists(commonDir / "Vulkan");

			// 2. Скрипты композитора, материалы и шейдеры RmlOgre
			const auto rmlCompositorsDir = mediaDir / "scripts" / "compositors" / "Rml";
			const auto rmlMaterialsDir = mediaDir / "scripts" / "materials" / "Rml";
			if (!std::filesystem::exists(rmlCompositorsDir)) {
				throw std::runtime_error("Rml compositor directory not found: " + rmlCompositorsDir.string());
			}

			add_dir_if_exists(rmlCompositorsDir);
			add_dir_if_exists(rmlMaterialsDir);
			add_dir_if_exists(rmlMaterialsDir / "Any");
			add_dir_if_exists(rmlMaterialsDir / "GLSL");
			//add_dir_if_exists(rmlMaterialsDir / "GLSLVK");

			// 3. Текстуры RmlUi тестов (checkerboard.png, invader.tga, star.png)
			add_dir_if_exists(mediaDir / "rml");
		}

	} // namespace

	OgreRmlTestFixture* OgreRmlTestFixture::_instance = nullptr;

	OgreRmlTestFixture::OgreRmlTestFixture()
		: _media_dir(std::filesystem::absolute(RMLOGRE_MEDIA_DIR))
		, _output_dir(std::filesystem::absolute(RMLOGRE_TEST_OUTPUT_DIR))
	{
		const Ogre::AbiCookie abiCookie = Ogre::generateAbiCookie();

		// Без plugins.cfg и ogre.cfg
		_root.reset(OGRE_NEW Ogre::Root(&abiCookie, "", "", "./RmlOgreRenderTests.log"));

#if RML_TEST_STATIC_OGRE
		_vulkan_plugin = std::make_unique<Ogre::VulkanPlugin>();
		_root->installPlugin(_vulkan_plugin.get(), nullptr);
#elif OGRE_DEBUG_MODE
		_root->loadPlugin("RenderSystem_Vulkan_d");
#else
		_root->loadPlugin("RenderSystem_Vulkan");
#endif

		Ogre::RenderSystem* rs = _root->getRenderSystemByName("Vulkan Rendering Subsystem");
		if (!rs) {
			throw std::runtime_error("Vulkan Rendering Subsystem not available");
		}
		_root->setRenderSystem(rs);

		rs->setConfigOption("sRGB Gamma Conversion", "No");
		_root->initialise(false);

		Ogre::NameValuePairList miscParams;
		miscParams["hidden"] = "true";
		miscParams["gamma"] = "false";
		_window = _root->createRenderWindow("RmlOgreRenderTests_Hidden", 128u, 128u, false, &miscParams);

		rs->setConfigOption("sRGB Gamma Conversion", "Yes");

		_pass_provider = std::make_unique<TestCompositorPassProvider>();
		_root->getCompositorManager2()->setCompositorPassProvider(_pass_provider.get());

		register_hlms(_media_dir);
		setup_resources(_media_dir);
		Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups(true);

		Rml::Initialise();
		const auto fontsDir = _media_dir / "fonts";
		Rml::LoadFontFace((fontsDir / "LatoLatin-Bold.ttf").string());
		Rml::LoadFontFace((fontsDir / "LatoLatin-BoldItalic.ttf").string());
		Rml::LoadFontFace((fontsDir / "LatoLatin-Italic.ttf").string());
		Rml::LoadFontFace((fontsDir / "LatoLatin-Regular.ttf").string());

		_scene_mgr = _root->createSceneManager(Ogre::ST_GENERIC, 1u, "RmlTestSceneManager");
		_render_interface = std::make_unique<nimble::RmlOgre::RenderInterface>(
			"RmlTestUI", _scene_mgr, nullptr, nullptr);

		_instance = this;
	}

	OgreRmlTestFixture::~OgreRmlTestFixture() {
		_instance = nullptr;

		if (_render_interface) {
			_render_interface->SetOutput(nullptr);
		}

		// Сначала освобождаем все RenderManager'ы и ставим VAO/текстуры в очередь на удаление
		Rml::Shutdown();

		// Сбрасываем releaseGeometries / releaseTextures пока Ogre::Root ещё жив,
		// после чего уничтожаем RenderInterface (ассерт HasRenderManager уже не сработает)
		if (_render_interface) {
			_render_interface->BeginFrame();
			_render_interface.reset();
		}

		if (_root) {
			if (_scene_mgr) {
				_root->destroySceneManager(_scene_mgr);
				_scene_mgr = nullptr;
			}
			_root->getCompositorManager2()->setCompositorPassProvider(nullptr);
			_root.reset();
		}
	}

	OgreRmlTestFixture& OgreRmlTestFixture::instance() {
		if (!_instance) {
			throw std::runtime_error("OgreRmlTestFixture is not initialized");
		}
		return *_instance;
	}

	ImageCompareResult OgreRmlTestFixture::run_render_test(const RenderTestSpec& spec) {
		const auto rmlDir = _media_dir / "rml";
		const auto refPath = rmlDir / "reference" / spec.reference_filename;

		const auto [width, height] = read_png_dimensions(refPath);

		auto* rs = _root->getRenderSystem();
		auto* texMgr = rs->getTextureGpuManager();

		Ogre::TextureGpu* rtTexture = texMgr->createTexture(
			"RmlTestRT_" + spec.name,
			Ogre::GpuPageOutStrategy::Discard,
			Ogre::TextureFlags::RenderToTexture,
			Ogre::TextureTypes::Type2D);
		rtTexture->setResolution(width, height);
		rtTexture->setPixelFormat(Ogre::PFG_RGBA8_UNORM);
		rtTexture->setNumMipmaps(1u);
		rtTexture->scheduleTransitionTo(Ogre::GpuResidency::Resident);
		texMgr->waitForStreamingCompletion();

		_render_interface->SetOutput(rtTexture);

		Rml::Context* context = Rml::CreateContext(
			"Ctx_" + spec.name,
			Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)),
			_render_interface.get());
		if (!context) {
			_render_interface->SetOutput(nullptr);
			texMgr->destroyTexture(rtTexture);
			throw std::runtime_error("Failed to create Rml::Context for " + spec.name);
		}

		ImageCompareResult result{};
		try {
			ScopedCurrentPath cwdGuard(rmlDir);

			Rml::ElementDocument* doc = context->LoadDocument(spec.rml_filename);
			if (!doc) {
				throw std::runtime_error("Failed to load RML document: " + (rmlDir / spec.rml_filename).string());
			}
			doc->Show();

			const std::uint32_t totalFrames = std::max<std::uint32_t>(1u, spec.warmup_frames) + 1u;
			for (std::uint32_t frame = 0; frame < totalFrames; ++frame) {
				context->Update();
				_render_interface->BeginFrame();
				context->Render();
				_render_interface->EndFrame();

				texMgr->waitForStreamingCompletion();
				_root->renderOneFrame();
			}

			result = compare_texture_with_png(rtTexture, refPath, spec.compare_options, spec.name, _output_dir);

			doc->Close();
		}
		catch (...) {
			Rml::RemoveContext(context->GetName());
			_render_interface->SetOutput(nullptr);
			texMgr->destroyTexture(rtTexture);
			throw;
		}

		Rml::RemoveContext(context->GetName());
		_render_interface->SetOutput(nullptr);
		texMgr->destroyTexture(rtTexture);
		return result;
	}

} // namespace rml_tests