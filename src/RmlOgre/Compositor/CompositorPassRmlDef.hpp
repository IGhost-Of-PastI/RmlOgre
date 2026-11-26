// CompositorPassRmlDef.hpp
#pragma once

#include <Compositor/Pass/OgreCompositorPassDef.h>

namespace nimble::RmlOgre {

	struct RmlFrameData;

	class CompositorPassRmlDef : public Ogre::CompositorPassDef {
	public:
		const RmlFrameData* mFrameData = nullptr;

		explicit CompositorPassRmlDef(Ogre::CompositorTargetDef* parentTargetDef)
			: Ogre::CompositorPassDef(Ogre::PASS_CUSTOM, parentTargetDef)
		{
		}
	};

} // namespace nimble::RmlOgre