#pragma once

#include "Material.hpp"

#include <OgreMaterial.h>
#include <OgreMatrix4.h>
#include <OgrePrerequisites.h>
#include <RmlUi/Core.h>

#include <cstdint>
#include <variant>
#include <vector>

namespace Ogre {
	struct VertexArrayObject;
	class TextureGpu;
}

namespace nimble::RmlOgre {

	/**
	 * @brief Состояние прямоугольника отсечения (scissor) для команды рендера.
	 *
	 * Заполняется методами @ref RenderInterface::EnableScissorRegion и
	 * @ref RenderInterface::SetScissorRegion, преобразуется в нормализованные
	 * координаты `[0..1]` внутри @ref CompositorPassRml::execute.
	 */
	struct ScissorState {
		bool            enabled = false; ///< Включён ли scissor-тест для команды.
		Rml::Rectanglei region{};        ///< Прямоугольник отсечения в пикселях целевого RT.

		/// Сравнивает состояния; при `enabled == false` у обоих операндов `region` игнорируется.
		bool operator==(const ScissorState& rhs) const noexcept {
			if (!enabled && !rhs.enabled)
				return true;
			return enabled == rhs.enabled && region == rhs.region;
		}
	};

	/**
	 * @brief Режим работы буфера трафарета (stencil) при отрисовке батча.
	 *
	 * Конвертируется в `Ogre::StencilParams` перед вызовом `RenderSystem::setStencilBufferParams`
	 * внутри @ref CompositorPassRml::flushBatch.
	 */
	enum class StencilMode : std::uint8_t {
		Disabled,        ///< Stencil-тест и запись отключены (обычная отрисовка).
		TestEqual,       ///< Отрисовка цвета только там, где `stencil == stencilRef`.
		WriteSet,        ///< Запись маски (`ClipMaskOperation::Set`): `ALWAYS_PASS`, `REPLACE` с `ref = 1`, запись цвета отключена.
		WriteSetInverse, ///< Инверсная запись маски (`SetInverse`): `ALWAYS_PASS`, `REPLACE` с `ref = 0`, запись цвета отключена.
		WriteIntersect   ///< Пересечение масок (`Intersect`): `ALWAYS_PASS`, `INCREMENT`, запись цвета отключена.
	};

	/**
	 * @brief Команда полной очистки буфера трафарета заданным значением.
	 *
	 * @par Кто создаёт
	 * Исключительно @ref RenderInterface::RenderToClipMask при операциях
	 * `Rml::ClipMaskOperation::Set` (`value = 0`) и `SetInverse` (`value = 1`).
	 *
	 * @par Кто исполняет
	 * @ref CompositorPassRml::executeClearStencil с помощью полноэкранного треугольника
	 * в NDC, чтобы не разрывать активный Vulkan `VkRenderPass`.
	 */
	struct ClearStencilCmd {
		std::uint32_t value = 0; ///< Значение, которым заполняется весь stencil-буфер (`0` или `1`).
	};

	/**
	 * @brief Команда отрисовки одной скомпилированной геометрии RmlUi.
	 *
	 * @par Кто создаёт
	 * @ref RenderInterface::RenderGeometry, @ref RenderInterface::RenderShader и
	 * @ref RenderInterface::RenderToClipMask.
	 *
	 * @par Кто исполняет
	 * @ref CompositorPassRml::execute (соседние команды склеиваются в один батч
	 * через @ref can_batch_draws и отправляются в @ref CompositorPassRml::flushBatch).
	 */
	struct DrawCmd {
		Ogre::VertexArrayObject* vao = nullptr;                      ///< VAO из @ref RenderInterface::CompileGeometry.
		Material                 material{};                         ///< Копия материала по значению (защита от реаллокации `ObjectIndex`).
		Ogre::Matrix4            transform = Ogre::Matrix4::IDENTITY;///< CSS-матрица трансформации (в Ogre row-major после `.transpose()`).
		Rml::Vector2f            translation{ 0.0f, 0.0f };          ///< 2D-смещение элемента в пикселях (кладётся в локальный `SceneNode`).
		ScissorState             scissor{};                          ///< Состояние scissor-отсечения.
		StencilMode              stencilMode = StencilMode::Disabled;///< Режим stencil-теста/записи.
		std::uint32_t            stencilRef = 0;                     ///< Опорное значение stencil для сравнения или записи.
	};

	/**
	 * @brief Команда переключения рендера на новый прозрачный offscreen-слой.
	 *
	 * @par Кто создаёт
	 * Исключительно @ref RenderInterface::PushLayer.
	 *
	 * @par Кто исполняет
	 * @ref CompositorPassRml::handlePushLayer (лениво выделяет `PFG_RGBA16_FLOAT` слой
	 * и очищает его в `(0, 0, 0, 0)` с сохранением общего stencil-буфера).
	 */
	struct PushLayerCmd {
		std::uint32_t layerId = 0; ///< Индекс слоя (`1..N`; `0` зарезервирован за базовым RT пасса).
	};

	/**
	 * @brief Команда возврата рендера на предыдущий слой стека.
	 *
	 * @par Кто создаёт
	 * Исключительно @ref RenderInterface::PopLayer.
	 *
	 * @par Кто исполняет
	 * @ref CompositorPassRml::handlePopLayer.
	 */
	struct PopLayerCmd {
		std::uint32_t restoreLayerId = 0; ///< Индекс слоя, становящегося активным после снятия верхнего.
	};

	/**
	 * @brief Описание одного шага постобработки внутри @ref CompositeLayersCmd.
	 *
	 * Формируется методом @ref Filter::appendSteps в момент вызова
	 * @ref RenderInterface::CompositeLayers и исполняется внутри
	 * @ref CompositorPassRml::handleCompositeLayers.
	 */
	struct FilterStep {
		/// Тип пайплайна постобработки.
		enum class Kind : std::uint8_t {
			SingleMaterial, ///< Один полноэкранный проход материалом @ref material (`opacity`, `colour-matrix`, `mask`, `blurless-shadow`).
			Blur,           ///< Многопроходный `halfsample` + разделимый `blurH` / `blurV` (@ref CompositorPassRml::applyBlurPass).
			DropShadow      ///< Сохранение исходника в `mScratchShadow` -> `Rml/Shadow` -> `Blur` -> наложение исходника поверх тени.
		};

		Kind              kind = Kind::SingleMaterial; ///< Алгоритм исполнения шага.
		Ogre::MaterialPtr material;                    ///< Основной материал (`SingleMaterial`, `Rml/Shadow` или `Rml/BlurlessShadow`).
		Ogre::MaterialPtr halfsampleMat;               ///< Материал 2x даунсэмплинга (`Rml/Halfsample`) для `Blur`/`DropShadow`.
		Ogre::MaterialPtr blurHMat;                    ///< Горизонтальный проход `Rml/Blur` с весами и `scale`.
		Ogre::MaterialPtr blurVMat;                    ///< Вертикальный проход `Rml/Blur` с весами и `scale`.
		int               halfSamples = 0;             ///< Количество проходов `halfsampleMat` перед размытием.
		Ogre::TextureGpu* extraTexture = nullptr;      ///< Вторая входная текстура (например, `dstTex` в `Rml/Mask`), требующая layout-барьера.
	};

	/**
	 * @brief Команда применения цепочки фильтров и композитинга слоя `sourceLayer` в `destinationLayer`.
	 *
	 * @par Кто создаёт
	 * Исключительно @ref RenderInterface::CompositeLayers.
	 *
	 * @par Кто исполняет
	 * @ref CompositorPassRml::handleCompositeLayers.
	 */
	struct CompositeLayersCmd {
		std::uint32_t           sourceLayer = 0;                       ///< Исходный слой.
		std::uint32_t           destinationLayer = 0;                  ///< Целевой слой назначения.
		std::uint32_t           restoreLayer = 0;                      ///< Активный слой стека, на который нужно вернуться после композитинга.
		Rml::BlendMode          blendMode = Rml::BlendMode::Blend;     ///< Режим смешивания (`Blend` -> `Rml/AlphaBlend`, `Replace` -> `Ogre/Copy/4xFP32`).
		std::vector<FilterStep> filters;                               ///< Упорядоченная цепочка фильтров, применяемая к `sourceLayer` до композитинга.
		ScissorState            scissor{};                             ///< Область отсечения для проходов фильтров и финального композитинга.
		bool                    stencilEnabled = false;                ///< Применять ли stencil-тест при финальном композитинге.
		std::uint32_t           stencilRef = 0;                        ///< Опорное значение stencil для финального композитинга.
	};

	/**
	 * @brief Команда копирования прямоугольника текущего слоя во внешнюю RTT-текстуру.
	 *
	 * @par Кто создаёт
	 * @ref RenderInterface::SaveLayerAsTexture и @ref RenderInterface::SaveLayerAsMaskImage.
	 *
	 * @par Кто исполняет
	 * @ref CompositorPassRml::handleSaveLayer через `Rml/ScissorCopy`, после чего сразу переводит
	 * @ref dstTexture в `ResourceLayout::Texture` для чтения в том же кадре.
	 */
	struct SaveLayerCmd {
		std::uint32_t     sourceLayer = 0;      ///< Активный слой, из которого читаются пиксели.
		Ogre::TextureGpu* dstTexture = nullptr; ///< Целевая текстура (создана в @ref RenderInterface::createSaveLayerTexture).
		ScissorState      scissor{};            ///< Область копирования (используется в `Rml/ScissorQuad_vs`).
	};

	/// Объединённый тип команды кадра UI.
	using RmlCommand = std::variant<
		ClearStencilCmd,
		DrawCmd,
		PushLayerCmd,
		PopLayerCmd,
		CompositeLayersCmd,
		SaveLayerCmd>;

	/**
	 * @brief Иммутабельный на время GPU-пасса снимок команд одного кадра UI.
	 *
	 * Заполняется в @ref RenderInterface (между `BeginFrame()` и `EndFrame()`) и
	 * читается синхронно в @ref CompositorPassRml::execute внутри `renderOneFrame()`.
	 */
	struct RmlFrameData {
		Material                       stencilMaterial{};   ///< Служебный `HlmsUnlit` материал с `mBlendChannelMask = 0` для записи маски.
		std::vector<RmlCommand>        commands;            ///< Линейный поток команд кадра в порядке отрисовки.
		std::vector<Ogre::TextureGpu*> textureDependencies; ///< Динамические RTT-зависимости (от `SaveLayerAsTexture`) для `BarrierSolver`.

		/// Очищает команды и зависимости кадра, сохраняя @ref stencilMaterial и выделенную `capacity()`.
		void clear() noexcept {
			commands.clear();
			textureDependencies.clear();
		}
	};

	/**
	 * @brief Проверяет, можно ли склеить две соседние команды @ref DrawCmd в один батч `RenderQueue`.
	 *
	 * @par Кто вызывает
	 * Исключительно @ref CompositorPassRml::execute при нарезке батчей.
	 *
	 * @note 2D-смещение `DrawCmd::translation` и `DrawCmd::material` **не** разрывают батч
	 * (учитываются на `SceneNode` и внутри `RenderQueue`). Смена `transform`, `scissor`,
	 * `stencilMode` или `stencilRef` завершает текущий батч.
	 */
	inline bool can_batch_draws(const DrawCmd& a, const DrawCmd& b) noexcept {
		return a.stencilMode == b.stencilMode
			&& a.stencilRef == b.stencilRef
			&& a.scissor == b.scissor
			&& a.transform == b.transform;
	}

} // namespace nimble::RmlOgre