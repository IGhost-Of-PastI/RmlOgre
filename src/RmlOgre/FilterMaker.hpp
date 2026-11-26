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

	class Filter
	{
	public:
		virtual ~Filter() = default;

		virtual void appendSteps(std::vector<FilterStep>& out) const = 0;

		/// Возвращает текстуру, которой владеет фильтр (например, SaveLayerAsMaskImage)
		virtual Ogre::TextureGpu* ownedTexture() const noexcept { return nullptr; }
	};

	class SingleMaterialFilter : public Filter
	{
		Ogre::MaterialPtr mMaterial;
		Ogre::TextureGpu* mExtraTexture = nullptr;
		Ogre::TextureGpu* mOwnedTexture = nullptr;

	public:
		explicit SingleMaterialFilter(
			Ogre::MaterialPtr material,
			Ogre::TextureGpu* extraTexture = nullptr,
			Ogre::TextureGpu* ownedTexture = nullptr)
			: mMaterial(std::move(material))
			, mExtraTexture(extraTexture)
			, mOwnedTexture(ownedTexture)
		{
		}

		void appendSteps(std::vector<FilterStep>& out) const override {
			out.push_back(FilterStep{
				.kind = FilterStep::Kind::SingleMaterial,
				.material = mMaterial,
				.extraTexture = mExtraTexture
				});
		}

		Ogre::TextureGpu* ownedTexture() const noexcept override { return mOwnedTexture; }
	};

	class FilterMaker
	{
	public:
		virtual ~FilterMaker() = default;
		virtual std::unique_ptr<Filter> make(const Rml::Dictionary& parameters) = 0;
	};

} // namespace nimble::RmlOgre

#endif // NIMBLE_RMLOGRE_FILTERMAKER_HPP