// Material.hpp
#ifndef NIMBLE_RMLOGRE_MATERIAL_HPP
#define NIMBLE_RMLOGRE_MATERIAL_HPP

#include "RenderObject.hpp"

#include <OgreMaterial.h>

namespace Ogre {

	class HlmsDatablock;

}

namespace nimble::RmlOgre {

	/**
	 * @brief Подготовленный материал для быстрой привязки к @ref RenderObject без повторного хэширования HLMS.
	 *
	 * Используется для двух видов рендера:
	 * - **HLMS Unlit** (`material == nullptr`, заполнен `datablock`) — обычная геометрия, текстуры и stencil;
	 * - **Low-level `.material`** (заполнен `material`, `datablock` извлекается из `Pass(0)`) — градиенты (`RenderShader`).
	 *
	 * Копируется по значению внутрь @ref DrawCmd, чтобы избежать висячих указателей при росте @ref ObjectIndex.
	 */
	struct Material
	{
		Ogre::MaterialPtr    material;                    ///< Low-level материал (пустой для `HlmsUnlit`).
		Ogre::HlmsDatablock* datablock = nullptr;         ///< Активный datablock, привязываемый к `RenderObject`.
		Ogre::TextureGpu* textureDependency = nullptr; ///< Динамическая RTT-текстура (от `SaveLayerAsTexture`), регистрируемая в `RmlFrameData::textureDependencies`.
		Ogre::uint32         hash = 0;                    ///< Предрассчитанный HLMS-хэш для `RenderQueue`.
		Ogre::uint32         casterHash = 0;              ///< Предрассчитанный caster HLMS-хэш.

		/// Возвращает `true`, если HLMS-хэши ещё не вычислены.
		bool needsHashing() const
		{
			return this->hash == 0 || this->casterHash == 0;
		}

		/**
		 * @brief Предрассчитывает `hash` и `casterHash` через фиктивный `RenderObject`.
		 * @par Кто вызывает
		 * @ref RenderInterface при создании материала (`LoadTexture`, `GenerateTexture`, `CompileShader`,
		 * `SaveLayerAsTexture`, конструктор) и лениво при первом `RenderGeometry`/`RenderShader`.
		 */
		void calculateHlmsHash();
	};

} // namespace nimble::RmlOgre

#endif // NIMBLE_RMLOGRE_MATERIAL_HPP