
#include <editor/editor.hpp>

#include <sstream>

void rynx::editor::float_field(
	const rynx::reflection::field& member,
	struct rynx_common_info info,
	rynx::menu::Component* component_sheet,
	std::vector<std::pair<rynx::reflection::type, rynx::reflection::field>> reflection_stack)
{
	int32_t mem_offset = info.cumulative_offset + member.m_memory_offset;
	float value = ecs_value_editor().access<float>(*info.ecs, info.entity_id, info.component_type_id, mem_offset);

	auto field_container = std::make_shared<rynx::menu::Div>(rynx::vec3f(0.6f, 0.03f, 0.0f));
	auto variable_name_label = std::make_shared<rynx::menu::Text>(rynx::vec3f(0.4f, 1.0f, 0.0f));
	variable_name_label->text(std::string(2 * info.indent, '-') + member.m_field_name);
	variable_name_label->text_align_left();

	auto variable_value_field = std::make_shared<rynx::menu::Button>(*info.textures, "Frame", rynx::vec3f(0.4f, 1.0f, 0.0f));

	struct field_config {
		float min_value = std::numeric_limits<float>::lowest();
		float max_value = std::numeric_limits<float>::max();
		bool slider_dynamic = true;

		float constrain(float v) const {
			if (v < min_value) return min_value;
			if (v > max_value) return max_value;
			return v;
		}
	};

	field_config config;

	auto handle_annotations = [&config](
		// const rynx::reflection::type& type,
		const rynx::reflection::field& field)
	{
		for (auto&& annotation : field.m_annotations) {
			if (annotation == ">=0")
				config.min_value = 0;
			if (annotation.starts_with("range")) {
				std::stringstream ss(annotation);
				std::string v;
				ss >> v;
				ss >> v;
				config.min_value = std::stof(v);
				ss >> v;
				config.max_value = std::stof(v);
				config.slider_dynamic = false;
			}
		}
	};

	for (auto it = reflection_stack.rbegin(); it != reflection_stack.rend(); ++it)
		handle_annotations(it->second);
	handle_annotations(member);

	variable_value_field->text()
		.text(std::to_string(value))
		.text_align_right()
		.input_enable();

	variable_value_field->text().on_value_changed([info, mem_offset, config](const std::string& s) {
		float new_value = 0.0f;
		try { new_value = config.constrain(std::stof(s)); }
		catch (...) { return; }

		ecs_value_editor().access<float>(*info.ecs, info.entity_id, info.component_type_id, mem_offset) = new_value;
	});

	std::shared_ptr<rynx::menu::SlideBarVertical> value_slider;

	if (config.slider_dynamic) {
		value_slider = std::make_shared<rynx::menu::SlideBarVertical>(*info.textures, "Frame", "Frame", rynx::vec3f(0.2f, 1.0f, 0.0f), -1.0f, +1.0f);
		value_slider->setValue(0);
		value_slider->on_active_tick([info, mem_offset, config, self = value_slider.get(), text_element = variable_value_field.get()](float /* input_v */, float dt) {
			float& v = ecs_value_editor().access<float>(*info.ecs, info.entity_id, info.component_type_id, mem_offset);
			float input_v = self->getValueCubic_AroundCenter();
			float tmp = v + dt * input_v;
			constexpr float value_modify_velocity = 3.0f;
			if (input_v * v > 0) {
				tmp *= (1.0f + std::fabs(input_v) * value_modify_velocity * dt);
			}
			else {
				tmp *= 1.0f / (1.0f + std::fabs(input_v) * value_modify_velocity * dt);
			}
			v = config.constrain(tmp);
			text_element->text().text(std::to_string(v));
		});

		value_slider->on_drag_end([self = value_slider.get()](float v) {
			self->setValue(0);
		});
	}
	else {
		value_slider = std::make_shared<rynx::menu::SlideBarVertical>(
			*info.textures,
			"Frame",
			"Frame",
			rynx::vec3f(0.2f, 1.0f, 0.0f),
			config.min_value,
			config.max_value);

		value_slider->setValue(ecs_value_editor().access<float>(*info.ecs, info.entity_id, info.component_type_id, mem_offset));
		value_slider->on_value_changed([info, mem_offset, text_element = variable_value_field.get()](float v) {
			float& field_value = ecs_value_editor().access<float>(*info.ecs, info.entity_id, info.component_type_id, mem_offset);
			field_value = v;
			text_element->text().text(std::to_string(v));
		});
	}

	value_slider->align().right_inside().top_inside().offset_x(-0.15f);
	variable_value_field->align().target(value_slider.get()).left_outside().top_inside();
	variable_name_label->align().left_inside().top_inside();

	field_container->addChild(variable_name_label);
	field_container->addChild(variable_value_field);
	field_container->addChild(value_slider);

	field_container->align()
		.target(component_sheet->last_child())
		.bottom_outside()
		.left_inside();

	field_container->velocity_position(500.0f);
	variable_name_label->velocity_position(1000.0f);
	variable_value_field->velocity_position(1000.0f);
	value_slider->velocity_position(1000.0f);
	component_sheet->addChild(field_container);
}




void rynx::editor::generate_menu_for_reflection(
	rynx::reflection::reflections& reflections_,
	const rynx::reflection::type& type_reflection,
	struct rynx_common_info info,
	rynx::menu::Component* component_sheet_,
	std::vector<std::pair<rynx::reflection::type, rynx::reflection::field>> reflection_stack)
{
	for (auto&& member : type_reflection.m_members) {
		if (member.m_type_name == "float") {
			rynx_common_info field_info = info;
			++field_info.indent;
			float_field(member, field_info, component_sheet_, reflection_stack);
		}
		else if (reflections_.has(member.m_type_name)) {
			auto label = std::make_shared<rynx::menu::Button>(*info.textures, "Frame", rynx::vec3f(0.6f, 0.03f, 0.0f));
			label->text()
				.text(std::string(info.indent + 1, '-') + member.m_field_name + " (" + member.m_type_name + ")")
				.text_align_left();

			label->align()
				.target(component_sheet_->last_child())
				.bottom_outside()
				.left_inside();

			label->velocity_position(100.0f);
			component_sheet_->addChild(label);

			rynx_common_info field_info;
			field_info = info;
			field_info.cumulative_offset += member.m_memory_offset;
			++field_info.indent;

			reflection_stack.emplace_back(type_reflection, member);
			const auto& member_type_reflection = reflections_.get(member);
			generate_menu_for_reflection(reflections_, member_type_reflection, field_info, component_sheet_, reflection_stack);
			reflection_stack.pop_back();
		}
		else {
			auto label = std::make_shared<rynx::menu::Button>(*info.textures, "Frame", rynx::vec3f(0.6f, 0.03f, 0.0f));
			label->text()
				.text(std::string(info.indent + 1, '-') + member.m_field_name + " (" + member.m_type_name + ")")
				.text_align_left();

			label->align()
				.target(component_sheet_->last_child())
				.bottom_outside()
				.left_inside();

			label->velocity_position(100.0f);
			component_sheet_->addChild(label);
		}
	}
}