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

	/**
	 * @brief Синхронный композитор-пасс Ogre-Next, исполняющий команды @ref RmlFrameData на GPU.
	 *
	 * Работает внутри любого внешнего `CompositorWorkspace` (основной экран, offscreen RTT,
	 * multi-view сцена). Не регистрирует `RenderObject` и `SceneNode` в глобальном `SceneManager`,
	 * а держит изолированные `ObjectMemoryManager`, `NodeMemoryManager` и `RenderQueue` с режимом
	 * `DisableSort` (строгое сохранение порядка отрисовки UI).
	 *
	 * @par Управление Vulkan-барьерами и RenderPass
	 * Все переходы image layout выполняются через штатный `CompositorPass::resolveTransition()`
	 * и `executeResourceTransitions()`. Offscreen-слои и scratch-буферы выделяются лениво —
	 * для простого HUD без фильтров дополнительная видеопамять не расходуется.
	 */
	class CompositorPassRml final
		: public Ogre::CompositorPass
		, public Ogre::TextureGpuListener
	{
	public:
		/**
		 * @brief Создаёт пасс, служебную ортокамеру, локальный `RenderQueue`, NDC-треугольник
		 *        для очистки stencil и клоны материалов композитинга.
		 * @par Кто вызывает
		 * `Ogre::CompositorPassProvider::addPass()` при инстанцировании `CompositorWorkspace`.
		 */
		CompositorPassRml(
			const CompositorPassRmlDef* definition,
			Ogre::Camera* defaultCamera,
			const Ogre::RenderTargetViewDef* rtv,
			Ogre::CompositorNode* parentNode);

		/**
		 * @brief Освобождает локальные объекты, пул offscreen-слоёв, кэш `RenderPassDescriptor`,
		 *        отписывается от всех `TextureGpuListener` и уничтожает служебную камеру.
		 * @par Кто вызывает
		 * Ogre при удалении `CompositorWorkspace` / `CompositorNode`.
		 */
		~CompositorPassRml() override;

		/**
		 * @brief Подменяет источник команд кадра без пересоздания воркспейса.
		 * @par Кто вызывает
		 * Код приложения (например, при перепривязке `RenderInterface` к существующей стадии рендера).
		 */
		void setFrameData(const RmlFrameData* frameData) noexcept { mFrameData = frameData; }

		/// Текущий привязанный указатель на буфер команд (может быть `nullptr`).
		const RmlFrameData* frameData() const noexcept { return mFrameData; }

		/**
		 * @brief Точка входа GPU-рендера UI в кадре.
		 *
		 * Последовательно обходит `mFrameData->commands`: склеивает соседние @ref DrawCmd
		 * через @ref can_batch_draws и отправляет их в @ref flushBatch, а служебные команды
		 * делегирует в `executeClearStencil` / `handlePushLayer` / `handlePopLayer` /
		 * `handleCompositeLayers` / `handleSaveLayer`.
		 *
		 * @par Кто вызывает
		 * Исключительно `Ogre::CompositorNode::_update()` внутри `Ogre::Root::renderOneFrame()`.
		 *
		 * @post Если использовались offscreen-слои, базовый `mAnyTargetTexture` возвращается в
		 *       `ResourceLayout::RenderTarget`, а активный `RenderPassDescriptor` закрыт.
		 */
		void execute(const Ogre::Camera* lodCamera) override;

		/**
		 * @brief Колбэк жизненного цикла текстур для очистки кэша `mSaveRpdCache`.
		 *
		 * @par Кто вызывает
		 * Исключительно `Ogre::TextureGpuManager::destroyTexture()` при уничтожении текстуры,
		 * ранее переданной в @ref handleSaveLayer (например, при освобождении маски в `BeginFrame()`).
		 *
		 * @details При `Reason::Deleted` уничтожает кэшированный `RenderPassDescriptor` и
		 *          вызывает `texture->removeListener(this)` (обязательное требование `~TextureGpu`).
		 */
		void notifyTextureChanged(
			Ogre::TextureGpu* texture,
			Ogre::TextureGpuListener::Reason reason,
			void* extraData) override;

	private:
		/// Offscreen-буфер (`PFG_RGBA16_FLOAT`) с двумя готовыми `RenderPassDescriptor`: с очисткой и с сохранением.
		struct OffscreenSlot {
			Ogre::TextureGpu* texture = nullptr;
			Ogre::RenderPassDescriptor* clearRpd = nullptr; ///< `LoadAction::Clear` `(0,0,0,0)` + `StoreAction::Store`.
			Ogre::RenderPassDescriptor* loadRpd = nullptr;  ///< `LoadAction::Load` + `StoreAction::Store`.
		};

		/// Клонирует `Rml/AlphaBlend`, `Ogre/Copy/4xFP32`, `Rml/ScissorCopy` с включённым `mScissorTestEnabled`. Вызывается только из конструктора.
		void initQuadMaterials();

		/// Лениво создаёт `mLayer0LoadRpd` (`LoadAction::Load`) для возврата на базовый RT после offscreen-слоёв без повторного Clear. Вызывается из @ref getLayerLoadRpd.
		void ensureLayer0LoadRpd();

		/// Уничтожает все текстуры и `RenderPassDescriptor` пула слоёв и scratch-буферов. Вызывается из деструктора и @ref ensurePoolMatchesTarget при ресайзе.
		void destroyLayerPool();

		/// Проверяет совпадение габаритов пула с `mAnyTargetTexture`; при ресайзе сбрасывает пул. Вызывается из @ref ensureLayerSlot и @ref ensureScratchSlots.
		void ensurePoolMatchesTarget();

		/// Возвращает (лениво создавая с привязанным depth-stencil) слот слоя `layerId >= 1`. Вызывается из @ref handlePushLayer, @ref getLayerTexture, @ref getLayerLoadRpd.
		OffscreenSlot& ensureLayerSlot(std::uint32_t layerId);

		/// Лениво создаёт три scratch-буфера (`mScratchA`, `mScratchB`, `mScratchShadow`) без depth-stencil. Вызывается исключительно из @ref handleCompositeLayers при наличии фильтров или `source == dest`.
		void ensureScratchSlots();

		/// Выделяет одну `PFG_RGBA16_FLOAT` текстуру, дожидается её резидентности и собирает пару RPD. Вызывается из @ref ensureLayerSlot и @ref ensureScratchSlots.
		OffscreenSlot createOffscreenSlot(const std::string& name, bool attachDepthStencil);

		/// Освобождает текстуру и оба RPD слота. Вызывается из @ref destroyLayerPool.
		void destroyOffscreenSlot(OffscreenSlot& slot);

		/// Возвращает текстуру слоя (`0` -> `mAnyTargetTexture`, `>= 1` -> `mLayerSlots[layerId - 1]`).
		Ogre::TextureGpu* getLayerTexture(std::uint32_t layerId);

		/// Возвращает `RenderPassDescriptor` для продолжения рисования в слой `layerId` (сохраняет `mRenderPassDesc`, пока Layer 0 не покидали).
		Ogre::RenderPassDescriptor* getLayerLoadRpd(std::uint32_t layerId);

		/// Возвращает или создаёт и кэширует `RenderPassDescriptor` (`LoadAction::Clear`) для `dstTexture` из @ref SaveLayerCmd, подписывая `this` как `TextureGpuListener`.
		Ogre::RenderPassDescriptor* getOrCreateSaveRpd(Ogre::TextureGpu* dstTexture);

		/// Выполняет одиночный переход Vulkan image layout через `resolveTransition()` + `executeResourceTransitions()`. Вызывается при смене активного RT/слоя.
		void transitionTexture(
			Ogre::TextureGpu* texture,
			Ogre::ResourceLayout::Layout layout,
			Ogre::ResourceAccess::ResourceAccess access,
			Ogre::uint8 stageMask);

		/// Безопасно сбрасывает stencil-параметры и закрывает текущий `RenderPassDescriptor`, если он открыт. Вызывается перед любым барьером или сменой RT.
		void endCurrentRenderPass(Ogre::RenderSystem* rs);

		/// Если Layer 0 ещё ни разу не открывался в этом кадре, кратко открывает и закрывает `mRenderPassDesc`, чтобы отработала его семантика `LoadAction` (например, `Clear` от предыдущего пасса).
		void ensureLayer0ClearedIfNeeded(Ogre::RenderSystem* rs);

		/**
		 * @brief Открывает `rpd` с нужным scissor (или обновляет динамический `vkCmdSetScissor`, если тот же `rpd` уже активен).
		 * @param applyVpModifier `true` только для Layer 0 (учитывает `ViewportModifier` внешнего воркспейса); `false` для offscreen/scratch RT.
		 */
		void openRenderPass(
			Ogre::RenderSystem* rs,
			Ogre::RenderPassDescriptor* rpd,
			Ogre::TextureGpu* targetTex,
			const Ogre::Vector4& normScissor,
			bool applyVpModifier);

		/// Обновляет матрицы `mDerivedTransform` всех локальных `mSceneNodes` перед `renderPassPrepare()`. Вызывается исключительно из @ref flushBatch.
		void updateLocalSceneNodes();

		/**
		 * @brief Отправляет накопленный батч `mRenderObjects` в `mRenderQueue` и сразу отвязывает объекты от `HlmsDatablock`.
		 * @par Кто вызывает
		 * @ref execute (для батча `DrawCmd`) и @ref executeClearStencil (для полноэкранного NDC-треугольника).
		 */
		void flushBatch(
			Ogre::RenderSystem* rs,
			const Ogre::Matrix4& projMatrix,
			const Ogre::Vector4& normScissor,
			StencilMode stencilMode,
			Ogre::uint32 stencilRef);

		/**
		 * @brief Рисует общий полноэкранный треугольник `mFsRect` low-level материалом `mat` с текстурой `srcTex` в слоте 0.
		 * @par Кто вызывает
		 * @ref drawScratchQuad, @ref handleCompositeLayers (финальное наложение) и @ref handleSaveLayer.
		 * @details Внутренний RAII-guard гарантированно возвращает `mFsRect` на дефолтный `HlmsUnlitDatablock`
		 *          и очищает `TextureUnitState(0)` через `setTextureName("")`, чтобы не блокировать удаление материалов фильтров.
		 */
		void drawFullscreenQuad(
			Ogre::RenderSystem* rs,
			const Ogre::MaterialPtr& mat,
			Ogre::TextureGpu* srcTex);

		/**
		 * @brief Выполняет изолированный полноэкранный проход `srcTex` (+ опционально `extraTex`) -> `dstTex` с полным набором барьеров и открытием/закрытием `dstRpd`.
		 * @par Кто вызывает
		 * @ref handleCompositeLayers и @ref applyBlurPass.
		 */
		void drawScratchQuad(
			Ogre::RenderSystem* rs,
			const Ogre::MaterialPtr& mat,
			Ogre::TextureGpu* srcTex,
			Ogre::RenderPassDescriptor* dstRpd,
			Ogre::TextureGpu* dstTex,
			const ScissorState& scissor,
			Ogre::TextureGpu* extraTex = nullptr);

		/**
		 * @brief Исполняет цепочку `halfsample` (0..N раз) -> `blurH` -> `blurV` в ping-pong буферах `curr` / `other`.
		 * @par Кто вызывает
		 * Исключительно @ref handleCompositeLayers для шагов `FilterStep::Kind::Blur` и `DropShadow`.
		 * @post Результат размытия лежит в `*curr`.
		 */
		void applyBlurPass(
			Ogre::RenderSystem* rs,
			const FilterStep& step,
			const ScissorState& origScissor,
			OffscreenSlot*& curr,
			OffscreenSlot*& other);

		/// Исполняет @ref ClearStencilCmd через `mFullscreenVao` и `stencilMaterial`. Вызывается из @ref execute.
		void executeClearStencil(Ogre::RenderSystem* rs, const ClearStencilCmd& cmd);

		/// Исполняет @ref PushLayerCmd: выделяет/очищает слой и делает его текущим. Вызывается из @ref execute.
		void handlePushLayer(Ogre::RenderSystem* rs, const PushLayerCmd& cmd);

		/// Исполняет @ref PopLayerCmd: закрывает текущий RT и возвращает `mCurrentLayer` на предыдущий слой. Вызывается из @ref execute.
		void handlePopLayer(Ogre::RenderSystem* rs, const PopLayerCmd& cmd);

		/// Исполняет @ref CompositeLayersCmd: прогоняет `cmd.filters` в scratch-буферах и композитит результат в `cmd.destinationLayer`. Вызывается из @ref execute.
		void handleCompositeLayers(Ogre::RenderSystem* rs, const CompositeLayersCmd& cmd);

		/// Исполняет @ref SaveLayerCmd: копирует scissor-область `cmd.sourceLayer` в `cmd.dstTexture` и переводит её в `Texture`. Вызывается из @ref execute.
		void handleSaveLayer(Ogre::RenderSystem* rs, const SaveLayerCmd& cmd);

		const CompositorPassRmlDef* mDefinition = nullptr;
		const RmlFrameData* mFrameData = nullptr;

		Ogre::SceneManager* mUiSceneManager = nullptr; ///< Общий 1-поточный SceneManager (не зависит от потоков 3D-сцены).
		Ogre::Camera* mOrthoCamera = nullptr;
		Ogre::v1::Rectangle2D* mFsRect = nullptr;

		Ogre::MaterialPtr mAlphaBlendMat;
		Ogre::MaterialPtr mCopyMat;
		Ogre::MaterialPtr mScissorCopyMat;

		Ogre::ObjectMemoryManager mObjectMemoryManager;
		Ogre::NodeMemoryManager   mNodeMemoryManager;

		std::vector<Ogre::SceneNode> mSceneNodes;
		std::vector<RenderObject, Ogre::STLAllocator<RenderObject, Ogre::AlignAllocPolicy<>>> mRenderObjects;

		Ogre::RenderQueue* mRenderQueue = nullptr; ///< Родная RenderQueue из mUiSceneManager (numWorkerThreads == 1).
		Ogre::VertexArrayObject* mFullscreenVao = nullptr;

		// Пул слоёв и scratch-буферов (выделяются лениво при первой offscreen-команде)
		Ogre::RenderPassDescriptor* mLayer0LoadRpd = nullptr;
		Ogre::uint32                mPoolWidth = 0;
		Ogre::uint32                mPoolHeight = 0;
		std::vector<OffscreenSlot>  mLayerSlots; ///< Индекс `i` соответствует `layerId = i + 1`.
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