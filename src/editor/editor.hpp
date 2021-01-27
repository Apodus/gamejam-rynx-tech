
#pragma once

#include <rynx/tech/ecs.hpp>
#include <rynx/graphics/texture/texturehandler.hpp>

#include <rynx/menu/Div.hpp>
#include <rynx/menu/Text.hpp>
#include <rynx/menu/Button.hpp>
#include <rynx/menu/Slider.hpp>

namespace rynx {
	namespace editor {

		struct ecs_value_editor {
			template<typename T>
			T& access(rynx::ecs& ecs, rynx::ecs::id id, int32_t component_type_id, int32_t memoffset) {
				char* component_ptr = static_cast<char*>(ecs[id].get(component_type_id));
				return *reinterpret_cast<T*>(component_ptr + memoffset);
			}
		};

		struct rynx_common_info {
			rynx::ecs* ecs = nullptr;
			rynx::ecs::id entity_id = 0;
			rynx::graphics::GPUTextures* textures = nullptr;
			int32_t component_type_id = 0;
			int32_t cumulative_offset = 0;
			int32_t indent = 0;
		};

		void field_float(
			const rynx::reflection::field& member,
			struct rynx_common_info info,
			rynx::menu::Component* component_sheet,
			std::vector<std::pair<rynx::reflection::type, rynx::reflection::field>>
		);

		void field_bool(
			const rynx::reflection::field& member,
			struct rynx_common_info info,
			rynx::menu::Component* component_sheet,
			std::vector<std::pair<rynx::reflection::type, rynx::reflection::field>>
		);

		void generate_menu_for_reflection(
			rynx::reflection::reflections& reflections_,
			const rynx::reflection::type& type_reflection,
			struct rynx_common_info info,
			rynx::menu::Component* component_sheet_,
			std::vector<std::pair<rynx::reflection::type, rynx::reflection::field>> = {}
		);
	}
}