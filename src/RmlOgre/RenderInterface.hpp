#ifndef NIMBLE_RMLOGRE_RENDERINTERFACE_HPP
#define NIMBLE_RMLOGRE_RENDERINTERFACE_HPP

#include "FilterMaker.hpp"
#include "Material.hpp"
#include "ObjectIndex.hpp"
#include "RmlCommandBuffer.hpp"
#include "ShaderMaker.hpp"
#include "filters.hpp"

#include <OgreHlmsDatablock.h>
#include <OgreHlmsSamplerblock.h>
#include <OgreMatrix4.h>
#include <OgreString.h>

#include <RmlUi/Core/RenderInterface.h>

#include <cstdint>
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

	/**
	 * @brief Реализация `Rml::RenderInterface`, записывающая команды кадра в @ref RmlFrameData.
	 *
	 * Класс работает строго как **CPU-продюсер** и не владеет ни `CompositorWorkspace`,
	 * ни целевым рендер-таргетом. Один экземпляр может обслуживать любое количество
	 * `Rml::Context` и `UiSurface` (в том числе из разных `Ogre::SceneManager` и
	 * `Ogre::CompositorWorkspace`). Вся отрисовка на GPU выполняется синхронно
	 * экземплярами @ref CompositorPassRml во время `Ogre::Root::renderOneFrame()`.
	 *
	 * @par Жизненный цикл ресурсов
	 * Геометрия (`VAO`), текстуры и материалы компилируются сразу по запросу RmlUi,
	 * но их уничтожение всегда откладывается до @ref FlushReleases, так как RmlUi
	 * может вызывать `Release*()` прямо во время `Context::Render()`, когда ресурсы
	 * ещё упоминаются в текущем @ref RmlFrameData или используются на GPU.
	 */
	class RenderInterface : public Rml::RenderInterface
	{
		Ogre::String mName;
		Rml::Vector2i mViewportSize{ 1, 1 };

		Ogre::HlmsUnlit* mHlms = nullptr;
		Ogre::HlmsMacroblock mMacroblock;
		Ogre::HlmsBlendblock mBlendblock;
		Ogre::HlmsSamplerblock mSamplerblock;

		ObjectIndex<Material> mMaterials;

		std::unordered_map<Rml::String, std::unique_ptr<FilterMaker>> mFilterMakers;
		MaskImageFilterMaker mMaskImageFilterMaker;
		ObjectIndex<std::unique_ptr<Filter>> mFilters;

		std::unordered_map<Rml::String, std::unique_ptr<ShaderMaker>> mShaderMakers;
		ObjectIndex<Material> mShaders;

		// Текущее состояние записи команд кадра (сбрасывается в BeginFrame)
		ScissorState mCurrentScissor{};
		Ogre::Matrix4 mCurrentTransform = Ogre::Matrix4::IDENTITY;
		bool mStencilEnabled = false;
		Ogre::uint32 mStencilRef = 0;
		std::uint32_t mLayerDepth = 1; ///< Текущая глубина стека слоёв (`1` => активен Layer 0).

		Material mStencilMaterial{};
		RmlFrameData mDefaultFrameData{};
		RmlFrameData* mActiveFrameData = &mDefaultFrameData;

		int mDatablockId = 0;
		int mRenderTextureId = 0;
		std::vector<Rml::CompiledGeometryHandle> mReleaseGeometries;
		std::vector<Rml::TextureHandle> mReleaseTextures;
		std::vector<Ogre::TextureGpu*> mReleaseRenderTextures;

		/**
		 * @brief Уничтожает все VAO, накопленные в `mReleaseGeometries`.
		 * @par Кто вызывает
		 * Исключительно @ref FlushReleases.
		 */
		void releaseBufferedGeometries();

		/**
		 * @brief Уничтожает `HlmsUnlitDatablock` и `TextureGpu` из очередей `mReleaseTextures`
		 *        и `mReleaseRenderTextures`.
		 * @par Кто вызывает
		 * Исключительно @ref FlushReleases (строго после очистки всех @ref RmlFrameData прошлого кадра,
		 * чтобы не осталось живых `Ogre::MaterialPtr`, подписанных на удаляемые текстуры).
		 */
		void releaseBufferedTextures();

		/**
		 * @brief Выделяет `PFG_RGBA8_UNORM` RTT-текстуру под текущий scissor или `mViewportSize`.
		 * @par Кто вызывает
		 * @ref SaveLayerAsTexture и @ref SaveLayerAsMaskImage.
		 * @param[out] outDimensions Итоговые размеры созданной текстуры (>= 1×1).
		 */
		Ogre::TextureGpu* createSaveLayerTexture(Rml::Vector2i& outDimensions);

		/// Возвращает индекс текущего активного слоя (`0` — базовый RT, `1..N` — offscreen-слои).
		std::uint32_t activeLayer() const noexcept { return mLayerDepth > 0 ? mLayerDepth - 1 : 0; }

	public:
		/**
		 * @brief Создаёт интерфейс рендера, регистрирует базовые `HlmsUnlit`-материалы,
		 *        стандартные фабрики фильтров (10 шт.) и градиентов (3 шт.).
		 * @par Когда вызывается
		 * Кодом приложения **после** инициализации `Ogre::Root`, регистрации `HlmsUnlit`
		 * и загрузки ресурсных групп с материалами `Rml/*`.
		 * @param name Префикс для уникальных имён создаваемых текстур и datablock'ов.
		 */
		explicit RenderInterface(Ogre::String name);

		/**
		 * @brief Конструктор совместимости со старой сигнатурой.
		 *
		 * Игнорирует `sceneManager` и `background`; если передан `output`, инициализирует
		 * по его габаритам @ref mViewportSize.
		 */
		RenderInterface(
			const Ogre::String& name,
			Ogre::SceneManager* /*sceneManager*/,
			Ogre::TextureGpu* output,
			Ogre::TextureGpu* /*background*/ = nullptr);

		/// Имя экземпляра интерфейса (используется как префикс GPU-ресурсов).
		const Ogre::String& getName() const noexcept { return mName; }

		/// Ссылка на встроенный буфер (для сценариев с единственной поверхностью / тестов).
		const RmlFrameData& frameData() const noexcept { return mDefaultFrameData; }

		/**
		 * @brief Задаёт размеры целевого вьюпорта в пикселях (используется в @ref createSaveLayerTexture при отключённом scissor).
		 * @par Кто вызывает
		 * Код приложения при создании поверхности и при каждом её ресайзе (в одноповерхностном режиме).
		 */
		void SetViewportSize(Rml::Vector2i size) noexcept { mViewportSize = size; }

		/// Текущие размеры вьюпорта, заданные через @ref SetViewportSize или @ref BeginFrame(RmlFrameData&, Rml::Vector2i).
		Rml::Vector2i GetViewportSize() const noexcept { return mViewportSize; }

		/**
		 * @brief Регистрирует пользовательскую фабрику CSS-фильтра по имени.
		 * @par Когда вызывается
		 * При инициализации до загрузки RML-документов.
		 */
		void AddFilterMaker(Rml::String name, std::unique_ptr<FilterMaker> filterMaker);

		/**
		 * @brief Регистрирует пользовательскую фабрику шейдера (градиента) по имени.
		 * @par Когда вызывается
		 * При инициализации до загрузки RML-документов.
		 */
		void AddShaderMaker(Rml::String name, std::unique_ptr<ShaderMaker> shaderMaker);

		/**
		 * @brief Уничтожает отложенные VAO, текстуры и datablock'и, накопленные через `Release*()`.
		 * @par Когда вызывается
		 * Один раз за кадр до записи команд поверхностей, а также один раз после `Rml::Shutdown()`.
		 * @pre Все устаревшие @ref RmlFrameData прошлого кадра должны быть предварительно очищены
		 *      (`frameData.clear()`), чтобы не удерживать `Ogre::MaterialPtr` с удаляемыми текстурами.
		 */
		void FlushReleases();

		/**
		 * @brief Начинает запись команд конкретной поверхности в переданный буфер `targetFrame`.
		 * @par Кто вызывает
		 * Код приложения перед `Rml::Context::Render()` каждой поверхности в мульти-контекстном режиме.
		 * @note Не вызывает @ref FlushReleases — в мульти-поверхностном режиме его вызывают отдельно один раз на кадр.
		 */
		void BeginFrame(RmlFrameData& targetFrame, Rml::Vector2i viewportSize);

		/**
		 * @brief Перегрузка для одноповерхностного режима и тестов.
		 *
		 * Очищает `mDefaultFrameData`, вызывает @ref FlushReleases и перенаправляет в
		 * @ref BeginFrame(RmlFrameData&, Rml::Vector2i).
		 */
		void BeginFrame();

		/**
		 * @brief Завершает запись текущего кадра поверхности (сбрасывает `mActiveFrameData = nullptr`).
		 * @par Кто вызывает
		 * Код приложения сразу после `Rml::Context::Render()`.
		 */
		void EndFrame();

		/** @name Методы управления геометрией (вызываются RmlUi) */
		///@{

		/**
		 * @brief Создаёт Ogre VAO из массива вершин и индексов RmlUi.
		 * @par Кто вызывает
		 * RmlUi при компиляции элементов (во время `Context::Update()` или `Context::Render()`).
		 */
		Rml::CompiledGeometryHandle CompileGeometry(
			Rml::Span<const Rml::Vertex> vertices,
			Rml::Span<const int> indices
		) override;

		/**
		 * @brief Записывает @ref DrawCmd с текущим scissor/transform/stencil в активный @ref RmlFrameData.
		 * @par Кто вызывает
		 * Исключительно RmlUi внутри `Rml::Context::Render()`.
		 */
		void RenderGeometry(
			Rml::CompiledGeometryHandle geometry,
			Rml::Vector2f translation,
			Rml::TextureHandle texture
		) override;

		/**
		 * @brief Ставит VAO в очередь отложенного удаления (фактическое удаление в @ref FlushReleases).
		 * @par Кто вызывает
		 * RmlUi при перестройке документа, скрытии элементов или `Rml::Shutdown()`.
		 */
		void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
		///@}

		/** @name Методы управления текстурами (вызываются RmlUi) */
		///@{

		/**
		 * @brief Загружает текстуру через `Ogre::TextureGpuManager` с premultiply alpha и создаёт `HlmsUnlitDatablock`.
		 * @par Кто вызывает
		 * RmlUi при загрузке изображений из RML/RCSS.
		 */
		Rml::TextureHandle LoadTexture(
			Rml::Vector2i& texture_dimensions,
			const Rml::String& source
		) override;

		/**
		 * @brief Создаёт `PFG_RGBA8_UNORM` текстуру из сырого буфера в памяти (например, атлас шрифта или SVG/Lottie).
		 * @par Кто вызывает
		 * RmlUi при генерации глифов шрифтов и процедурных текстур.
		 */
		Rml::TextureHandle GenerateTexture(
			Rml::Span<const Rml::byte> source,
			Rml::Vector2i source_dimensions
		) override;

		/**
		 * @brief Ставит текстуру и её datablock в очередь отложенного удаления (фактическое удаление в @ref FlushReleases).
		 * @par Кто вызывает
		 * RmlUi при выгрузке текстур или `Rml::Shutdown()`.
		 */
		void ReleaseTexture(Rml::TextureHandle texture) override;
		///@}

		/** @name Состояние пайплайна (вызываются RmlUi внутри Context::Render()) */
		///@{

		/// Включает или отключает scissor-отсечение для последующих команд.
		void EnableScissorRegion(bool enable) override;

		/// Устанавливает прямоугольник отсечения в пикселях для последующих команд.
		void SetScissorRegion(Rml::Rectanglei region) override;

		/**
		 * @brief Устанавливает матрицу CSS-трансформации (`nullptr` сбрасывает в единичную).
		 * @note Транспонирует column-major матрицу RmlUi в row-major формат `Ogre::Matrix4`.
		 */
		void SetTransform(const Rml::Matrix4f* transform) override;

		/// Включает или отключает проверку буфера трафарета (`StencilMode::TestEqual`) для последующих команд.
		void EnableClipMask(bool enable) override;

		/**
		 * @brief Записывает команды формирования трафаретной маски (`ClearStencilCmd` и/или `DrawCmd` со `mStencilMaterial`).
		 * @par Кто вызывает
		 * Исключительно RmlUi внутри `Rml::Context::Render()` для `overflow: hidden` / `border-radius` / `clip`.
		 */
		void RenderToClipMask(
			Rml::ClipMaskOperation operation,
			Rml::CompiledGeometryHandle geometry,
			Rml::Vector2f translation
		) override;
		///@}

		/** @name Слои, фильтры и сохранение слоёв (вызываются RmlUi) */
		///@{

		/**
		 * @brief Увеличивает стек слоёв и записывает @ref PushLayerCmd.
		 * @par Кто вызывает
		 * Исключительно RmlUi внутри `Rml::Context::Render()` при встрече элемента с `filter`, `mask-image` или `opacity < 1` на контейнере.
		 * @return Идентификатор нового слоя (`1..N`).
		 */
		Rml::LayerHandle PushLayer() override;

		/**
		 * @brief Разворачивает указанные фильтры в массив @ref FilterStep и записывает @ref CompositeLayersCmd.
		 * @par Кто вызывает
		 * Исключительно RmlUi внутри `Rml::Context::Render()` перед закрытием слоя.
		 */
		void CompositeLayers(
			Rml::LayerHandle source,
			Rml::LayerHandle destination,
			Rml::BlendMode blend_mode,
			Rml::Span<const Rml::CompiledFilterHandle> filters
		) override;

		/**
		 * @brief Снимает верхний слой со стека и записывает @ref PopLayerCmd.
		 * @par Кто вызывает
		 * Исключительно RmlUi внутри `Rml::Context::Render()` парно с @ref PushLayer.
		 */
		void PopLayer() override;

		/**
		 * @brief Компилирует CSS-фильтр через соответствующий @ref FilterMaker.
		 * @par Кто вызывает
		 * RmlUi при изменении стилей или во время `Context::Render()`.
		 */
		Rml::CompiledFilterHandle CompileFilter(
			const Rml::String& name,
			const Rml::Dictionary& parameters
		) override;

		/**
		 * @brief Освобождает скомпилированный фильтр; если фильтр владел текстурой маски, ставит её в `mReleaseRenderTextures`.
		 * @par Кто вызывает
		 * RmlUi при уничтожении элемента/стиля или завершении `Context::Render()` (для временных масок).
		 */
		void ReleaseFilter(Rml::CompiledFilterHandle filter) override;

		/**
		 * @brief Выделяет RTT-текстуру, регистрирует её как `Rml::TextureHandle` и записывает @ref SaveLayerCmd.
		 * @par Кто вызывает
		 * Исключительно RmlUi внутри `Rml::Context::Render()` (например, `backdrop-filter` / `render-to-texture`).
		 */
		Rml::TextureHandle SaveLayerAsTexture() override;

		/**
		 * @brief Выделяет RTT-текстуру, записывает @ref SaveLayerCmd и возвращает `Rml/Mask`-фильтр, владеющий этой текстурой.
		 * @par Кто вызывает
		 * Исключительно RmlUi внутри `Rml::Context::Render()` при вычислении `mask-image`.
		 */
		Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;
		///@}

		/** @name Градиентные шейдеры (вызываются RmlUi) */
		///@{

		/**
		 * @brief Компилирует low-level материал градиента (`linear-gradient`, `radial-gradient`, `conic-gradient`).
		 * @par Кто вызывает
		 * RmlUi при компиляции декораторов градиентов.
		 */
		Rml::CompiledShaderHandle CompileShader(
			const Rml::String& name,
			const Rml::Dictionary& parameters
		) override;

		/**
		 * @brief Записывает @ref DrawCmd с материалом градиента в активный @ref RmlFrameData.
		 * @par Кто вызывает
		 * Исключительно RmlUi внутри `Rml::Context::Render()`.
		 */
		void RenderShader(
			Rml::CompiledShaderHandle shader,
			Rml::CompiledGeometryHandle geometry,
			Rml::Vector2f translation,
			Rml::TextureHandle texture
		) override;

		/**
		 * @brief Удаляет запись скомпилированного шейдера из `mShaders`.
		 * @par Кто вызывает
		 * RmlUi при уничтожении декоратора или `Rml::Shutdown()`.
		 */
		void ReleaseShader(Rml::CompiledShaderHandle shader) override;
		///@}
	};

} // namespace nimble::RmlOgre

#endif // NIMBLE_RMLOGRE_RENDERINTERFACE_HPP