// CompositorPassRml.hpp
#pragma once

#include "../RenderObject.hpp"
#include "../RmlCommandBuffer.hpp"

#include <Compositor/Pass/OgreCompositorPass.h>
#include <Math/Array/OgreNodeMemoryManager.h>
#include <Math/Array/OgreObjectMemoryManager.h>
#include <OgreMemoryStdAlloc.h>
#include <OgreSceneNode.h>

#include <memory>
#include <vector>

namespace Ogre {
	class Camera;
	class RenderQueue;
	struct VertexArrayObject;
}

namespace nimble::RmlOgre {

	class CompositorPassRmlDef;

	class CompositorPassRml final : public Ogre::CompositorPass {
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

	private:
		void applyScissor(const Ogre::Vector4& normScissor);
		void updateLocalSceneNodes();
		void flushBatch(
			Ogre::RenderSystem* rs,
			const Ogre::Matrix4& projMatrix,
			const Ogre::Vector4& normScissor,
			StencilMode stencilMode,
			Ogre::uint32 stencilRef);
		void executeClearStencil(Ogre::RenderSystem* rs, const ClearStencilCmd& cmd);

		const CompositorPassRmlDef* mDefinition = nullptr;
		const RmlFrameData* mFrameData = nullptr;

		Ogre::SceneManager* mSceneManager = nullptr;
		Ogre::Camera* mOrthoCamera = nullptr;

		Ogre::ObjectMemoryManager mObjectMemoryManager;
		Ogre::NodeMemoryManager   mNodeMemoryManager;

		std::vector<Ogre::SceneNode> mSceneNodes;
		std::vector<RenderObject, Ogre::STLAllocator<RenderObject, Ogre::AlignAllocPolicy<>>> mRenderObjects;

		std::unique_ptr<Ogre::RenderQueue> mRenderQueue;
		Ogre::VertexArrayObject* mFullscreenVao = nullptr;

		std::size_t   mNumStaticTextureDeps = 0;
		Ogre::Vector4 mLastAppliedScissor{ -1.0f, -1.0f, -1.0f, -1.0f };
		bool          mRenderPassStarted = false;
	};

} // namespace nimble::RmlOgre