// CompositorPassRmlDef.hpp
#pragma once

#include <Compositor/Pass/OgreCompositorPassDef.h>

namespace nimble::RmlOgre {

	struct RmlFrameData;

	/**
	 * @brief Описание (definition) кастомного композитор-пасса `"rml/render"`.
	 *
	 * @par Кто создаёт
	 * `Ogre::CompositorPassProvider::addPassDef()` при добавлении `PASS_CUSTOM` с
	 * идентификатором `"rml/render"` в `Ogre::CompositorTargetDef`.
	 *
	 * @par Использование
	 * После добавления пасса в `CompositorTargetDef` хост присваивает @ref mFrameData
	 * адрес `renderInterface.frameData()` (или задаёт его позже на живом пассе через
	 * @ref CompositorPassRml::setFrameData).
	 */
	class CompositorPassRmlDef : public Ogre::CompositorPassDef {
	public:
		/// Не владеющий указатель на буфер команд @ref RenderInterface::frameData().
		const RmlFrameData* mFrameData = nullptr;

		explicit CompositorPassRmlDef(Ogre::CompositorTargetDef* parentTargetDef)
			: Ogre::CompositorPassDef(Ogre::PASS_CUSTOM, parentTargetDef)
		{
		}
	};

} // namespace nimble::RmlOgre