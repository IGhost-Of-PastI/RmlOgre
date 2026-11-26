// CompositorPassRml.cpp
#include "CompositorPassRml.hpp"
#include "CompositorPassRmlDef.hpp"
#include "../geometry.hpp"
#include <RmlUi/Core/Vertex.h>
#include <Compositor/OgreCompositorNode.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <OgreCamera.h>
#include <OgreCommon.h>
#include <OgreRenderQueue.h>
#include <OgreRenderSystem.h>
#include <OgreRoot.h>
#include <OgreSceneManager.h>
#include <Vao/OgreVaoManager.h>
#include <Vao/OgreVertexArrayObject.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>

using namespace nimble::RmlOgre;

namespace {

	Ogre::StencilParams make_stencil_params(StencilMode mode) noexcept {
		Ogre::StencilParams params{};
		params.readMask = 0xFF;
		params.writeMask = 0xFF;

		auto setBoth = [&](Ogre::CompareFunction cmp, Ogre::StencilOperation passOp) {
			params.enabled = true;
			params.stencilFront.compareOp = cmp;
			params.stencilFront.stencilPassOp = passOp;
			params.stencilFront.stencilFailOp = Ogre::SOP_KEEP;
			params.stencilFront.stencilDepthFailOp = Ogre::SOP_KEEP;
			params.stencilBack = params.stencilFront;
			};

		switch (mode) {
		case StencilMode::Disabled:
			params.enabled = false;
			break;
		case StencilMode::TestEqual:
			setBoth(Ogre::CMPF_EQUAL, Ogre::SOP_KEEP);
			break;
		case StencilMode::WriteSet:
		case StencilMode::WriteSetInverse:
			setBoth(Ogre::CMPF_ALWAYS_PASS, Ogre::SOP_REPLACE);
			break;
		case StencilMode::WriteIntersect:
			setBoth(Ogre::CMPF_ALWAYS_PASS, Ogre::SOP_INCREMENT);
			break;
		}
		return params;
	}

	Ogre::Vector4 to_norm_scissor(const ScissorState& sc, float w, float h) noexcept {
		if (!sc.enabled || w <= 0.0f || h <= 0.0f) {
			return Ogre::Vector4(0.0f, 0.0f, 1.0f, 1.0f);
		}
		return Ogre::Vector4(
			sc.region.Left() / w,
			sc.region.Top() / h,
			sc.region.Width() / w,
			sc.region.Height() / h);
	}

	Ogre::VertexArrayObject* create_fullscreen_triangle_vao() {
		// Полноэкранный треугольник в NDC [-1, 1] x [-1, 1]
		std::array<Rml::Vertex, 3> verts{};
		verts[0].position = { -1.0f, -1.0f };
		verts[1].position = { 3.0f, -1.0f };
		verts[2].position = { -1.0f,  3.0f };
		for (auto& v : verts) {
			v.colour = Rml::ColourbPremultiplied(255, 255, 255, 255);
		}
		std::array<int, 3> indices{ 0, 1, 2 };
		return create_vao(
			Rml::Span<const Rml::Vertex>{ verts.data(), verts.size() },
			Rml::Span<const int>{ indices.data(), indices.size() });
	}

	void destroy_vao(Ogre::VertexArrayObject* vao) {
		if (!vao) return;
		auto* root = Ogre::Root::getSingletonPtr();
		if (!root || !root->getRenderSystem()) return;
		auto* vaoMgr = root->getRenderSystem()->getVaoManager();
		if (!vaoMgr) return;

		for (auto* vb : vao->getVertexBuffers())
			vaoMgr->destroyVertexBuffer(vb);
		if (vao->getIndexBuffer())
			vaoMgr->destroyIndexBuffer(vao->getIndexBuffer());
		vaoMgr->destroyVertexArrayObject(vao);
	}

} // namespace

CompositorPassRml::CompositorPassRml(
	const CompositorPassRmlDef* definition,
	Ogre::Camera* defaultCamera,
	const Ogre::RenderTargetViewDef* rtv,
	Ogre::CompositorNode* parentNode)
	: Ogre::CompositorPass(definition, parentNode)
	, mDefinition(definition)
	, mFrameData(definition ? definition->mFrameData : nullptr)
	, mSceneManager(defaultCamera->getSceneManager())
{
	initialize(rtv);
	mNumStaticTextureDeps = mTextureDependencies.size();

	const std::string camName = "RmlOrthoCam_" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
	mOrthoCamera = mSceneManager->createCamera(camName, false);

	auto& root = Ogre::Root::getSingleton();
	mRenderQueue = std::make_unique<Ogre::RenderQueue>(
		root.getHlmsManager(),
		mSceneManager,
		root.getRenderSystem()->getVaoManager());
	mRenderQueue->setSortRenderQueue(
		RenderObject::RENDER_QUEUE_ID,
		Ogre::RenderQueue::RqSortMode::DisableSort);

	mFullscreenVao = create_fullscreen_triangle_vao();
}

CompositorPassRml::~CompositorPassRml() {
	mRenderObjects.clear();
	mSceneNodes.clear();
	mRenderQueue.reset();

	destroy_vao(mFullscreenVao);
	mFullscreenVao = nullptr;

	if (mSceneManager && mOrthoCamera) {
		mSceneManager->destroyCamera(mOrthoCamera);
		mOrthoCamera = nullptr;
	}
}

void CompositorPassRml::applyScissor(const Ogre::Vector4& normScissor) {
	if (mRenderPassStarted && mLastAppliedScissor == normScissor) {
		return;
	}

	auto* rs = mParentNode->getRenderSystem();
	auto* workspace = mParentNode->getWorkspace();
	const Ogre::uint8 workspaceVpMask = workspace->getViewportModifierMask();
	const bool applyModifier = (workspaceVpMask & mDefinition->mViewportModifierMask) != 0;
	const Ogre::Vector4 vpModifier = applyModifier
		? workspace->getViewportModifier()
		: Ogre::Vector4(0.0f, 0.0f, 1.0f, 1.0f);

	Ogre::Vector4 sc = normScissor;
	sc.x = std::clamp(sc.x + vpModifier.x, 0.0f, 1.0f);
	sc.y = std::clamp(sc.y + vpModifier.y, 0.0f, 1.0f);
	sc.z = std::clamp(sc.z * vpModifier.z, 0.0f, 1.0f);
	sc.w = std::clamp(sc.w * vpModifier.w, 0.0f, 1.0f);

	const Ogre::uint32 numViewports = mDefinition->mNumViewports;
	Ogre::Vector4 vpSize[16];
	Ogre::Vector4 scissors[16];

	for (Ogre::uint32 i = 0; i < numViewports; ++i) {
		vpSize[i] = Ogre::Vector4(
			mDefinition->mVpRect[i].mVpLeft + vpModifier.x,
			mDefinition->mVpRect[i].mVpTop + vpModifier.y,
			mDefinition->mVpRect[i].mVpWidth * vpModifier.z,
			mDefinition->mVpRect[i].mVpHeight * vpModifier.w);
		scissors[i] = sc;
	}

	rs->beginRenderPassDescriptor(
		mRenderPassDesc,
		mAnyTargetTexture,
		mAnyMipLevel,
		vpSize,
		scissors,
		numViewports,
		mDefinition->mIncludeOverlays,
		mDefinition->mWarnIfRtvWasFlushed);

	mLastAppliedScissor = normScissor;
	mRenderPassStarted = true;
}

void CompositorPassRml::updateLocalSceneNodes() {
	if (!mSceneNodes.empty()) {
		Ogre::Transform t;
		const std::size_t numNodes = mNodeMemoryManager.getFirstNode(t, 0);
		Ogre::Node::updateAllTransforms(numNodes, t);
	}
}

void CompositorPassRml::flushBatch(
	Ogre::RenderSystem* rs,
	const Ogre::Matrix4& projMatrix,
	const Ogre::Vector4& normScissor,
	StencilMode stencilMode,
	Ogre::uint32 stencilRef)
{
	if (mRenderObjects.empty()) {
		return;
	}

	applyScissor(normScissor);
	mOrthoCamera->setCustomProjectionMatrix(true, projMatrix);
	rs->setStencilBufferParams(stencilRef, make_stencil_params(stencilMode));

	updateLocalSceneNodes();
	mRenderQueue->renderPassPrepare(false, false);
	rs->executeRenderPassDescriptorDelayedActions();
	mRenderQueue->render(
		rs,
		RenderObject::RENDER_QUEUE_ID,
		RenderObject::RENDER_QUEUE_ID + 1,
		false,
		false);
	mRenderQueue->frameEnded();

	// Обязательно очищаем сразу: отвязывает RenderObject от HlmsDatablock
	// до следующего RenderInterface::BeginFrame() / releaseBufferedTextures()
	mRenderQueue->clear();
	mRenderObjects.clear();
	mSceneNodes.clear();
}

void CompositorPassRml::executeClearStencil(Ogre::RenderSystem* rs, const ClearStencilCmd& cmd) {
	if (!mFullscreenVao || !mFrameData || !mFrameData->stencilMaterial.datablock) {
		return;
	}

	mRenderObjects.clear();
	mSceneNodes.clear();
	mRenderQueue->clear();

	mRenderObjects.reserve(1);
	mSceneNodes.reserve(1);

	mRenderObjects.emplace_back(
		Ogre::Id::generateNewId<Ogre::MovableObject>(),
		&mObjectMemoryManager,
		nullptr,
		RenderObject::RENDER_QUEUE_ID);
	auto* obj = &mRenderObjects.back();
	obj->setVao(mFullscreenVao);
	obj->setPreparedMaterial(mFrameData->stencilMaterial);

	mSceneNodes.emplace_back(
		Ogre::Id::generateNewId<Ogre::Node>(),
		nullptr,
		&mNodeMemoryManager,
		nullptr);
	auto* node = &mSceneNodes.back();
	node->setPosition(Ogre::Vector3::ZERO);
	node->attachObject(obj);

	for (auto* renderable : obj->mRenderables) {
		mRenderQueue->addRenderableV2(0, RenderObject::RENDER_QUEUE_ID, false, renderable, obj);
	}

	// Сбрасываем stencil по всему RT с помощью полноэкранного треугольника в NDC
	flushBatch(
		rs,
		Ogre::Matrix4::IDENTITY,
		Ogre::Vector4(0.0f, 0.0f, 1.0f, 1.0f),
		StencilMode::WriteSet,
		cmd.value);
}

void CompositorPassRml::execute(const Ogre::Camera* /*lodCamera*/) {
	if (mNumPassesLeft != std::numeric_limits<Ogre::uint32>::max()) {
		if (!mNumPassesLeft)
			return;
		--mNumPassesLeft;
	}

	profilingBegin();
	notifyPassEarlyPreExecuteListeners();

	// Синхронизируем динамические textureDependencies для барьер-солвера Ogre
	mTextureDependencies.erase(
		mTextureDependencies.begin() + static_cast<std::ptrdiff_t>(mNumStaticTextureDeps),
		mTextureDependencies.end());
	if (mFrameData) {
		for (auto* tex : mFrameData->textureDependencies) {
			if (tex) {
				mTextureDependencies.push_back(Ogre::CompositorTexture(Ogre::IdString(), tex));
			}
		}
	}

	analyzeBarriers(true);
	executeResourceTransitions();

	mRenderPassStarted = false;
	mLastAppliedScissor = Ogre::Vector4(-1.0f, -1.0f, -1.0f, -1.0f);

	notifyPassPreExecuteListeners();

	if (mFrameData && !mFrameData->commands.empty()) {
		auto* rs = mSceneManager->getDestinationRenderSystem();
		const float w = static_cast<float>(mAnyTargetTexture->getWidth());
		const float h = static_cast<float>(mAnyTargetTexture->getHeight());

		const Ogre::Matrix4 orthoProj{
			2.0f / w,  0.0f,     0.0f, -1.0f,
			0.0f,     -2.0f / h, 0.0f,  1.0f,
			0.0f,      0.0f,     1.0f,  0.0f,
			0.0f,      0.0f,     0.0f,  1.0f
		};

		mSceneManager->_setCamerasInProgress(Ogre::CamerasInProgress(mOrthoCamera));
		mSceneManager->_setCurrentCompositorPass(this);

		const auto& cmds = mFrameData->commands;
		std::size_t idx = 0;

		while (idx < cmds.size()) {
			if (const auto* clearCmd = std::get_if<ClearStencilCmd>(&cmds[idx])) {
				executeClearStencil(rs, *clearCmd);
				++idx;
				continue;
			}

			const auto& firstDraw = std::get<DrawCmd>(cmds[idx]);
			std::size_t batchEnd = idx + 1;
			while (batchEnd < cmds.size()) {
				const auto* nextDraw = std::get_if<DrawCmd>(&cmds[batchEnd]);
				if (!nextDraw || !can_batch_draws(firstDraw, *nextDraw)) {
					break;
				}
				++batchEnd;
			}

			const std::size_t batchCount = batchEnd - idx;
			mRenderObjects.clear();
			mSceneNodes.clear();
			mRenderQueue->clear();
			mRenderObjects.reserve(batchCount);
			mSceneNodes.reserve(batchCount);

			for (std::size_t i = idx; i < batchEnd; ++i) {
				const auto& cmd = std::get<DrawCmd>(cmds[i]);
				if (!cmd.vao || !cmd.material.datablock)
					continue;

				mRenderObjects.emplace_back(
					Ogre::Id::generateNewId<Ogre::MovableObject>(),
					&mObjectMemoryManager,
					nullptr,
					RenderObject::RENDER_QUEUE_ID);
				auto* obj = &mRenderObjects.back();
				obj->setVao(cmd.vao);
				obj->setPreparedMaterial(cmd.material);

				mSceneNodes.emplace_back(
					Ogre::Id::generateNewId<Ogre::Node>(),
					nullptr,
					&mNodeMemoryManager,
					nullptr);
				auto* node = &mSceneNodes.back();
				node->setPosition(Ogre::Vector3{ cmd.translation.x, cmd.translation.y, 0.0f });
				node->attachObject(obj);

				for (auto* renderable : obj->mRenderables) {
					mRenderQueue->addRenderableV2(
						0, RenderObject::RENDER_QUEUE_ID, false, renderable, obj);
				}
			}

			flushBatch(
				rs,
				orthoProj * firstDraw.transform,
				to_norm_scissor(firstDraw.scissor, w, h),
				firstDraw.stencilMode,
				firstDraw.stencilRef);

			idx = batchEnd;
		}

		rs->setStencilBufferParams(0u, Ogre::StencilParams{});
		mSceneManager->_setCurrentCompositorPass(nullptr);
	}
	else {
		// Даже при пустом кадре открываем RenderPassDescriptor (важно для Load/Store semantics)
		setRenderPassDescToCurrent();
	}

	notifyPassPosExecuteListeners();
	profilingEnd();
}