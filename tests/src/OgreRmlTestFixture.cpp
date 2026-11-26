// tests/src/OgreRmlTestFixture.cpp
#include "OgreRmlTestFixture.hpp"

#include <OgreAbiUtils.h>
#include <OgreArchiveManager.h>
#include <OgreCamera.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsUnlit.h>
#include <OgreRenderSystem.h>
#include <OgreResourceGroupManager.h>
#include <OgreRoot.h>
#include <OgreSceneManager.h>
#include <OgreTextureGpuManager.h>
#include <OgreWindow.h>

#include <Compositor/OgreCompositorManager2.h>
#include <Compositor/OgreCompositorNodeDef.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <Compositor/OgreCompositorWorkspaceDef.h>
#include <Compositor/Pass/OgreCompositorPassProvider.h>
#include <Compositor/Pass/PassClear/OgreCompositorPassClearDef.h>

#if RML_TEST_STATIC_OGRE
#	include <RenderSystems/Vulkan/OgreVulkanPlugin.h>
#endif

#include <RmlUi/Core.h>
#include <RmlOgre/Compositor/CompositorPassRml.hpp>
#include <RmlOgre/Compositor/CompositorPassRmlDef.hpp>
#include <RmlOgre/RenderInterface.hpp>

#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#endif
#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>

namespace rml_tests {

	namespace {

		std::optional<std::filesystem::path> find_vk_validation_layer_dir(
			const std::filesystem::path& fallbackBuildRoot = {})
		{
			namespace fs = std::filesystem;

			const std::array<fs::path, 4> relSubpaths = {
				fs::path{},
				fs::path("vcpkg_installed") / "x64-windows" / "bin",
				fs::path("vcpkg_installed") / "x64-windows" / "debug" / "bin",
				fs::path("vcpkg_installed") / "x64-windows" / "tools" / "vulkan-validationlayers"
			};

			auto checkRoot = [&](const fs::path& root) -> std::optional<fs::path> {
				if (root.empty())
					return std::nullopt;
				std::error_code ec;
				for (const auto& rel : relSubpaths) {
					const fs::path dir = rel.empty() ? root : (root / rel);
					if (fs::exists(dir / "VkLayer_khronos_validation.json", ec)) {
						return fs::weakly_canonical(dir, ec);
					}
				}
				return std::nullopt;
				};

#ifdef _WIN32
			// 1. Ищем относительно папки .exe и вверх до корня build-директории (out/build/x64-debug)
			char exeBuf[MAX_PATH]{};
			if (GetModuleFileNameA(nullptr, exeBuf, MAX_PATH) > 0) {
				fs::path probe = fs::path(exeBuf).parent_path();
				for (int depth = 0; depth < 4 && !probe.empty(); ++depth) {
					if (auto found = checkRoot(probe)) {
						return found;
					}
					probe = probe.parent_path();
				}
			}
#endif

			// 2. Запасной корень сборки (например, VisionLib/out/build/x64-debug)
			return checkRoot(fallbackBuildRoot);
		}

		void enable_vulkan_validation_env(const std::filesystem::path& fallbackBuildRoot = {})
		{
#ifdef _WIN32
			if (auto layerDir = find_vk_validation_layer_dir(fallbackBuildRoot)) {
				const std::string dirStr = layerDir->string();
				_putenv_s("VK_ADD_LAYER_PATH", dirStr.c_str());
				std::cout << "[Vulkan] VK_ADD_LAYER_PATH=" << dirStr << "\n";
			}
			else {
				std::cerr << "[Vulkan] VkLayer_khronos_validation.json not found\n";
			}

			// Принудительно активируем слой и дублируем error/warn в stdout
			// (работает даже без создания внешнего VkInstance и VkDebugUtilsMessengerEXT)
			_putenv_s("VK_INSTANCE_LAYERS", "VK_LAYER_KHRONOS_validation");
			_putenv_s("VK_KHRONOS_VALIDATION_DEBUG_ACTION", "VK_DBG_LAYER_ACTION_LOG_MSG");
			_putenv_s("VK_KHRONOS_VALIDATION_REPORT_FLAGS", "error,warn");
			_putenv_s("VK_KHRONOS_VALIDATION_LOG_FILENAME", "stdout");
#endif
		}

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
				Ogre::CompositorNodeDef* /*parentNodeDef*/) override
			{
				if (customId == "rml/render")
					return OGRE_NEW nimble::RmlOgre::CompositorPassRmlDef(parentTargetDef);
				return nullptr;
			}

			Ogre::CompositorPass* addPass(
				const Ogre::CompositorPassDef* definition,
				Ogre::Camera* defaultCamera,
				Ogre::CompositorNode* parentNode,
				const Ogre::RenderTargetViewDef* rtvDef,
				Ogre::SceneManager* /*sceneManager*/) override
			{
				if (auto* d = dynamic_cast<const nimble::RmlOgre::CompositorPassRmlDef*>(definition))
					return OGRE_NEW nimble::RmlOgre::CompositorPassRml(d, defaultCamera, rtvDef, parentNode);

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
			const auto withHlmsPrefix = hlmsRoot / ogreRelPath;
			if (std::filesystem::exists(withHlmsPrefix)) {
				return withHlmsPrefix;
			}

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
			// 1. Common-ресурсы Ogre (Quad.program, Copyback.material и GLSL-шейдеры)
			const auto commonDir = mediaDir / "common";
			if (!std::filesystem::exists(commonDir)) {
				throw std::runtime_error("Common resources directory not found: " + commonDir.string());
			}
			add_dir_if_exists(commonDir);
			add_dir_if_exists(commonDir / "Any");
			add_dir_if_exists(commonDir / "GLSL");

			// 2. Материалы и шейдеры RmlOgre (градиенты для test_10_shaders и др.)
			// Старую папку scripts/compositors/Rml не подключаем — воркспейс собирается программно
			const auto rmlMaterialsDir = mediaDir / "scripts" / "materials" / "Rml";
			add_dir_if_exists(rmlMaterialsDir);
			add_dir_if_exists(rmlMaterialsDir / "Any");
			add_dir_if_exists(rmlMaterialsDir / "GLSL");

			// 3. Текстуры RmlUi тестов (checkerboard.png, invader.tga, star.png)
			add_dir_if_exists(mediaDir / "rml");
		}

		/// RAII-обёртка над статическим 1-нодовым воркспейсом (clear + rml/render)
		struct ScopedRmlTestWorkspace {
			Ogre::CompositorManager2* compMgr = nullptr;
			Ogre::SceneManager* sceneMgr = nullptr;
			Ogre::Camera* camera = nullptr;
			Ogre::CompositorWorkspace* workspace = nullptr;
			Ogre::IdString wsDefName{};
			Ogre::IdString nodeDefName{};

			ScopedRmlTestWorkspace(
				Ogre::CompositorManager2* cm,
				Ogre::SceneManager* sm,
				Ogre::TextureGpu* rtTexture,
				const nimble::RmlOgre::RmlFrameData* frameData,
				const std::string& prefix)
				: compMgr(cm)
				, sceneMgr(sm)
			{
				camera = sceneMgr->createCamera(prefix + "_Cam", false);

				const std::string nodeStr = prefix + "_Node";
				const std::string wsStr = prefix + "_Ws";
				nodeDefName = Ogre::IdString(nodeStr);
				wsDefName = Ogre::IdString(wsStr);

				if (compMgr->hasNodeDefinition(nodeDefName))
					compMgr->removeNodeDefinition(nodeDefName);
				if (compMgr->hasWorkspaceDefinition(wsDefName))
					compMgr->removeWorkspaceDefinition(wsDefName);

				auto* nodeDef = compMgr->addNodeDefinition(nodeStr);
				nodeDef->addTextureSourceName("rt_out", 0, Ogre::TextureDefinitionBase::TEXTURE_INPUT);

				auto* depthTex = nodeDef->addTextureDefinition("rtd");
				depthTex->textureType = Ogre::TextureTypes::Type2D;
				depthTex->width = 0;
				depthTex->height = 0;
				depthTex->format = Ogre::PFG_D32_FLOAT_S8X24_UINT;

				auto* rtv = nodeDef->addRenderTextureView("rtv_rml");
				rtv->colourAttachments.emplace_back();
				rtv->colourAttachments.back().textureName = "rt_out";
				rtv->depthAttachment.textureName = "rtd";

				nodeDef->setNumTargetPass(1);
				auto* targetPass = nodeDef->addTargetPass("rtv_rml");
				targetPass->setNumPasses(2);

				auto* clearPass = static_cast<Ogre::CompositorPassClearDef*>(
					targetPass->addPass(Ogre::PASS_CLEAR));
				clearPass->mClearColour[0] = Ogre::ColourValue(0.0f, 0.0f, 0.0f, 1.0f);
				clearPass->mClearStencil = 0u;

				auto* rmlPass = static_cast<nimble::RmlOgre::CompositorPassRmlDef*>(
					targetPass->addPass(Ogre::PASS_CUSTOM, Ogre::IdString("rml/render")));
				rmlPass->mFrameData = frameData;

				auto* wsDef = compMgr->addWorkspaceDefinition(wsStr);
				wsDef->connectExternal(0, nodeDefName, 0);

				workspace = compMgr->addWorkspace(sceneMgr, rtTexture, camera, wsDefName, true);
			}

			~ScopedRmlTestWorkspace() {
				if (compMgr) {
					if (workspace)
						compMgr->removeWorkspace(workspace);
					if (wsDefName != Ogre::IdString() && compMgr->hasWorkspaceDefinition(wsDefName))
						compMgr->removeWorkspaceDefinition(wsDefName);
					if (nodeDefName != Ogre::IdString() && compMgr->hasNodeDefinition(nodeDefName))
						compMgr->removeNodeDefinition(nodeDefName);
				}
				if (sceneMgr && camera) {
					sceneMgr->destroyCamera(camera);
				}
			}
		};

	} // namespace

	OgreRmlTestFixture* OgreRmlTestFixture::_instance = nullptr;

	OgreRmlTestFixture::OgreRmlTestFixture()
		: _media_dir(std::filesystem::absolute(RMLOGRE_MEDIA_DIR))
		, _output_dir(std::filesystem::absolute(RMLOGRE_TEST_OUTPUT_DIR))
	{
		const Ogre::AbiCookie abiCookie = Ogre::generateAbiCookie();
		enable_vulkan_validation_env(
			std::filesystem::current_path());
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
		const auto& opts = rs->getConfigOptions();
		if (opts.find("Debug Layer") != opts.end()) {
			rs->setConfigOption("Debug Layer", "Yes");
		}
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
		_render_interface = std::make_unique<nimble::RmlOgre::RenderInterface>("RmlTestUI");

		_instance = this;
	}

	OgreRmlTestFixture::~OgreRmlTestFixture() {
		_instance = nullptr;

		Rml::Shutdown();

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
		_render_interface->SetViewportSize(Rml::Vector2i{
			static_cast<int>(width),
			static_cast<int>(height)
			});

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

		Rml::Context* context = Rml::CreateContext(
			"Ctx_" + spec.name,
			Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)),
			_render_interface.get());
		if (!context) {
			texMgr->destroyTexture(rtTexture);
			throw std::runtime_error("Failed to create Rml::Context for " + spec.name);
		}

		ImageCompareResult result{};
		try {
			ScopedRmlTestWorkspace testWs(
				_root->getCompositorManager2(),
				_scene_mgr,
				rtTexture,
				&_render_interface->frameData(),
				"RmlTest_" + spec.name);

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
			texMgr->destroyTexture(rtTexture);
			throw;
		}

		Rml::RemoveContext(context->GetName());
		texMgr->destroyTexture(rtTexture);
		return result;
	}

} // namespace rml_tests