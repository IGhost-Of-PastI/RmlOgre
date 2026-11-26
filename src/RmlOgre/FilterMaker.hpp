// FilterMaker.hpp
#ifndef NIMBLE_RMLOGRE_FILTERMAKER_HPP
#define NIMBLE_RMLOGRE_FILTERMAKER_HPP

#include "RmlCommandBuffer.hpp"

#include <OgreMaterial.h>
#include <RmlUi/Core/Dictionary.h>

#include <memory>
#include <vector>

namespace Ogre {
	class TextureGpu;
}

namespace nimble::RmlOgre {

	/**
	 * @brief Скомпилированный CSS-фильтр, способный развернуть себя в шаги @ref FilterStep.
	 *
	 * Создаётся фабриками @ref FilterMaker внутри @ref RenderInterface::CompileFilter
	 * (или @ref RenderInterface::SaveLayerAsMaskImage) и хранится в `RenderInterface::mFilters`
	 * до вызова @ref RenderInterface::ReleaseFilter.
	 */
	class Filter
	{
	public:
		virtual ~Filter() = default;

		/**
		 * @brief Добавляет один или несколько шагов исполнения фильтра в команду @ref CompositeLayersCmd::filters.
		 * @par Кто вызывает
		 * Исключительно @ref RenderInterface::CompositeLayers во время `Rml::Context::Render()`.
		 */
		virtual void appendSteps(std::vector<FilterStep>& out) const = 0;

		/**
		 * @brief Возвращает RTT-текстуру, которой владеет фильтр, для отложенного удаления.
		 * @par Кто вызывает
		 * Исключительно @ref RenderInterface::ReleaseFilter.
		 * @return Указатель на текстуру (непустой только для масок из `SaveLayerAsMaskImage`) или `nullptr`.
		 */
		virtual Ogre::TextureGpu* ownedTexture() const noexcept { return nullptr; }
	};

	/**
	 * @brief Фильтр, исполняемый одним полноэкранным low-level материалом (`opacity`, цветовые матрицы, `mask`, `blurless-shadow`).
	 */
	class SingleMaterialFilter : public Filter
	{
		Ogre::MaterialPtr mMaterial;
		Ogre::TextureGpu* mExtraTexture = nullptr;
		Ogre::TextureGpu* mOwnedTexture = nullptr;

	public:
		/**
		 * @param material     Клонированный материал с выставленными uniform-параметрами.
		 * @param extraTexture Дополнительная входная текстура (например, `dstTex` в `Rml/Mask`), для которой нужен layout-барьер.
		 * @param ownedTexture Текстура, подлежащая удалению при @ref RenderInterface::ReleaseFilter (если фильтр ей владеет).
		 */
		explicit SingleMaterialFilter(
			Ogre::MaterialPtr material,
			Ogre::TextureGpu* extraTexture = nullptr,
			Ogre::TextureGpu* ownedTexture = nullptr)
			: mMaterial(std::move(material))
			, mExtraTexture(extraTexture)
			, mOwnedTexture(ownedTexture)
		{
		}

		/// Добавляет один @ref FilterStep с типом `FilterStep::Kind::SingleMaterial`.
		void appendSteps(std::vector<FilterStep>& out) const override {
			out.push_back(FilterStep{
				.kind = FilterStep::Kind::SingleMaterial,
				.material = mMaterial,
				.extraTexture = mExtraTexture
				});
		}

		Ogre::TextureGpu* ownedTexture() const noexcept override { return mOwnedTexture; }
	};

	/**
	 * @brief Базовый интерфейс фабрики CSS-фильтров.
	 *
	 * Регистрируется в @ref RenderInterface::AddFilterMaker и вызывается из
	 * @ref RenderInterface::CompileFilter при встрече одноимённого фильтра в RCSS.
	 */
	class FilterMaker
	{
	public:
		virtual ~FilterMaker() = default;

		/**
		 * @brief Создаёт экземпляр @ref Filter по словарю параметров от RmlUi.
		 * @par Кто вызывает
		 * Исключительно @ref RenderInterface::CompileFilter.
		 */
		virtual std::unique_ptr<Filter> make(const Rml::Dictionary& parameters) = 0;
	};

} // namespace nimble::RmlOgre

#endif // NIMBLE_RMLOGRE_FILTERMAKER_HPP