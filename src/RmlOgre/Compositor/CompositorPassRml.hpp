// CompositorPassRml.hpp
#pragma once

#include "../RenderObject.hpp"
#include "../RmlCommandBuffer.hpp"

#include <Compositor/Pass/OgreCompositorPass.h>
#include <Math/Array/OgreNodeMemoryManager.h>
#include <Math/Array/OgreObjectMemoryManager.h>
#include <OgreMaterial.h>
#include <OgreMemoryStdAlloc.h>
#include <OgreResourceTransition.h>
#include <OgreSceneNode.h>
#include <OgreTextureGpuListener.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Ogre {
	class Camera;
	class RenderPassDescriptor;
	class RenderQueue;
	struct VertexArrayObject;

	namespace v1 {
		class Rectangle2D;
	}
}

namespace nimble::RmlOgre {

	class CompositorPassRmlDef;

	class CompositorPassRml final
		: public Ogre::CompositorPass
		, public Ogre::TextureGpuListener
	{
	public:
		CompositorPassRml(
			const CompositorPassRmlDef* definition,
			Ogre::Camera* defaultCamera,
			const Ogre::RenderTargetViewDef* rtv,
			Ogre::CompositorNode* parentNode);

		~CompositorPassRml() override;

		void setFrameData(const RmlFrameData* frameData) noexcept { mFrameData = frameData; }
		const RmlFrameData* frameData() const noexcept { return mFrameData; }

		void execute(const Ogre::Camera* lodCamera) override;

		void notifyTextureChanged(
			Ogre::TextureGpu* texture,
			Ogre::TextureGpuListener::Reason reason,
			void* extraData) override;

	private:
		struct OffscreenSlot {
			Ogre::TextureGpu* texture = nullptr;
			Ogre::RenderPassDescriptor* clearRpd = nullptr;
			Ogre::RenderPassDescriptor* loadRpd = nullptr;
		};

		void initQuadMaterials();
		void ensureLayer0LoadRpd();
		void destroyLayerPool();
		void ensurePoolMatchesTarget();
		OffscreenSlot& ensureLayerSlot(std::uint32_t layerId);
		void ensureScratchSlots();
		OffscreenSlot createOffscreenSlot(const std::string& name, bool attachDepthStencil);
		void destroyOffscreenSlot(OffscreenSlot& slot);

		Ogre::TextureGpu* getLayerTexture(std::uint32_t layerId);
		Ogre::RenderPassDescriptor* getLayerLoadRpd(std::uint32_t layerId);
		Ogre::RenderPassDescriptor* getOrCreateSaveRpd(Ogre::TextureGpu* dstTexture);

		void transitionTexture(
			Ogre::TextureGpu* texture,
			Ogre::ResourceLayout::Layout layout,
			Ogre::ResourceAccess::ResourceAccess access,
			Ogre::uint8 stageMask);

		void endCurrentRenderPass(Ogre::RenderSystem* rs);
		void ensureLayer0ClearedIfNeeded(Ogre::RenderSystem* rs);
		void openRenderPass(
			Ogre::RenderSystem* rs,
			Ogre::RenderPassDescriptor* rpd,
			Ogre::TextureGpu* targetTex,
			const Ogre::Vector4& normScissor,
			bool applyVpModifier);

		void updateLocalSceneNodes();
		void flushBatch(
			Ogre::RenderSystem* rs,
			const Ogre::Matrix4& projMatrix,
			const Ogre::Vector4& normScissor,
			StencilMode stencilMode,
			Ogre::uint32 stencilRef);

		void drawFullscreenQuad(
			Ogre::RenderSystem* rs,
			const Ogre::MaterialPtr& mat,
			Ogre::TextureGpu* srcTex);

		void drawScratchQuad(
			Ogre::RenderSystem* rs,
			const Ogre::MaterialPtr& mat,
			Ogre::TextureGpu* srcTex,
			Ogre::RenderPassDescriptor* dstRpd,
			Ogre::TextureGpu* dstTex,
			const ScissorState& scissor,
			Ogre::TextureGpu* extraTex = nullptr);

		void applyBlurPass(
			Ogre::RenderSystem* rs,
			const FilterStep& step,
			const ScissorState& origScissor,
			OffscreenSlot*& curr,
			OffscreenSlot*& other);

		void executeClearStencil(Ogre::RenderSystem* rs, const ClearStencilCmd& cmd);
		void handlePushLayer(Ogre::RenderSystem* rs, const PushLayerCmd& cmd);
		void handlePopLayer(Ogre::RenderSystem* rs, const PopLayerCmd& cmd);
		void handleCompositeLayers(Ogre::RenderSystem* rs, const CompositeLayersCmd& cmd);
		void handleSaveLayer(Ogre::RenderSystem* rs, const SaveLayerCmd& cmd);

		const CompositorPassRmlDef* mDefinition = nullptr;
		const RmlFrameData* mFrameData = nullptr;

		Ogre::SceneManager* mSceneManager = nullptr;
		Ogre::Camera* mOrthoCamera = nullptr;
		Ogre::v1::Rectangle2D* mFsRect = nullptr;

		Ogre::MaterialPtr mAlphaBlendMat;
		Ogre::MaterialPtr mCopyMat;
		Ogre::MaterialPtr mScissorCopyMat;

		Ogre::ObjectMemoryManager mObjectMemoryManager;
		Ogre::NodeMemoryManager   mNodeMemoryManager;

		std::vector<Ogre::SceneNode> mSceneNodes;
		std::vector<RenderObject, Ogre::STLAllocator<RenderObject, Ogre::AlignAllocPolicy<>>> mRenderObjects;

		std::unique_ptr<Ogre::RenderQueue> mRenderQueue;
		Ogre::VertexArrayObject* mFullscreenVao = nullptr;

		// Пул слоёв и scratch-буферов (выделяются лениво)
		Ogre::RenderPassDescriptor* mLayer0LoadRpd = nullptr;
		Ogre::uint32                mPoolWidth = 0;
		Ogre::uint32                mPoolHeight = 0;
		std::vector<OffscreenSlot>  mLayerSlots; // индекс i = layerId (i + 1)
		OffscreenSlot               mScratchA{};
		OffscreenSlot               mScratchB{};
		OffscreenSlot               mScratchShadow{};

		// Кэш RenderPassDescriptor для SaveLayerAsTexture / SaveLayerAsMaskImage
		std::unordered_map<Ogre::TextureGpu*, Ogre::RenderPassDescriptor*> mSaveRpdCache;

		std::size_t                 mNumStaticTextureDeps = 0;
		std::uint32_t               mCurrentLayer = 0;
		Ogre::RenderPassDescriptor* mActiveRpd = nullptr;
		Ogre::Vector4               mLastAppliedScissor{ -1.0f, -1.0f, -1.0f, -1.0f };
		bool                        mLayer0Touched = false;
		bool                        mUsedOffscreen = false;
	};

} // namespace nimble::RmlOgre