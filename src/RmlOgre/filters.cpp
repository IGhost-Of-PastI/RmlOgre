// filters.cpp
#include "filters.hpp"

#include <OgreGpuProgramParams.h>
#include <OgreHlmsDatablock.h>
#include <OgreMaterial.h>
#include <OgreMaterialManager.h>
#include <OgreTechnique.h>
#include <OgreTextureUnitState.h>

#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Dictionary.h>

#include <array>

using namespace nimble::RmlOgre;

namespace {

	Ogre::Vector4 to_ogre(Rml::ColourbPremultiplied c)
	{
		Ogre::Vector4 result;
		for (int i = 0; i < 4; ++i)
			result[i] = (1.f / 255.f) * float(c[i]);
		return result;
	}

	Ogre::MaterialPtr clone_filter_material(const Ogre::MaterialPtr& base)
	{
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

void BlurFilter::appendSteps(std::vector<FilterStep>& out) const
{
	out.push_back(FilterStep{
		.kind = FilterStep::Kind::Blur,
		.halfsampleMat = this->halfsample,
		.blurHMat = this->blurH,
		.blurVMat = this->blurV,
		.halfSamples = this->halfSamples
		});
}

BlurFilterMaker::BlurFilterMaker()
{
	auto baseHalf = Ogre::MaterialManager::getSingleton().getByName("Rml/Halfsample");
	this->halfsampleMaterial = clone_filter_material(baseHalf);
	this->blurMaterial = Ogre::MaterialManager::getSingleton().getByName("Rml/Blur");
}

BlurFilter BlurFilterMaker::make(float sigma)
{
	int halfsamples = 0;
	int step = 1;
	while (sigma > MAX_SCALED_SIGMA * step)
	{
		halfsamples += 1;
		step *= 2;
	}
	const float scale = 1.0f / step;

	std::array<float, NUM_WEIGHTS> weights{};
	if (sigma < 0.1f)
	{
		weights[0] = 1.0f;
	}
	else
	{
		float weightsTotal = 0.0f;
		for (int i = 0; i < NUM_WEIGHTS; ++i)
		{
			const int j = i * step;
			weights[i] = Rml::Math::Exp(-float(j * j) / (2.0f * sigma * sigma))
				/ (Rml::Math::SquareRoot(2.0f * Rml::Math::RMLUI_PI) * sigma);
			weightsTotal += (i == 0 ? 1.0f : 2.0f) * weights[i];
		}

		for (float& weight : weights)
			weight /= weightsTotal;
	}

	auto blurH = clone_filter_material(this->blurMaterial);
	{
		auto* pass = blurH->getBestTechnique()->getPass(0);
		pass->getVertexProgramParameters()->setNamedConstant("scale", Ogre::Vector2{ scale, 1.0f });
		auto psParams = pass->getFragmentProgramParameters();
		psParams->setNamedConstant("direction", Ogre::Vector2{ 1.0f, 0.0f });
		psParams->setNamedConstant("weights", weights.data(), weights.size() / 4);
	}

	auto blurV = clone_filter_material(this->blurMaterial);
	{
		auto* pass = blurV->getBestTechnique()->getPass(0);
		pass->getVertexProgramParameters()->setNamedConstant("scale", Ogre::Vector2{ 1.0f, scale });
		auto psParams = pass->getFragmentProgramParameters();
		psParams->setNamedConstant("direction", Ogre::Vector2{ 0.0f, 1.0f });
		psParams->setNamedConstant("weights", weights.data(), weights.size() / 4);
	}

	return BlurFilter(this->halfsampleMaterial, std::move(blurH), std::move(blurV), halfsamples);
}

std::unique_ptr<Filter> BlurFilterMaker::make(const Rml::Dictionary& parameters)
{
	return std::make_unique<BlurFilter>(this->make(Rml::Get(parameters, "sigma", 0.0f)));
}

void DropShadowFilter::appendSteps(std::vector<FilterStep>& out) const
{
	out.push_back(FilterStep{
		.kind = FilterStep::Kind::DropShadow,
		.material = this->shadowMaterial,
		.halfsampleMat = this->blurFilter.halfsampleMaterial(),
		.blurHMat = this->blurFilter.blurHMaterial(),
		.blurVMat = this->blurFilter.blurVMaterial(),
		.halfSamples = this->blurFilter.numHalfSamples()
		});
}

DropShadowFilterMaker::DropShadowFilterMaker()
{
	this->shadowMaterial = Ogre::MaterialManager::getSingleton().getByName("Rml/Shadow");
	this->blurlessShadowMaterial = Ogre::MaterialManager::getSingleton().getByName("Rml/BlurlessShadow");
}

std::unique_ptr<Filter> DropShadowFilterMaker::make(const Rml::Dictionary& parameters)
{
	const float sigma = Rml::Get(parameters, "sigma", 0.0f);
	const auto colour = Rml::Get(parameters, "color", Rml::Colourb()).ToPremultiplied();
	const auto offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.f));

	if (sigma > 0.5f)
	{
		auto shadowMat = clone_filter_material(this->shadowMaterial);
		auto* pass = shadowMat->getBestTechnique()->getPass(0);
		pass->getVertexProgramParameters()->setNamedConstant("offset", Ogre::Vector2{ offset.x, offset.y });
		pass->getFragmentProgramParameters()->setNamedConstant("colour", to_ogre(colour));

		return std::make_unique<DropShadowFilter>(
			this->blurFilterMaker.make(sigma),
			std::move(shadowMat));
	}
	else
	{
		auto shadowMat = clone_filter_material(this->blurlessShadowMaterial);
		auto* pass = shadowMat->getBestTechnique()->getPass(0);
		auto psParams = pass->getFragmentProgramParameters();
		psParams->setNamedConstant("offset", Ogre::Vector2{ offset.x, offset.y });
		psParams->setNamedConstant("colour", to_ogre(colour));

		return std::make_unique<SingleMaterialFilter>(std::move(shadowMat));
	}
}

OpacityFilterMaker::OpacityFilterMaker()
{
	this->baseMaterial = Ogre::MaterialManager::getSingleton().getByName("Rml/Opacity");
}

std::unique_ptr<Filter> OpacityFilterMaker::make(const Rml::Dictionary& parameters)
{
	auto material = clone_filter_material(this->baseMaterial);
	material->getBestTechnique()->getPass(0)->getFragmentProgramParameters()
		->setNamedConstant("value", Rml::Get(parameters, "value", 1.0f));
	return std::make_unique<SingleMaterialFilter>(std::move(material));
}

ColourMatrixFilterMaker::ColourMatrixFilterMaker()
{
	this->baseMaterial = Ogre::MaterialManager::getSingleton().getByName("Rml/ColourMatrix");
}

std::unique_ptr<Filter> ColourMatrixFilterMaker::make(const Ogre::Matrix4& matrix)
{
	auto material = clone_filter_material(this->baseMaterial);
	material->getBestTechnique()->getPass(0)->getFragmentProgramParameters()
		->setNamedConstant("colourMatrix", matrix);
	return std::make_unique<SingleMaterialFilter>(std::move(material));
}

std::unique_ptr<Filter> BrightnessFilterMaker::make(const Rml::Dictionary& parameters)
{
	const float value = Rml::Get(parameters, "value", 1.0f);
	return this->make(Ogre::Matrix4::getScale(value, value, value));
}

std::unique_ptr<Filter> ContrastFilterMaker::make(const Rml::Dictionary& parameters)
{
	const float value = Rml::Get(parameters, "value", 1.0f);
	const float grayness = 0.5f - 0.5f * value;
	auto matrix = Ogre::Matrix4::getScale(value, value, value);
	matrix[0][3] = grayness;
	matrix[1][3] = grayness;
	matrix[2][3] = grayness;
	return this->make(matrix);
}

std::unique_ptr<Filter> InvertFilterMaker::make(const Rml::Dictionary& parameters)
{
	const float value = Rml::Math::Clamp(Rml::Get(parameters, "value", 1.0f), 0.0f, 1.0f);
	const float inverted = 1.0f - 2.0f * value;
	auto matrix = Ogre::Matrix4::getScale(inverted, inverted, inverted);
	matrix[0][3] = value;
	matrix[1][3] = value;
	matrix[2][3] = value;
	return this->make(matrix);
}

std::unique_ptr<Filter> GrayscaleFilterMaker::make(const Rml::Dictionary& parameters)
{
	const float value = Rml::Get(parameters, "value", 1.0f);
	const float invValue = 1.0f - value;
	const auto gray = value * Ogre::Vector3{ 0.2126f, 0.7152f, 0.0722f };
	const Ogre::Matrix4 matrix{
		gray.x + invValue, gray.y,            gray.z,            0.0f,
		gray.x,            gray.y + invValue, gray.z,            0.0f,
		gray.x,            gray.y,            gray.z + invValue, 0.0f,
		0.0f,              0.0f,              0.0f,              1.0f
	};
	return this->make(matrix);
}

std::unique_ptr<Filter> SepiaFilterMaker::make(const Rml::Dictionary& parameters)
{
	const float value = Rml::Get(parameters, "value", 1.0f);
	const float invValue = 1.0f - value;
	const auto rMix = value * Ogre::Vector3{ 0.393f, 0.769f, 0.189f };
	const auto gMix = value * Ogre::Vector3{ 0.349f, 0.686f, 0.168f };
	const auto bMix = value * Ogre::Vector3{ 0.272f, 0.534f, 0.131f };
	const Ogre::Matrix4 matrix{
		rMix.x + invValue, rMix.y,            rMix.z,            0.0f,
		gMix.x,            gMix.y + invValue, gMix.z,            0.0f,
		bMix.x,            bMix.y,            bMix.z + invValue, 0.0f,
		0.0f,              0.0f,              0.0f,              1.0f
	};
	return this->make(matrix);
}

std::unique_ptr<Filter> HueRotateFilterMaker::make(const Rml::Dictionary& parameters)
{
	const float value = Rml::Get(parameters, "value", 1.0f);
	const float s = Ogre::Math::Sin(value);
	const float c = Ogre::Math::Cos(value);
	const Ogre::Matrix4 matrix{
		0.213f + 0.787f * c - 0.213f * s,  0.715f - 0.715f * c - 0.715f * s,  0.072f - 0.072f * c + 0.928f * s,  0.0f,
		0.213f - 0.213f * c + 0.143f * s,  0.715f + 0.285f * c + 0.140f * s,  0.072f - 0.072f * c - 0.283f * s,  0.0f,
		0.213f - 0.213f * c - 0.787f * s,  0.715f - 0.715f * c + 0.715f * s,  0.072f + 0.928f * c + 0.072f * s,  0.0f,
		0.0f,                              0.0f,                              0.0f,                              1.0f
	};
	return this->make(matrix);
}

std::unique_ptr<Filter> SaturateFilterMaker::make(const Rml::Dictionary& parameters)
{
	const float value = Rml::Get(parameters, "value", 1.0f);
	const Ogre::Matrix4 matrix{
		0.213f + 0.787f * value,  0.715f - 0.715f * value,  0.072f - 0.072f * value,  0.0f,
		0.213f - 0.213f * value,  0.715f + 0.285f * value,  0.072f - 0.072f * value,  0.0f,
		0.213f - 0.213f * value,  0.715f - 0.715f * value,  0.072f + 0.928f * value,  0.0f,
		0.0f,                     0.0f,                     0.0f,                     1.0f
	};
	return this->make(matrix);
}

MaskImageFilterMaker::MaskImageFilterMaker()
{
	this->baseMaterial = Ogre::MaterialManager::getSingleton().getByName("Rml/Mask");
}

SingleMaterialFilter MaskImageFilterMaker::make(Ogre::TextureGpu* image, bool takeOwnership)
{
	auto material = clone_filter_material(this->baseMaterial);
	auto* textureUnit = material->getBestTechnique()->getPass(0)->getTextureUnitState("dstTex");
	textureUnit->setTexture(image);
	return SingleMaterialFilter(std::move(material), image, takeOwnership ? image : nullptr);
}

std::unique_ptr<Filter> MaskImageFilterMaker::make(const Rml::Dictionary& parameters)
{
	void* texture = Rml::Get<void*>(parameters, "maskImage", nullptr);
	return std::make_unique<SingleMaterialFilter>(
		this->make(static_cast<Ogre::TextureGpu*>(texture), false));
}