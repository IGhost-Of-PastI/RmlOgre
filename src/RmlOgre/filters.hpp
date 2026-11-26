// filters.hpp
#ifndef NIMBLE_RMLOGRE_FILTERS_HPP
#define NIMBLE_RMLOGRE_FILTERS_HPP

#include "FilterMaker.hpp"

#include <OgreMatrix4.h>

namespace nimble::RmlOgre {

	/**
	 * @brief Многопроходный фильтр гауссова размытия (`blur(sigma)`).
	 *
	 * Хранит предрассчитанные материалы `Rml/Halfsample`, горизонтального и вертикального
	 * `Rml/Blur`. Исполняется внутри @ref CompositorPassRml::applyBlurPass.
	 */
	class BlurFilter : public Filter
	{
		Ogre::MaterialPtr halfsample;
		Ogre::MaterialPtr blurH;
		Ogre::MaterialPtr blurV;
		int halfSamples = 0;

	public:
		BlurFilter(
			Ogre::MaterialPtr halfsample,
			Ogre::MaterialPtr blurH,
			Ogre::MaterialPtr blurV,
			int halfSamples)
			: halfsample(std::move(halfsample))
			, blurH(std::move(blurH))
			, blurV(std::move(blurV))
			, halfSamples(halfSamples)
		{
		}

		const Ogre::MaterialPtr& halfsampleMaterial() const noexcept { return halfsample; }
		const Ogre::MaterialPtr& blurHMaterial() const noexcept { return blurH; }
		const Ogre::MaterialPtr& blurVMaterial() const noexcept { return blurV; }
		int numHalfSamples() const noexcept { return halfSamples; }

		/// Добавляет шаг `FilterStep::Kind::Blur` в команду композитинга.
		void appendSteps(std::vector<FilterStep>& out) const override;
	};

	/**
	 * @brief Фабрика фильтра `blur` (также используется внутри @ref DropShadowFilterMaker).
	 *
	 * Вычисляет число шагов даунсэмплинга (`halfSamples`) и 8 гауссовых весов
	 * по параметру `sigma` из словаря RmlUi.
	 */
	class BlurFilterMaker : public FilterMaker
	{
		Ogre::MaterialPtr halfsampleMaterial;
		Ogre::MaterialPtr blurMaterial;

	public:
		static constexpr int NUM_WEIGHTS = 8;          ///< Должно совпадать с `NUM_WEIGHTS` в `Blur_ps.glsl`.
		static constexpr float MAX_SCALED_SIGMA = 3.0f;///< Порог `sigma`, выше которого добавляется шаг 2x `halfsample`.

		BlurFilterMaker();
		BlurFilter make(float sigma);
		std::unique_ptr<Filter> make(const Rml::Dictionary& parameters) override;
	};

	/**
	 * @brief Фильтр размытой тени (`drop-shadow` при `sigma > 0.5`).
	 *
	 * Исполняется внутри @ref CompositorPassRml::handleCompositeLayers: копирует исходник в
	 * `mScratchShadow`, сдвигает/окрашивает тень материалом `Rml/Shadow`, размывает её через
	 * @ref CompositorPassRml::applyBlurPass и накладывает исходник поверх тени.
	 */
	class DropShadowFilter : public Filter
	{
		BlurFilter blurFilter;
		Ogre::MaterialPtr shadowMaterial;

	public:
		DropShadowFilter(
			BlurFilter blurFilter,
			Ogre::MaterialPtr shadowMaterial)
			: blurFilter(std::move(blurFilter))
			, shadowMaterial(std::move(shadowMaterial))
		{
		}

		/// Добавляет шаг `FilterStep::Kind::DropShadow` в команду композитинга.
		void appendSteps(std::vector<FilterStep>& out) const override;
	};

	/**
	 * @brief Фабрика фильтра `drop-shadow`.
	 *
	 * При `sigma > 0.5` создаёт @ref DropShadowFilter, иначе — однопроходный
	 * @ref SingleMaterialFilter на базе `Rml/BlurlessShadow`.
	 */
	class DropShadowFilterMaker : public FilterMaker
	{
		BlurFilterMaker blurFilterMaker;
		Ogre::MaterialPtr shadowMaterial;
		Ogre::MaterialPtr blurlessShadowMaterial;

	public:
		DropShadowFilterMaker();
		std::unique_ptr<Filter> make(const Rml::Dictionary& parameters) override;
	};

	/// Фабрика фильтра `opacity` (материал `Rml/Opacity`).
	class OpacityFilterMaker : public FilterMaker
	{
		Ogre::MaterialPtr baseMaterial;

	public:
		OpacityFilterMaker();
		std::unique_ptr<Filter> make(const Rml::Dictionary& parameters) override;
	};

	/// Базовая фабрика для всех цветовых матричных фильтров (материал `Rml/ColourMatrix`).
	class ColourMatrixFilterMaker : public FilterMaker
	{
		Ogre::MaterialPtr baseMaterial;

	public:
		ColourMatrixFilterMaker();
		using FilterMaker::make;
		std::unique_ptr<Filter> make(const Ogre::Matrix4& matrix);
	};

	/// Фабрика фильтра `brightness(value)`.
	class BrightnessFilterMaker : public ColourMatrixFilterMaker
	{
	public:
		using ColourMatrixFilterMaker::make;
		std::unique_ptr<Filter> make(const Rml::Dictionary& parameters) override;
	};

	/// Фабрика фильтра `contrast(value)`.
	class ContrastFilterMaker : public ColourMatrixFilterMaker
	{
	public:
		using ColourMatrixFilterMaker::make;
		std::unique_ptr<Filter> make(const Rml::Dictionary& parameters) override;
	};

	/// Фабрика фильтра `invert(value)`.
	class InvertFilterMaker : public ColourMatrixFilterMaker
	{
	public:
		using ColourMatrixFilterMaker::make;
		std::unique_ptr<Filter> make(const Rml::Dictionary& parameters) override;
	};

	/// Фабрика фильтра `grayscale(value)`.
	class GrayscaleFilterMaker : public ColourMatrixFilterMaker
	{
	public:
		using ColourMatrixFilterMaker::make;
		std::unique_ptr<Filter> make(const Rml::Dictionary& parameters) override;
	};

	/// Фабрика фильтра `sepia(value)`.
	class SepiaFilterMaker : public ColourMatrixFilterMaker
	{
	public:
		using ColourMatrixFilterMaker::make;
		std::unique_ptr<Filter> make(const Rml::Dictionary& parameters) override;
	};

	/// Фабрика фильтра `hue-rotate(angle)`.
	class HueRotateFilterMaker : public ColourMatrixFilterMaker
	{
	public:
		using ColourMatrixFilterMaker::make;
		std::unique_ptr<Filter> make(const Rml::Dictionary& parameters) override;
	};

	/// Фабрика фильтра `saturate(value)`.
	class SaturateFilterMaker : public ColourMatrixFilterMaker
	{
	public:
		using ColourMatrixFilterMaker::make;
		std::unique_ptr<Filter> make(const Rml::Dictionary& parameters) override;
	};

	/**
	 * @brief Фабрика фильтра наложения маски `Rml/Mask`.
	 *
	 * @par Кто вызывает
	 * @ref RenderInterface::SaveLayerAsMaskImage с `takeOwnership = true` (текстура маски
	 * будет уничтожена при @ref RenderInterface::ReleaseFilter).
	 */
	class MaskImageFilterMaker : public FilterMaker
	{
		Ogre::MaterialPtr baseMaterial;

	public:
		MaskImageFilterMaker();

		SingleMaterialFilter make(Ogre::TextureGpu* image, bool takeOwnership = false);
		std::unique_ptr<Filter> make(const Rml::Dictionary& parameters) override;
	};

} // namespace nimble::RmlOgre

#endif // NIMBLE_RMLOGRE_FILTERS_HPP