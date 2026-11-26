#pragma once

#include "Material.hpp"

#include <OgreMatrix4.h>
#include <OgrePrerequisites.h>
#include <RmlUi/Core/Types.h>

#include <cstdint>
#include <variant>
#include <vector>

namespace Ogre {
	struct VertexArrayObject;
	class TextureGpu;
}

namespace nimble::RmlOgre {

	struct ScissorState {
		bool            enabled = false;
		Rml::Rectanglei region{};

		bool operator==(const ScissorState& rhs) const noexcept {
			if (!enabled && !rhs.enabled)
				return true;
			return enabled == rhs.enabled && region == rhs.region;
		}
	};

	enum class StencilMode : std::uint8_t {
		Disabled,
		TestEqual,       // color draw: stencil == ref
		WriteSet,        // clip mask:  always_pass, replace with 1 (color write off)
		WriteSetInverse, // clip mask:  always_pass, replace with 0 (color write off)
		WriteIntersect   // clip mask:  always_pass, increment     (color write off)
	};

	struct ClearStencilCmd {
		std::uint32_t value = 0; // 0 для Set, 1 для SetInverse
	};

	struct DrawCmd {
		Ogre::VertexArrayObject* vao = nullptr;
		Material                 material{};
		Ogre::Matrix4            transform = Ogre::Matrix4::IDENTITY;
		Rml::Vector2f            translation{ 0.0f, 0.0f };
		ScissorState             scissor{};
		StencilMode              stencilMode = StencilMode::Disabled;
		std::uint32_t            stencilRef = 0;
	};

	using RmlCommand = std::variant<ClearStencilCmd, DrawCmd>;

	struct RmlFrameData {
		Material                       stencilMaterial{}; // HlmsUnlit с mBlendChannelMask = 0
		std::vector<RmlCommand>        commands;
		std::vector<Ogre::TextureGpu*> textureDependencies;

		void clear() noexcept {
			commands.clear();
			textureDependencies.clear();
		}
	};

	inline bool can_batch_draws(const DrawCmd& a, const DrawCmd& b) noexcept {
		return a.stencilMode == b.stencilMode
			&& a.stencilRef == b.stencilRef
			&& a.scissor == b.scissor
			&& a.transform == b.transform;
	}

} // namespace nimble::RmlOgre