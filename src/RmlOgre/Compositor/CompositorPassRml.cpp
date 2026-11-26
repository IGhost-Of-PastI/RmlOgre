// CompositorPassRml.cpp
#include "CompositorPassRml.hpp"
#include "CompositorPassRmlDef.hpp"
#include "../geometry.hpp"

#include <Compositor/OgreCompositorManager2.h>
#include <Compositor/OgreCompositorNode.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <OgreCamera.h>
#include <OgreCommon.h>
#include <OgreGpuProgramParams.h>
#include <OgreHlmsDatablock.h>
#include <OgreMaterialManager.h>
#include <OgreRectangle2D.h>
#include <OgreRenderPassDescriptor.h>
#include <OgreRenderQueue.h>
#include <OgreRenderSystem.h>
#include <OgreRoot.h>
#include <OgreSceneManager.h>
#include <OgreTechnique.h>
#include <OgreTextureGpu.h>
#include <OgreTextureGpuManager.h>
#include <OgreTextureUnitState.h>
#include <Vao/OgreVaoManager.h>
#include <Vao/OgreVertexArrayObject.h>
#include <OgreHlms.h>
#include <OgreHlmsManager.h>

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

	Ogre::MaterialPtr clone_quad_material(const char* baseName) {
		auto base = Ogre::MaterialManager::getSingleton().getByName(baseName);
		if (!base)
			return {};

		Ogre::MaterialPtr mat(OGRE_NEW Ogre::Material(nullptr, "", 0, "", false, nullptr));
		*mat = *base;
		mat->load();

		Ogre::HlmsMacroblock macroblock;
		macroblock.mScissorTestEnabled = true;
		macroblock.mDepthCheck = false;
		macroblock.mDepthWrite = false;
		macroblock.mCullMode = Ogre::CULL_NONE;
		mat->setMacroblock(macroblock);

		return mat;
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
	mFsRect = parentNode->getWorkspace()->getCompositorManager()->getSharedFullscreenTriangle();

	auto& root = Ogre::Root::getSingleton();
	mRenderQueue = std::make_unique<Ogre::RenderQueue>(
		root.getHlmsManager(),
		mSceneManager,
		root.getRenderSystem()->getVaoManager());
	mRenderQueue->setSortRenderQueue(
		RenderObject::RENDER_QUEUE_ID,
		Ogre::RenderQueue::RqSortMode::DisableSort);

	mFullscreenVao = create_fullscreen_triangle_vao();
	initQuadMaterials();
}

CompositorPassRml::~CompositorPassRml() {
	mRenderObjects.clear();
	mSceneNodes.clear();
	mRenderQueue.reset();

	auto* rs = mParentNode ? mParentNode->getRenderSystem() : nullptr;

	for (auto& [tex, rpd] : mSaveRpdCache) {
		if (tex)
			tex->removeListener(this);
		if (rs && rpd)
			rs->destroyRenderPassDescriptor(rpd);
	}
	mSaveRpdCache.clear();

	destroyLayerPool();

	if (rs && mLayer0LoadRpd) {
		rs->destroyRenderPassDescriptor(mLayer0LoadRpd);
		mLayer0LoadRpd = nullptr;
	}

	destroy_vao(mFullscreenVao);
	mFullscreenVao = nullptr;

	if (mSceneManager && mOrthoCamera) {
		mSceneManager->destroyCamera(mOrthoCamera);
		mOrthoCamera = nullptr;
	}
}

void CompositorPassRml::initQuadMaterials() {
	mAlphaBlendMat = clone_quad_material("Rml/AlphaBlend");
	mCopyMat = clone_quad_material("Ogre/Copy/4xFP32");
	mScissorCopyMat = clone_quad_material("Rml/ScissorCopy");
}

void CompositorPassRml::notifyTextureChanged(
	Ogre::TextureGpu* texture,
	Ogre::TextureGpuListener::Reason reason,
	void* /*extraData*/)
{
	if (reason == Ogre::TextureGpuListener::Deleted) {
		auto it = mSaveRpdCache.find(texture);
		if (it != mSaveRpdCache.end()) {
			if (auto* rs = mParentNode ? mParentNode->getRenderSystem() : nullptr) {
				if (it->second)
					rs->destroyRenderPassDescriptor(it->second);
			}
			mSaveRpdCache.erase(it);
		}
		if (texture)
			texture->removeListener(this);
	}
}

void CompositorPassRml::ensureLayer0LoadRpd() {
	if (!mRenderPassDesc)
		return;

	auto* rs = mParentNode->getRenderSystem();
	auto* colorTex = mRenderPassDesc->mColour[0].texture;
	auto* depthTex = mRenderPassDesc->mDepth.texture;
	auto* stencilTex = mRenderPassDesc->mStencil.texture;

	if (mLayer0LoadRpd
		&& mLayer0LoadRpd->mColour[0].texture == colorTex
		&& mLayer0LoadRpd->mDepth.texture == depthTex
		&& mLayer0LoadRpd->mStencil.texture == stencilTex)
	{
		return;
	}

	if (mLayer0LoadRpd) {
		rs->destroyRenderPassDescriptor(mLayer0LoadRpd);
		mLayer0LoadRpd = nullptr;
	}

	mLayer0LoadRpd = rs->createRenderPassDescriptor();
	mLayer0LoadRpd->mColour[0].texture = colorTex;
	mLayer0LoadRpd->mColour[0].loadAction = Ogre::LoadAction::Load;
	mLayer0LoadRpd->mColour[0].storeAction = Ogre::StoreAction::Store;

	if (depthTex) {
		mLayer0LoadRpd->mDepth.texture = depthTex;
		mLayer0LoadRpd->mDepth.loadAction = Ogre::LoadAction::Load;
		mLayer0LoadRpd->mDepth.storeAction = Ogre::StoreAction::Store;
	}
	if (stencilTex) {
		mLayer0LoadRpd->mStencil.texture = stencilTex;
		mLayer0LoadRpd->mStencil.loadAction = Ogre::LoadAction::Load;
		mLayer0LoadRpd->mStencil.storeAction = Ogre::StoreAction::Store;
	}
	mLayer0LoadRpd->entriesModified(Ogre::RenderPassDescriptor::All);
}

CompositorPassRml::OffscreenSlot CompositorPassRml::createOffscreenSlot(
	const std::string& name,
	bool attachDepthStencil)
{
	auto* rs = mParentNode->getRenderSystem();
	auto* texMgr = rs->getTextureGpuManager();

	OffscreenSlot slot{};
	slot.texture = texMgr->createTexture(
		name,
		Ogre::GpuPageOutStrategy::Discard,
		Ogre::TextureFlags::RenderToTexture,
		Ogre::TextureTypes::Type2D);
	slot.texture->setPixelFormat(Ogre::PFG_RGBA16_FLOAT);
	slot.texture->setResolution(mPoolWidth, mPoolHeight);
	slot.texture->setNumMipmaps(1u);
	slot.texture->scheduleTransitionTo(Ogre::GpuResidency::Resident);
	texMgr->waitForStreamingCompletion();

	auto makeRpd = [&](Ogre::LoadAction::LoadAction colorLoad) {
		auto* rpd = rs->createRenderPassDescriptor();
		rpd->mColour[0].texture = slot.texture;
		rpd->mColour[0].loadAction = colorLoad;
		rpd->mColour[0].storeAction = Ogre::StoreAction::Store;
		rpd->mColour[0].clearColour = Ogre::ColourValue(0.0f, 0.0f, 0.0f, 0.0f);

		if (attachDepthStencil && mRenderPassDesc) {
			if (mRenderPassDesc->mDepth.texture) {
				rpd->mDepth.texture = mRenderPassDesc->mDepth.texture;
				rpd->mDepth.loadAction = Ogre::LoadAction::Load;
				rpd->mDepth.storeAction = Ogre::StoreAction::Store;
			}
			if (mRenderPassDesc->mStencil.texture) {
				rpd->mStencil.texture = mRenderPassDesc->mStencil.texture;
				rpd->mStencil.loadAction = Ogre::LoadAction::Load;
				rpd->mStencil.storeAction = Ogre::StoreAction::Store;
			}
		}

		rpd->entriesModified(Ogre::RenderPassDescriptor::All);
		return rpd;
		};

	slot.clearRpd = makeRpd(Ogre::LoadAction::Clear);
	slot.loadRpd = makeRpd(Ogre::LoadAction::Load);
	return slot;
}

void CompositorPassRml::destroyOffscreenSlot(OffscreenSlot& slot) {
	auto* rs = mParentNode ? mParentNode->getRenderSystem() : nullptr;
	if (rs) {
		if (slot.clearRpd)
			rs->destroyRenderPassDescriptor(slot.clearRpd);
		if (slot.loadRpd)
			rs->destroyRenderPassDescriptor(slot.loadRpd);
		if (slot.texture)
			rs->getTextureGpuManager()->destroyTexture(slot.texture);
	}
	slot = {};
}

void CompositorPassRml::destroyLayerPool() {
	for (auto& slot : mLayerSlots)
		destroyOffscreenSlot(slot);
	mLayerSlots.clear();

	destroyOffscreenSlot(mScratchA);
	destroyOffscreenSlot(mScratchB);
	destroyOffscreenSlot(mScratchShadow);

	mPoolWidth = 0;
	mPoolHeight = 0;
}

void CompositorPassRml::ensurePoolMatchesTarget() {
	const Ogre::uint32 w = mAnyTargetTexture->getWidth();
	const Ogre::uint32 h = mAnyTargetTexture->getHeight();
	if (mPoolWidth != w || mPoolHeight != h) {
		destroyLayerPool();
		mPoolWidth = w;
		mPoolHeight = h;
	}
}

CompositorPassRml::OffscreenSlot& CompositorPassRml::ensureLayerSlot(std::uint32_t layerId) {
	ensurePoolMatchesTarget();
	const std::size_t idx = layerId - 1;
	while (mLayerSlots.size() <= idx) {
		const std::string texName = "RmlLayer_"
			+ std::to_string(reinterpret_cast<std::uintptr_t>(this))
			+ "_" + std::to_string(mLayerSlots.size() + 1);
		mLayerSlots.push_back(createOffscreenSlot(texName, true));
	}
	return mLayerSlots[idx];
}

void CompositorPassRml::ensureScratchSlots() {
	ensurePoolMatchesTarget();
	const std::string prefix = "RmlScratch_" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
	if (!mScratchA.texture)
		mScratchA = createOffscreenSlot(prefix + "_A", false);
	if (!mScratchB.texture)
		mScratchB = createOffscreenSlot(prefix + "_B", false);
	if (!mScratchShadow.texture)
		mScratchShadow = createOffscreenSlot(prefix + "_Shadow", false);
}

Ogre::TextureGpu* CompositorPassRml::getLayerTexture(std::uint32_t layerId) {
	if (layerId == 0)
		return mAnyTargetTexture;
	return ensureLayerSlot(layerId).texture;
}

Ogre::RenderPassDescriptor* CompositorPassRml::getLayerLoadRpd(std::uint32_t layerId) {
	if (layerId == 0) {
		if (!mLayer0Touched || mActiveRpd == mRenderPassDesc)
			return mRenderPassDesc;
		ensureLayer0LoadRpd();
		return mLayer0LoadRpd;
	}
	return ensureLayerSlot(layerId).loadRpd;
}

Ogre::RenderPassDescriptor* CompositorPassRml::getOrCreateSaveRpd(Ogre::TextureGpu* dstTexture) {
	auto it = mSaveRpdCache.find(dstTexture);
	if (it != mSaveRpdCache.end())
		return it->second;

	auto* rs = mParentNode->getRenderSystem();
	auto* rpd = rs->createRenderPassDescriptor();
	rpd->mColour[0].texture = dstTexture;
	rpd->mColour[0].loadAction = Ogre::LoadAction::Clear;
	rpd->mColour[0].storeAction = Ogre::StoreAction::Store;
	rpd->mColour[0].clearColour = Ogre::ColourValue(0.0f, 0.0f, 0.0f, 0.0f);
	rpd->entriesModified(Ogre::RenderPassDescriptor::All);

	dstTexture->addListener(this);
	mSaveRpdCache.emplace(dstTexture, rpd);
	return rpd;
}

void CompositorPassRml::transitionTexture(
	Ogre::TextureGpu* texture,
	Ogre::ResourceLayout::Layout layout,
	Ogre::ResourceAccess::ResourceAccess access,
	Ogre::uint8 stageMask)
{
	if (!texture)
		return;
	mResourceTransitions.clear();
	resolveTransition(texture, layout, access, stageMask);
	executeResourceTransitions();
}

void CompositorPassRml::endCurrentRenderPass(Ogre::RenderSystem* rs) {
	if (mActiveRpd) {
		if (rs->getCurrentPassDescriptor())
			rs->setStencilBufferParams(0u, Ogre::StencilParams{});
		rs->endRenderPassDescriptor();
		mActiveRpd = nullptr;
		mLastAppliedScissor = Ogre::Vector4(-1.0f, -1.0f, -1.0f, -1.0f);
	}
}

void CompositorPassRml::ensureLayer0ClearedIfNeeded(Ogre::RenderSystem* rs) {
	if (mLayer0Touched)
		return;

	openRenderPass(rs, mRenderPassDesc, mAnyTargetTexture, Ogre::Vector4(0.0f, 0.0f, 1.0f, 1.0f), true);
	rs->executeRenderPassDescriptorDelayedActions();
	endCurrentRenderPass(rs);
}

void CompositorPassRml::openRenderPass(
	Ogre::RenderSystem* rs,
	Ogre::RenderPassDescriptor* rpd,
	Ogre::TextureGpu* targetTex,
	const Ogre::Vector4& normScissor,
	bool applyVpModifier)
{
	if (mActiveRpd == rpd && mLastAppliedScissor == normScissor)
		return;

	if (mActiveRpd && mActiveRpd != rpd) {
		rs->endRenderPassDescriptor();
		mActiveRpd = nullptr;
	}

	Ogre::Vector4 vpModifier(0.0f, 0.0f, 1.0f, 1.0f);
	Ogre::uint32 numViewports = 1u;
	Ogre::Vector4 vpSize[16];
	Ogre::Vector4 scissors[16];

	if (applyVpModifier) {
		auto* workspace = mParentNode->getWorkspace();
		const Ogre::uint8 workspaceVpMask = workspace->getViewportModifierMask();
		if ((workspaceVpMask & mDefinition->mViewportModifierMask) != 0)
			vpModifier = workspace->getViewportModifier();
		numViewports = mDefinition->mNumViewports;
	}

	Ogre::Vector4 sc = normScissor;
	sc.x = std::clamp(sc.x + vpModifier.x, 0.0f, 1.0f);
	sc.y = std::clamp(sc.y + vpModifier.y, 0.0f, 1.0f);
	sc.z = std::clamp(sc.z * vpModifier.z, 0.0f, 1.0f);
	sc.w = std::clamp(sc.w * vpModifier.w, 0.0f, 1.0f);

	for (Ogre::uint32 i = 0; i < numViewports; ++i) {
		if (applyVpModifier) {
			vpSize[i] = Ogre::Vector4(
				mDefinition->mVpRect[i].mVpLeft + vpModifier.x,
				mDefinition->mVpRect[i].mVpTop + vpModifier.y,
				mDefinition->mVpRect[i].mVpWidth * vpModifier.z,
				mDefinition->mVpRect[i].mVpHeight * vpModifier.w);
		}
		else {
			vpSize[i] = Ogre::Vector4(0.0f, 0.0f, 1.0f, 1.0f);
		}
		scissors[i] = sc;
	}

	rs->beginRenderPassDescriptor(
		rpd,
		targetTex,
		applyVpModifier ? mAnyMipLevel : 0u,
		vpSize,
		scissors,
		numViewports,
		applyVpModifier ? mDefinition->mIncludeOverlays : false,
		false);

	mActiveRpd = rpd;
	mLastAppliedScissor = normScissor;
	if (rpd == mRenderPassDesc)
		mLayer0Touched = true;
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
	if (mRenderObjects.empty())
		return;

	auto* targetTex = getLayerTexture(mCurrentLayer);
	auto* targetRpd = getLayerLoadRpd(mCurrentLayer);
	openRenderPass(rs, targetRpd, targetTex, normScissor, mCurrentLayer == 0);

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

	mRenderQueue->clear();
	mRenderObjects.clear();
	mSceneNodes.clear();
}

void CompositorPassRml::drawFullscreenQuad(
	Ogre::RenderSystem* rs,
	const Ogre::MaterialPtr& mat,
	Ogre::TextureGpu* srcTex)
{
	if (!mat || !mFsRect)
		return;

	auto* pass = mat->getBestTechnique()->getPass(0);

	struct QuadGuard {
		Ogre::v1::Rectangle2D* rect;
		Ogre::Pass* pass;
		bool                   clearTex0;

		~QuadGuard() {
			if (rect) {
				auto* defaultDb = Ogre::Root::getSingleton()
					.getHlmsManager()
					->getHlms(Ogre::HLMS_UNLIT)
					->getDefaultDatablock();
				rect->setDatablock(defaultDb);
			}
			if (clearTex0 && pass && pass->getNumTextureUnitStates() > 0) {
				pass->getTextureUnitState(0)->setTextureName(Ogre::BLANKSTRING);
			}
		}
	} guard{ mFsRect, pass, srcTex != nullptr };

	if (srcTex && pass->getNumTextureUnitStates() > 0)
		pass->getTextureUnitState(0)->setTexture(srcTex);

	const std::size_t numTUs = pass->getNumTextureUnitStates();
	for (std::size_t i = 0; i < numTUs; ++i)
		pass->getTextureUnitState(i)->_getTexturePtr();

	mFsRect->setMaterial(mat);
	rs->executeRenderPassDescriptorDelayedActions();
	mSceneManager->_renderSingleObject(mFsRect, mFsRect, false, false);
}

void CompositorPassRml::drawScratchQuad(
	Ogre::RenderSystem* rs,
	const Ogre::MaterialPtr& mat,
	Ogre::TextureGpu* srcTex,
	Ogre::RenderPassDescriptor* dstRpd,
	Ogre::TextureGpu* dstTex,
	const ScissorState& scissor,
	Ogre::TextureGpu* extraTex)
{
	endCurrentRenderPass(rs);

	transitionTexture(srcTex, Ogre::ResourceLayout::Texture, Ogre::ResourceAccess::Read, 1u << Ogre::PixelShader);
	if (extraTex)
		transitionTexture(extraTex, Ogre::ResourceLayout::Texture, Ogre::ResourceAccess::Read, 1u << Ogre::PixelShader);
	transitionTexture(dstTex, Ogre::ResourceLayout::RenderTarget, Ogre::ResourceAccess::Write, 0u);

	const float w = static_cast<float>(dstTex->getWidth());
	const float h = static_cast<float>(dstTex->getHeight());
	openRenderPass(rs, dstRpd, dstTex, to_norm_scissor(scissor, w, h), false);
	rs->setStencilBufferParams(0u, Ogre::StencilParams{});

	drawFullscreenQuad(rs, mat, srcTex);
	endCurrentRenderPass(rs);
}

void CompositorPassRml::applyBlurPass(
	Ogre::RenderSystem* rs,
	const FilterStep& step,
	const ScissorState& origScissor,
	OffscreenSlot*& curr,
	OffscreenSlot*& other)
{
	ScissorState sc = origScissor;
	for (int i = 0; i < step.halfSamples; ++i) {
		if (sc.enabled) {
			sc.region.p0 /= 2;
			sc.region.p1 = (sc.region.p1 + Rml::Vector2i{ 1, 1 }) / 2;
		}
		drawScratchQuad(rs, step.halfsampleMat, curr->texture, other->clearRpd, other->texture, sc);
		std::swap(curr, other);
	}

	if (sc.enabled) {
		sc.region.p0.x = origScissor.region.p0.x;
		sc.region.p1.x = origScissor.region.p1.x;
	}
	drawScratchQuad(rs, step.blurHMat, curr->texture, other->clearRpd, other->texture, sc);
	std::swap(curr, other);

	drawScratchQuad(rs, step.blurVMat, curr->texture, other->clearRpd, other->texture, origScissor);
	std::swap(curr, other);
}

void CompositorPassRml::executeClearStencil(Ogre::RenderSystem* rs, const ClearStencilCmd& cmd) {
	if (!mFullscreenVao || !mFrameData || !mFrameData->stencilMaterial.datablock)
		return;

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

	for (auto* renderable : obj->mRenderables)
		mRenderQueue->addRenderableV2(0, RenderObject::RENDER_QUEUE_ID, false, renderable, obj);

	flushBatch(
		rs,
		Ogre::Matrix4::IDENTITY,
		Ogre::Vector4(0.0f, 0.0f, 1.0f, 1.0f),
		StencilMode::WriteSet,
		cmd.value);
}

void CompositorPassRml::handlePushLayer(Ogre::RenderSystem* rs, const PushLayerCmd& cmd) {
	mUsedOffscreen = true;
	ensureLayer0ClearedIfNeeded(rs);
	endCurrentRenderPass(rs);

	auto& slot = ensureLayerSlot(cmd.layerId);
	transitionTexture(slot.texture, Ogre::ResourceLayout::RenderTarget, Ogre::ResourceAccess::Write, 0u);

	// Гарантированно очищаем новый слой в прозрачный (0,0,0,0), сохраняя общий stencil
	openRenderPass(rs, slot.clearRpd, slot.texture, Ogre::Vector4(0.0f, 0.0f, 1.0f, 1.0f), false);
	rs->executeRenderPassDescriptorDelayedActions();
	endCurrentRenderPass(rs);

	mCurrentLayer = cmd.layerId;
}

void CompositorPassRml::handlePopLayer(Ogre::RenderSystem* rs, const PopLayerCmd& cmd) {
	endCurrentRenderPass(rs);
	mCurrentLayer = cmd.restoreLayerId;
	transitionTexture(
		getLayerTexture(mCurrentLayer),
		Ogre::ResourceLayout::RenderTarget,
		Ogre::ResourceAccess::Write,
		0u);
}

void CompositorPassRml::handleCompositeLayers(Ogre::RenderSystem* rs, const CompositeLayersCmd& cmd) {
	mUsedOffscreen = true;
	ensureLayer0ClearedIfNeeded(rs);
	endCurrentRenderPass(rs);

	Ogre::TextureGpu* srcTex = getLayerTexture(cmd.sourceLayer);
	Ogre::TextureGpu* dstTex = getLayerTexture(cmd.destinationLayer);
	const auto& compositeMat = (cmd.blendMode == Rml::BlendMode::Replace) ? mCopyMat : mAlphaBlendMat;

	Ogre::TextureGpu* finalSrcTex = srcTex;

	if (!cmd.filters.empty() || cmd.sourceLayer == cmd.destinationLayer) {
		ensureScratchSlots();

		// Копируем исходный слой в mScratchA (с очисткой полей за пределами scissor)
		drawScratchQuad(rs, mCopyMat, srcTex, mScratchA.clearRpd, mScratchA.texture, cmd.scissor);

		OffscreenSlot* curr = &mScratchA;
		OffscreenSlot* other = &mScratchB;

		for (const auto& step : cmd.filters) {
			switch (step.kind) {
			case FilterStep::Kind::SingleMaterial:
				drawScratchQuad(
					rs, step.material, curr->texture,
					other->clearRpd, other->texture, cmd.scissor, step.extraTexture);
				std::swap(curr, other);
				break;

			case FilterStep::Kind::Blur:
				applyBlurPass(rs, step, cmd.scissor, curr, other);
				break;

			case FilterStep::Kind::DropShadow:
				// 1. Сохраняем исходный элемент в mScratchShadow
				drawScratchQuad(
					rs, mCopyMat, curr->texture,
					mScratchShadow.clearRpd, mScratchShadow.texture, cmd.scissor);
				// 2. Смещаем и красим тень
				drawScratchQuad(
					rs, step.material, curr->texture,
					other->clearRpd, other->texture, cmd.scissor);
				std::swap(curr, other);
				// 3. Размываем тень
				applyBlurPass(rs, step, cmd.scissor, curr, other);
				// 4. Рисуем исходный элемент поверх размытой тени (LoadAction::Load + AlphaBlend)
				drawScratchQuad(
					rs, mAlphaBlendMat, mScratchShadow.texture,
					curr->loadRpd, curr->texture, cmd.scissor);
				break;
			}
		}

		finalSrcTex = curr->texture;
	}

	// Финальный композитинг на destinationLayer с учётом scissor и stencil
	transitionTexture(finalSrcTex, Ogre::ResourceLayout::Texture, Ogre::ResourceAccess::Read, 1u << Ogre::PixelShader);
	transitionTexture(dstTex, Ogre::ResourceLayout::RenderTarget, Ogre::ResourceAccess::Write, 0u);

	const float dstW = static_cast<float>(dstTex->getWidth());
	const float dstH = static_cast<float>(dstTex->getHeight());
	openRenderPass(
		rs,
		getLayerLoadRpd(cmd.destinationLayer),
		dstTex,
		to_norm_scissor(cmd.scissor, dstW, dstH),
		cmd.destinationLayer == 0);

	rs->setStencilBufferParams(
		cmd.stencilEnabled ? cmd.stencilRef : 0u,
		make_stencil_params(cmd.stencilEnabled ? StencilMode::TestEqual : StencilMode::Disabled));

	drawFullscreenQuad(rs, compositeMat, finalSrcTex);
	endCurrentRenderPass(rs);

	// Возвращаемся к активному слою на вершине стека
	mCurrentLayer = cmd.restoreLayer;
	transitionTexture(
		getLayerTexture(mCurrentLayer),
		Ogre::ResourceLayout::RenderTarget,
		Ogre::ResourceAccess::Write,
		0u);
}

void CompositorPassRml::handleSaveLayer(Ogre::RenderSystem* rs, const SaveLayerCmd& cmd) {
	if (!cmd.dstTexture || !mScissorCopyMat)
		return;

	mUsedOffscreen = true;
	ensureLayer0ClearedIfNeeded(rs);
	endCurrentRenderPass(rs);

	Ogre::TextureGpu* srcTex = getLayerTexture(cmd.sourceLayer);
	const float srcW = static_cast<float>(srcTex->getWidth());
	const float srcH = static_cast<float>(srcTex->getHeight());
	const Ogre::Vector4 normScissor = to_norm_scissor(cmd.scissor, srcW, srcH);

	auto* pass = mScissorCopyMat->getBestTechnique()->getPass(0);
	pass->getVertexProgramParameters()->setNamedConstant("scissorRegion", normScissor);

	transitionTexture(srcTex, Ogre::ResourceLayout::Texture, Ogre::ResourceAccess::Read, 1u << Ogre::PixelShader);
	transitionTexture(cmd.dstTexture, Ogre::ResourceLayout::RenderTarget, Ogre::ResourceAccess::Write, 0u);

	auto* saveRpd = getOrCreateSaveRpd(cmd.dstTexture);
	openRenderPass(rs, saveRpd, cmd.dstTexture, Ogre::Vector4(0.0f, 0.0f, 1.0f, 1.0f), false);
	rs->setStencilBufferParams(0u, Ogre::StencilParams{});

	drawFullscreenQuad(rs, mScissorCopyMat, srcTex);
	endCurrentRenderPass(rs);

	// Сразу переводим сохранённую текстуру в SHADER_READ_ONLY для чтения в этом же кадре
	transitionTexture(cmd.dstTexture, Ogre::ResourceLayout::Texture, Ogre::ResourceAccess::Read, 1u << Ogre::PixelShader);
	transitionTexture(srcTex, Ogre::ResourceLayout::RenderTarget, Ogre::ResourceAccess::Write, 0u);
}

void CompositorPassRml::execute(const Ogre::Camera* /*lodCamera*/) {
	if (mNumPassesLeft != std::numeric_limits<Ogre::uint32>::max()) {
		if (!mNumPassesLeft)
			return;
		--mNumPassesLeft;
	}

	profilingBegin();
	notifyPassEarlyPreExecuteListeners();

	mTextureDependencies.erase(
		mTextureDependencies.begin() + static_cast<std::ptrdiff_t>(mNumStaticTextureDeps),
		mTextureDependencies.end());
	if (mFrameData) {
		for (auto* tex : mFrameData->textureDependencies) {
			if (tex)
				mTextureDependencies.push_back(Ogre::CompositorTexture(Ogre::IdString(), tex));
		}
	}

	analyzeBarriers(true);
	executeResourceTransitions();

	mCurrentLayer = 0;
	mActiveRpd = nullptr;
	mLastAppliedScissor = Ogre::Vector4(-1.0f, -1.0f, -1.0f, -1.0f);
	mLayer0Touched = false;
	mUsedOffscreen = false;

	notifyPassPreExecuteListeners();

	auto* rs = mSceneManager->getDestinationRenderSystem();

	if (mFrameData && !mFrameData->commands.empty()) {
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
			if (const auto* pushCmd = std::get_if<PushLayerCmd>(&cmds[idx])) {
				handlePushLayer(rs, *pushCmd);
				++idx;
				continue;
			}
			if (const auto* popCmd = std::get_if<PopLayerCmd>(&cmds[idx])) {
				handlePopLayer(rs, *popCmd);
				++idx;
				continue;
			}
			if (const auto* compCmd = std::get_if<CompositeLayersCmd>(&cmds[idx])) {
				handleCompositeLayers(rs, *compCmd);
				++idx;
				continue;
			}
			if (const auto* saveCmd = std::get_if<SaveLayerCmd>(&cmds[idx])) {
				handleSaveLayer(rs, *saveCmd);
				++idx;
				continue;
			}

			const auto& firstDraw = std::get<DrawCmd>(cmds[idx]);
			std::size_t batchEnd = idx + 1;
			while (batchEnd < cmds.size()) {
				const auto* nextDraw = std::get_if<DrawCmd>(&cmds[batchEnd]);
				if (!nextDraw || !can_batch_draws(firstDraw, *nextDraw))
					break;
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

				for (auto* renderable : obj->mRenderables)
					mRenderQueue->addRenderableV2(0, RenderObject::RENDER_QUEUE_ID, false, renderable, obj);
			}

			flushBatch(
				rs,
				orthoProj * firstDraw.transform,
				to_norm_scissor(firstDraw.scissor, w, h),
				firstDraw.stencilMode,
				firstDraw.stencilRef);

			idx = batchEnd;
		}

		if (rs->getCurrentPassDescriptor())
			rs->setStencilBufferParams(0u, Ogre::StencilParams{});
		mSceneManager->_setCurrentCompositorPass(nullptr);
	}
	else {
		setRenderPassDescToCurrent();
	}

	if (mUsedOffscreen || (mActiveRpd && mActiveRpd != mRenderPassDesc)) {
		endCurrentRenderPass(rs);
		if (mUsedOffscreen) {
			transitionTexture(
				mAnyTargetTexture,
				Ogre::ResourceLayout::RenderTarget,
				Ogre::ResourceAccess::Write,
				0u);
		}
	}

	notifyPassPosExecuteListeners();
	profilingEnd();
}