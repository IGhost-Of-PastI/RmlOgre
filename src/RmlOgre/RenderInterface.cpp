// RenderInterface.cpp
#include "RenderInterface.hpp"

#include "geometry.hpp"
#include "shaders.hpp"

#include <Hlms/Unlit/OgreHlmsUnlit.h>
#include <Hlms/Unlit/OgreHlmsUnlitDatablock.h>
#include <OgreHlmsManager.h>
#include <OgreImage2.h>
#include <OgrePixelFormatGpuUtils.h>
#include <OgreRenderSystem.h>
#include <OgreRoot.h>
#include <OgreTextureFilters.h>
#include <OgreTextureGpu.h>
#include <OgreTextureGpuManager.h>
#include <Vao/OgreVaoManager.h>
#include <Vao/OgreVertexArrayObject.h>

#include <algorithm>
#include <cassert>

using namespace nimble::RmlOgre;

RenderInterface::RenderInterface(Ogre::String name)
	: mName(std::move(name))
	, mHlms(static_cast<Ogre::HlmsUnlit*>(
		Ogre::Root::getSingleton().getHlmsManager()->getHlms(Ogre::HLMS_UNLIT)))
{
	mMacroblock.mScissorTestEnabled = true;
	mMacroblock.mDepthCheck = false;
	mMacroblock.mDepthWrite = false;
	mMacroblock.mCullMode = Ogre::CULL_NONE;

	mBlendblock.mSeparateBlend = false;
	mBlendblock.mBlendOperation = Ogre::SceneBlendOperation::SBO_ADD;
	mBlendblock.mDestBlendFactor = Ogre::SceneBlendFactor::SBF_ONE_MINUS_SOURCE_ALPHA;
	mBlendblock.mSourceBlendFactor = Ogre::SceneBlendFactor::SBF_ONE;

	mSamplerblock.mU = Ogre::TextureAddressingMode::TAM_WRAP;
	mSamplerblock.mV = Ogre::TextureAddressingMode::TAM_WRAP;

	// Индекс 0 в mMaterials — материал без текстуры
	auto* noTextureDatablock = static_cast<Ogre::HlmsUnlitDatablock*>(
		mHlms->getDatablock("NoTexture"));
	if (!noTextureDatablock)
	{
		noTextureDatablock = static_cast<Ogre::HlmsUnlitDatablock*>(
			mHlms->createDatablock(
				"NoTexture",
				"NoTexture",
				mMacroblock,
				mBlendblock,
				Ogre::HlmsParamVec()));
	}
	noTextureDatablock->setUseColour(true);
	Material noTextureMaterial{ nullptr, noTextureDatablock };
	noTextureMaterial.calculateHlmsHash();
	mMaterials.insert(std::move(noTextureMaterial));

	// Материал только для записи в stencil (запись в RGBA отключена)
	Ogre::HlmsBlendblock stencilBlendblock = mBlendblock;
	stencilBlendblock.mBlendChannelMask = 0u;

	const Ogre::String stencilDbName = "Rml_StencilOnly";
	auto* stencilDatablock = static_cast<Ogre::HlmsUnlitDatablock*>(
		mHlms->getDatablock(stencilDbName));
	if (!stencilDatablock)
	{
		stencilDatablock = static_cast<Ogre::HlmsUnlitDatablock*>(
			mHlms->createDatablock(
				stencilDbName,
				stencilDbName,
				mMacroblock,
				stencilBlendblock,
				Ogre::HlmsParamVec()));
	}
	mStencilMaterial = Material{ nullptr, stencilDatablock };
	mStencilMaterial.calculateHlmsHash();

	// Фильтры (индекс 0 зарезервирован под пустой handle)
	AddFilterMaker("blur", std::make_unique<BlurFilterMaker>());
	AddFilterMaker("drop-shadow", std::make_unique<DropShadowFilterMaker>());
	AddFilterMaker("opacity", std::make_unique<OpacityFilterMaker>());
	AddFilterMaker("brightness", std::make_unique<BrightnessFilterMaker>());
	AddFilterMaker("contrast", std::make_unique<ContrastFilterMaker>());
	AddFilterMaker("invert", std::make_unique<InvertFilterMaker>());
	AddFilterMaker("grayscale", std::make_unique<GrayscaleFilterMaker>());
	AddFilterMaker("sepia", std::make_unique<SepiaFilterMaker>());
	AddFilterMaker("hue-rotate", std::make_unique<HueRotateFilterMaker>());
	AddFilterMaker("saturate", std::make_unique<SaturateFilterMaker>());
	mFilters.insert({});

	// Градиентные шейдеры (индекс 0 зарезервирован под пустой handle)
	AddShaderMaker("linear-gradient", std::make_unique<LinearGradientMaker>());
	AddShaderMaker("radial-gradient", std::make_unique<RadialGradientMaker>());
	AddShaderMaker("conic-gradient", std::make_unique<ConicGradientMaker>());
	mShaders.insert({});
}

RenderInterface::RenderInterface(
	const Ogre::String& name,
	Ogre::SceneManager* /*sceneManager*/,
	Ogre::TextureGpu* output,
	Ogre::TextureGpu* /*background*/)
	: RenderInterface(name)
{
	if (output)
	{
		mViewportSize = Rml::Vector2i{
			static_cast<int>(output->getWidth()),
			static_cast<int>(output->getHeight())
		};
	}
}

void RenderInterface::releaseBufferedGeometries()
{
	Ogre::VaoManager* vaoManager = Ogre::Root::getSingleton()
		.getRenderSystem()
		->getVaoManager();

	for (Rml::CompiledGeometryHandle geometry : mReleaseGeometries)
	{
		auto* vao = reinterpret_cast<Ogre::VertexArrayObject*>(geometry);
		for (auto* buffer : vao->getVertexBuffers())
			vaoManager->destroyVertexBuffer(buffer);

		if (vao->getIndexBuffer())
			vaoManager->destroyIndexBuffer(vao->getIndexBuffer());
		vaoManager->destroyVertexArrayObject(vao);
	}
	mReleaseGeometries.clear();
}

void RenderInterface::releaseBufferedTextures()
{
	Ogre::TextureGpuManager& textureManager = *Ogre::Root::getSingleton()
		.getRenderSystem()
		->getTextureGpuManager();

	for (Rml::TextureHandle texture : mReleaseTextures)
	{
		if (!mMaterials.has(texture))
			continue;

		auto& material = mMaterials.at(texture);
		auto* datablock = static_cast<Ogre::HlmsUnlitDatablock*>(material.datablock);
		auto* textureGpu = datablock->getTexture(0);

		assert(datablock->getLinkedRenderables().empty());

		mHlms->destroyDatablock(datablock->getName());
		if (textureGpu)
			textureManager.destroyTexture(textureGpu);

		mMaterials.erase(texture);
	}
	mReleaseTextures.clear();

	for (Ogre::TextureGpu* texture : mReleaseRenderTextures)
	{
		if (texture)
			textureManager.destroyTexture(texture);
	}
	mReleaseRenderTextures.clear();
}

Ogre::TextureGpu* RenderInterface::createSaveLayerTexture(Rml::Vector2i& outDimensions)
{
	outDimensions = mCurrentScissor.enabled
		? mCurrentScissor.region.Size()
		: mViewportSize;
	outDimensions.x = std::max(1, outDimensions.x);
	outDimensions.y = std::max(1, outDimensions.y);

	Ogre::String texName = mName;
	texName.append("_SavedLayer_");
	texName.append(std::to_string(mRenderTextureId++));

	auto* textureManager = Ogre::Root::getSingleton()
		.getRenderSystem()
		->getTextureGpuManager();
	Ogre::TextureGpu* texture = textureManager->createTexture(
		texName,
		Ogre::GpuPageOutStrategy::Discard,
		Ogre::TextureFlags::RenderToTexture,
		Ogre::TextureTypes::Type2D);
	texture->setPixelFormat(Ogre::PixelFormatGpu::PFG_RGBA8_UNORM);
	texture->setResolution(
		static_cast<Ogre::uint32>(outDimensions.x),
		static_cast<Ogre::uint32>(outDimensions.y));
	texture->setNumMipmaps(1u);
	texture->scheduleTransitionTo(Ogre::GpuResidency::Resident);
	return texture;
}

void RenderInterface::AddFilterMaker(Rml::String name, std::unique_ptr<FilterMaker> filterMaker)
{
	mFilterMakers.emplace(std::move(name), std::move(filterMaker));
}

void RenderInterface::AddShaderMaker(Rml::String name, std::unique_ptr<ShaderMaker> shaderMaker)
{
	mShaderMakers.emplace(std::move(name), std::move(shaderMaker));
}

void RenderInterface::FlushReleases()
{
	releaseBufferedGeometries();
	releaseBufferedTextures();
}

void RenderInterface::BeginFrame(RmlFrameData& targetFrame, Rml::Vector2i viewportSize)
{
	mViewportSize = viewportSize;
	mActiveFrameData = &targetFrame;
	mActiveFrameData->clear();
	mActiveFrameData->stencilMaterial = mStencilMaterial;

	mCurrentScissor = {};
	mCurrentTransform = Ogre::Matrix4::IDENTITY;
	mStencilEnabled = false;
	mStencilRef = 0;
	mLayerDepth = 1;
}

void RenderInterface::BeginFrame()
{
	mDefaultFrameData.clear();
	FlushReleases();
	BeginFrame(mDefaultFrameData, mViewportSize);
}

void RenderInterface::EndFrame()
{
	mActiveFrameData = nullptr;
}

Rml::CompiledGeometryHandle RenderInterface::CompileGeometry(
	Rml::Span<const Rml::Vertex> vertices,
	Rml::Span<const int> indices)
{
	return reinterpret_cast<Rml::CompiledGeometryHandle>(create_vao(vertices, indices));
}

void RenderInterface::RenderGeometry(
	Rml::CompiledGeometryHandle geometry,
	Rml::Vector2f translation,
	Rml::TextureHandle texture)
{
	assert(mActiveFrameData);

	auto& material = mMaterials.at(texture);
	if (material.needsHashing())
		material.calculateHlmsHash();

	mActiveFrameData->commands.push_back(DrawCmd{
		.vao = reinterpret_cast<Ogre::VertexArrayObject*>(geometry),
		.material = material,
		.transform = mCurrentTransform,
		.translation = translation,
		.scissor = mCurrentScissor,
		.stencilMode = mStencilEnabled ? StencilMode::TestEqual : StencilMode::Disabled,
		.stencilRef = mStencilRef
		});

	if (material.textureDependency)
		mActiveFrameData->textureDependencies.push_back(material.textureDependency);
}

void RenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	mReleaseGeometries.push_back(geometry);
}

Rml::TextureHandle RenderInterface::LoadTexture(
	Rml::Vector2i& texture_dimensions,
	const Rml::String& source)
{
	Ogre::TextureGpuManager* textureManager = Ogre::Root::getSingleton()
		.getRenderSystem()
		->getTextureGpuManager();
	Ogre::TextureGpu* texture = nullptr;
	if (!source.empty())
	{
		texture = textureManager->createTexture(
			source,
			Ogre::GpuPageOutStrategy::Discard,
			Ogre::TextureFlags::AutomaticBatching,
			Ogre::TextureTypes::Type2D,
			Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME,
			Ogre::TextureFilter::TypePremultiplyAlpha);
		texture->scheduleTransitionTo(Ogre::GpuResidency::Resident);
	}

	if (!texture)
		return Rml::TextureHandle{};

	texture->waitForMetadata();
	texture_dimensions = Rml::Vector2i(texture->getWidth(), texture->getHeight());

	Ogre::String id = mName;
	id.append("_Texture_");
	id.append(std::to_string(mDatablockId++));
	auto* datablock = static_cast<Ogre::HlmsUnlitDatablock*>(
		mHlms->createDatablock(id, id, mMacroblock, mBlendblock, Ogre::HlmsParamVec()));
	datablock->setTexture(0, texture, &mSamplerblock);
	datablock->setUseColour(true);

	Material material{ nullptr, datablock };
	material.calculateHlmsHash();
	return mMaterials.insert(std::move(material));
}

Rml::TextureHandle RenderInterface::GenerateTexture(
	Rml::Span<const Rml::byte> source,
	Rml::Vector2i source_dimensions)
{
	Ogre::String id = mName;
	id.append("_Texture_");
	id.append(std::to_string(mDatablockId++));

	const std::size_t size = Ogre::PixelFormatGpuUtils::calculateSizeBytes(
		source_dimensions.x,
		source_dimensions.y,
		1u,
		1u,
		Ogre::PixelFormatGpu::PFG_RGBA8_UNORM_SRGB,
		1u,
		4u);
	auto* data = reinterpret_cast<Ogre::uint8*>(
		OGRE_MALLOC_SIMD(size, Ogre::MEMCATEGORY_GENERAL));
	std::copy(source.begin(), source.end(), data);

	auto* image = OGRE_NEW Ogre::Image2;
	image->loadDynamicImage(
		data,
		source_dimensions.x,
		source_dimensions.y,
		1u,
		Ogre::TextureTypes::Type2D,
		Ogre::PixelFormatGpu::PFG_RGBA8_UNORM,
		true,
		1u);

	Ogre::TextureGpuManager& textureManager = *Ogre::Root::getSingleton()
		.getRenderSystem()
		->getTextureGpuManager();
	Ogre::TextureGpu* texture = textureManager.createTexture(
		id,
		Ogre::GpuPageOutStrategy::AlwaysKeepSystemRamCopy,
		Ogre::TextureFlags::AutomaticBatching,
		Ogre::TextureTypes::Type2D,
		Ogre::BLANKSTRING);
	texture->setNumMipmaps(1);
	texture->setResolution(source_dimensions.x, source_dimensions.y);
	texture->setPixelFormat(Ogre::PixelFormatGpu::PFG_RGBA8_UNORM);
	texture->scheduleTransitionTo(Ogre::GpuResidency::Resident, image);

	auto* datablock = static_cast<Ogre::HlmsUnlitDatablock*>(
		mHlms->createDatablock(id, id, mMacroblock, mBlendblock, Ogre::HlmsParamVec()));
	datablock->setTexture(0, texture, &mSamplerblock);
	datablock->setUseColour(true);

	Material material{ nullptr, datablock };
	material.calculateHlmsHash();
	return mMaterials.insert(std::move(material));
}

void RenderInterface::ReleaseTexture(Rml::TextureHandle texture)
{
	mReleaseTextures.push_back(texture);
}

void RenderInterface::EnableScissorRegion(bool enable)
{
	mCurrentScissor.enabled = enable;
}

void RenderInterface::SetScissorRegion(Rml::Rectanglei region)
{
	mCurrentScissor.region = region;
}

void RenderInterface::SetTransform(const Rml::Matrix4f* transform)
{
	mCurrentTransform = transform
		? Ogre::Matrix4(transform->data()).transpose()
		: Ogre::Matrix4::IDENTITY;
}

void RenderInterface::EnableClipMask(bool enable)
{
	mStencilEnabled = enable;
}

void RenderInterface::RenderToClipMask(
	Rml::ClipMaskOperation operation,
	Rml::CompiledGeometryHandle geometry,
	Rml::Vector2f translation)
{
	assert(mActiveFrameData);

	auto* vao = reinterpret_cast<Ogre::VertexArrayObject*>(geometry);

	switch (operation)
	{
	case Rml::ClipMaskOperation::Set:
		mStencilRef = 1u;
		mActiveFrameData->commands.push_back(ClearStencilCmd{ 0u });
		mActiveFrameData->commands.push_back(DrawCmd{
			.vao = vao,
			.material = mStencilMaterial,
			.transform = mCurrentTransform,
			.translation = translation,
			.scissor = mCurrentScissor,
			.stencilMode = StencilMode::WriteSet,
			.stencilRef = 1u
			});
		break;

	case Rml::ClipMaskOperation::SetInverse:
		mActiveFrameData->commands.push_back(ClearStencilCmd{ 1u });
		mActiveFrameData->commands.push_back(DrawCmd{
			.vao = vao,
			.material = mStencilMaterial,
			.transform = mCurrentTransform,
			.translation = translation,
			.scissor = mCurrentScissor,
			.stencilMode = StencilMode::WriteSetInverse,
			.stencilRef = 0u
			});
		mStencilRef = 1u;
		break;

	case Rml::ClipMaskOperation::Intersect:
		++mStencilRef;
		mActiveFrameData->commands.push_back(DrawCmd{
			.vao = vao,
			.material = mStencilMaterial,
			.transform = mCurrentTransform,
			.translation = translation,
			.scissor = mCurrentScissor,
			.stencilMode = StencilMode::WriteIntersect,
			.stencilRef = mStencilRef
			});
		break;
	}
}

Rml::LayerHandle RenderInterface::PushLayer()
{
	assert(mActiveFrameData);

	const std::uint32_t newLayerId = mLayerDepth++;
	mActiveFrameData->commands.push_back(PushLayerCmd{ newLayerId });
	return static_cast<Rml::LayerHandle>(newLayerId);
}

void RenderInterface::CompositeLayers(
	Rml::LayerHandle source,
	Rml::LayerHandle destination,
	Rml::BlendMode blend_mode,
	Rml::Span<const Rml::CompiledFilterHandle> filters)
{
	assert(mActiveFrameData);

	CompositeLayersCmd cmd{};
	cmd.sourceLayer = static_cast<std::uint32_t>(source);
	cmd.destinationLayer = static_cast<std::uint32_t>(destination);
	cmd.restoreLayer = activeLayer();
	cmd.blendMode = blend_mode;
	cmd.scissor = mCurrentScissor;
	cmd.stencilEnabled = mStencilEnabled;
	cmd.stencilRef = mStencilRef;

	for (Rml::CompiledFilterHandle filterHandle : filters)
	{
		if (mFilters.has(filterHandle) && mFilters[filterHandle])
			mFilters[filterHandle]->appendSteps(cmd.filters);
	}

	mActiveFrameData->commands.push_back(std::move(cmd));
}

void RenderInterface::PopLayer()
{
	assert(mActiveFrameData);

	if (mLayerDepth > 1)
	{
		--mLayerDepth;
		mActiveFrameData->commands.push_back(PopLayerCmd{ activeLayer() });
	}
}

Rml::CompiledFilterHandle RenderInterface::CompileFilter(
	const Rml::String& name,
	const Rml::Dictionary& parameters)
{
	auto maker = mFilterMakers.find(name);
	if (maker == mFilterMakers.end())
		return {};

	auto filter = maker->second->make(parameters);
	return mFilters.insert(std::move(filter));
}

void RenderInterface::ReleaseFilter(Rml::CompiledFilterHandle filter)
{
	if (!mFilters.has(filter))
		return;

	if (auto& compiled = mFilters.at(filter))
	{
		if (auto* ownedTex = compiled->ownedTexture())
			mReleaseRenderTextures.push_back(ownedTex);
	}
	mFilters.erase(filter);
}

Rml::TextureHandle RenderInterface::SaveLayerAsTexture()
{
	assert(mActiveFrameData);

	Rml::Vector2i dimensions{};
	Ogre::TextureGpu* texture = createSaveLayerTexture(dimensions);

	Ogre::String id = mName;
	id.append("_Texture_");
	id.append(std::to_string(mDatablockId++));
	auto* datablock = static_cast<Ogre::HlmsUnlitDatablock*>(
		mHlms->createDatablock(id, id, mMacroblock, mBlendblock, Ogre::HlmsParamVec()));
	datablock->setTexture(0, texture);
	datablock->setUseColour(true);

	mActiveFrameData->commands.push_back(SaveLayerCmd{
		.sourceLayer = activeLayer(),
		.dstTexture = texture,
		.scissor = mCurrentScissor
		});

	Material material{ nullptr, datablock, texture };
	material.calculateHlmsHash();
	return mMaterials.insert(std::move(material));
}

Rml::CompiledFilterHandle RenderInterface::SaveLayerAsMaskImage()
{
	assert(mActiveFrameData);

	Rml::Vector2i dimensions{};
	Ogre::TextureGpu* texture = createSaveLayerTexture(dimensions);

	mActiveFrameData->commands.push_back(SaveLayerCmd{
		.sourceLayer = activeLayer(),
		.dstTexture = texture,
		.scissor = mCurrentScissor
		});

	auto filter = std::make_unique<SingleMaterialFilter>(
		mMaskImageFilterMaker.make(texture, true));
	return mFilters.insert(std::move(filter));
}

Rml::CompiledShaderHandle RenderInterface::CompileShader(
	const Rml::String& name,
	const Rml::Dictionary& parameters)
{
	auto maker = mShaderMakers.find(name);
	if (maker == mShaderMakers.end())
		return {};

	auto shader = maker->second->make(parameters);
	shader->setMacroblock(mMacroblock);
	shader->setBlendblock(mBlendblock);

	Material material{ shader };
	material.calculateHlmsHash();
	return mShaders.insert(std::move(material));
}

void RenderInterface::RenderShader(
	Rml::CompiledShaderHandle shader,
	Rml::CompiledGeometryHandle geometry,
	Rml::Vector2f translation,
	Rml::TextureHandle /*texture*/)
{
	assert(mActiveFrameData);

	auto& material = mShaders.at(shader);
	if (material.needsHashing())
		material.calculateHlmsHash();

	mActiveFrameData->commands.push_back(DrawCmd{
		.vao = reinterpret_cast<Ogre::VertexArrayObject*>(geometry),
		.material = material,
		.transform = mCurrentTransform,
		.translation = translation,
		.scissor = mCurrentScissor,
		.stencilMode = mStencilEnabled ? StencilMode::TestEqual : StencilMode::Disabled,
		.stencilRef = mStencilRef
		});
}

void RenderInterface::ReleaseShader(Rml::CompiledShaderHandle shader)
{
	mShaders.erase(shader);
}