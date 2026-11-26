#ifndef NIMBLE_RMLOGRE_RENDERINTERFACE_HPP
#define NIMBLE_RMLOGRE_RENDERINTERFACE_HPP

#include "Material.hpp"
#include "ObjectIndex.hpp"
#include "RmlCommandBuffer.hpp"
#include "ShaderMaker.hpp"

#include <OgreHlmsDatablock.h>
#include <OgreHlmsSamplerblock.h>
#include <OgreMatrix4.h>
#include <OgreString.h>

#include <RmlUi/Core/RenderInterface.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace Ogre {

	class HlmsUnlit;
	class HlmsUnlitDatablock;
	class SceneManager;
	class TextureGpu;

}

namespace nimble::RmlOgre {

	class RenderInterface : public Rml::RenderInterface
	{
		Ogre::String mName;

		Ogre::HlmsUnlit* mHlms = nullptr;
		Ogre::HlmsMacroblock mMacroblock;
		Ogre::HlmsBlendblock mBlendblock;
		Ogre::HlmsSamplerblock mSamplerblock;

		ObjectIndex<Material> mMaterials;
		std::unordered_map<Rml::String, std::unique_ptr<ShaderMaker>> mShaderMakers;
		ObjectIndex<Material> mShaders;

		// Текущее состояние записи команд кадра
		ScissorState mCurrentScissor{};
		Ogre::Matrix4 mCurrentTransform = Ogre::Matrix4::IDENTITY;
		bool mStencilEnabled = false;
		Ogre::uint32 mStencilRef = 0;

		RmlFrameData mFrameData{};

		int mDatablockId = 0;
		std::vector<Rml::CompiledGeometryHandle> mReleaseGeometries;
		std::vector<Rml::TextureHandle> mReleaseTextures;

		void releaseBufferedGeometries();
		void releaseBufferedTextures();

	public:
		explicit RenderInterface(Ogre::String name);

		// Временный конструктор совместимости для старых вызовов (UiSurface / тесты)
		RenderInterface(
			const Ogre::String& name,
			Ogre::SceneManager* /*sceneManager*/,
			Ogre::TextureGpu* /*output*/,
			Ogre::TextureGpu* /*background*/ = nullptr)
			: RenderInterface(name)
		{
		}

		const Ogre::String& getName() const noexcept { return mName; }
		const RmlFrameData& frameData() const noexcept { return mFrameData; }

		void AddShaderMaker(Rml::String name, std::unique_ptr<ShaderMaker> shaderMaker);

		void BeginFrame();
		void EndFrame();

		Rml::CompiledGeometryHandle CompileGeometry(
			Rml::Span<const Rml::Vertex> vertices,
			Rml::Span<const int> indices
		) override;
		void RenderGeometry(
			Rml::CompiledGeometryHandle geometry,
			Rml::Vector2f translation,
			Rml::TextureHandle texture
		) override;
		void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

		Rml::TextureHandle LoadTexture(
			Rml::Vector2i& texture_dimensions,
			const Rml::String& source
		) override;
		Rml::TextureHandle GenerateTexture(
			Rml::Span<const Rml::byte> source,
			Rml::Vector2i source_dimensions
		) override;
		void ReleaseTexture(Rml::TextureHandle texture) override;

		void EnableScissorRegion(bool enable) override;
		void SetScissorRegion(Rml::Rectanglei region) override;

		void SetTransform(const Rml::Matrix4f* transform) override;

		void EnableClipMask(bool enable) override;
		void RenderToClipMask(
			Rml::ClipMaskOperation operation,
			Rml::CompiledGeometryHandle geometry,
			Rml::Vector2f translation
		) override;

		// Заглушки Этапа 2 (слои и фильтры)
		Rml::LayerHandle PushLayer() override;
		void CompositeLayers(
			Rml::LayerHandle source,
			Rml::LayerHandle destination,
			Rml::BlendMode blend_mode,
			Rml::Span<const Rml::CompiledFilterHandle> filters
		) override;
		void PopLayer() override;

		Rml::CompiledFilterHandle CompileFilter(
			const Rml::String& name,
			const Rml::Dictionary& parameters
		) override;
		void ReleaseFilter(Rml::CompiledFilterHandle filter) override;

		Rml::TextureHandle SaveLayerAsTexture() override;
		Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;

		Rml::CompiledShaderHandle CompileShader(
			const Rml::String& name,
			const Rml::Dictionary& parameters
		) override;
		void RenderShader(
			Rml::CompiledShaderHandle shader,
			Rml::CompiledGeometryHandle geometry,
			Rml::Vector2f translation,
			Rml::TextureHandle texture
		) override;
		void ReleaseShader(Rml::CompiledShaderHandle shader) override;
	};

} // namespace nimble::RmlOgre

#endif // NIMBLE_RMLOGRE_RENDERINTERFACE_HPP