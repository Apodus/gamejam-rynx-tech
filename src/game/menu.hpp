
#include <rynx/menu/Div.hpp>
#include <rynx/menu/Button.hpp>
#include <rynx/menu/Slider.hpp>
#include <rynx/menu/List.hpp>

#include <rynx/math/vector.hpp>

#include <rynx/tech/profiling.hpp>
#include <rynx/graphics/renderer/meshrenderer.hpp>
#include <rynx/tech/collision_detection.hpp>
#include <rynx/graphics/mesh/shape.hpp>
#include <rynx/application/components.hpp>
#include <rynx/graphics/framebuffer.hpp>
#include <rynx/graphics/text/font.hpp>
#include <rynx/graphics/text/fontdata/consolamono.hpp>
#include <rynx/graphics/text/fontdata/lenka.hpp>

#include <rynx/graphics/renderer/textrenderer.hpp>

#include <rynx/math/geometry/ray.hpp>
#include <rynx/math/geometry/plane.hpp>

#include <editor/editor.hpp>

class ieditor_tool {
public:
	virtual void update(rynx::scheduler::context& ctx) = 0;
	virtual void on_tool_selected() = 0;
	virtual void on_tool_unselected() = 0;
};

namespace tools {
	class selection_tool : public ieditor_tool {
	public:
		selection_tool(rynx::scheduler::context& ctx) {
			auto& input = ctx.get_resource<rynx::mapped_input>();
			m_activation_key = input.generateAndBindGameKey(input.getMouseKeyPhysical(0), "selection tool activate");
		}

		virtual void update(rynx::scheduler::context& ctx) override {
			if (m_run_on_main_thread) {
				m_run_on_main_thread();
				m_run_on_main_thread = nullptr;
			}

			ctx.add_task("editor tick", [this](
				rynx::ecs& game_ecs,
				rynx::mapped_input& gameInput,
				rynx::camera& gameCamera)
			{
				if (gameInput.isKeyPressed(m_activation_key) && !gameInput.isKeyConsumed(m_activation_key)) {
					auto mouseRay = gameInput.mouseRay(gameCamera);
					auto [mouse_z_plane, hit] = mouseRay.intersect(rynx::plane(0, 0, 1, 0));
					mouse_z_plane.z = 0;

					if (hit) {
						on_key_press(game_ecs, mouse_z_plane);
					}
				}
			});
		}

		rynx::ecs::id selected_entity() const {
			return m_selected_entity_id;
		}

		void on_entity_selected(std::function<void(rynx::ecs::id)> op) {
			m_on_entity_selected = std::move(op);
		}

		virtual void on_tool_selected() override {
		}

		virtual void on_tool_unselected() override {
		}

	private:
		void on_key_press(rynx::ecs& game_ecs, rynx::vec3f cursorWorldPos) {
			float best_distance = 1e30f;
			rynx::ecs::id best_id;

			// find best selection
			game_ecs.query().for_each([&, mouse_world_pos = cursorWorldPos](rynx::ecs::id id, rynx::components::position pos) {
				float sqr_dist = (mouse_world_pos - pos.value).length_squared();
				auto* ptr = game_ecs[id].try_get<rynx::components::boundary>();

				if (sqr_dist < best_distance) {
					best_distance = sqr_dist;
					best_id = id;
				}

				if (ptr) {
					for (size_t i = 0; i < ptr->segments_world.size(); ++i) {
						// some threshold for vertex picking
						const auto vertex = ptr->segments_world.segment(i);
						auto [dist, shortest_pos] = rynx::math::pointDistanceLineSegmentSquared(vertex.p1, vertex.p2, mouse_world_pos);
						if (dist < best_distance) {
							best_distance = dist;
							best_id = id;
						}
					}
				}
			});

			// unselect previous selection
			if (game_ecs.exists(m_selected_entity_id)) {
				auto* color_ptr = game_ecs[m_selected_entity_id].try_get<rynx::components::color>();
				if (color_ptr) {
					color_ptr->value = m_selected_entity_original_color;
				}
			}

			// select new selection
			{
				auto* color_ptr = game_ecs[best_id].try_get<rynx::components::color>();
				if (color_ptr) {
					m_selected_entity_original_color = color_ptr->value;
					color_ptr->value = rynx::floats4{ 1.0f, 0.0f, 0.0f, 1.0f };
				}

				m_selected_entity_id = best_id;
				if (m_on_entity_selected) {
					m_run_on_main_thread = [this, selected_entity = m_selected_entity_id]() {
						m_on_entity_selected(selected_entity);
					};
				}
				std::cerr << "entity selection tool picked: " << best_id.value << std::endl;
			}
		}

		std::function<void()> m_run_on_main_thread;
		std::function<void(rynx::ecs::id)> m_on_entity_selected;
		rynx::ecs::id m_selected_entity_id;
		rynx::floats4 m_selected_entity_original_color;
		rynx::key::logical m_activation_key;
	};


	class polygon_tool : public ieditor_tool {
	public:
		polygon_tool(rynx::scheduler::context& ctx, selection_tool* selection) {
			auto& input = ctx.get_resource<rynx::mapped_input>();
			m_activation_key = input.generateAndBindGameKey(input.getMouseKeyPhysical(0), "polygon tool activate");
			m_secondary_activation_key = input.generateAndBindGameKey(input.getMouseKeyPhysical(1), "polygon tool activate");
			m_key_smooth = input.generateAndBindGameKey(',', "polygon smooth op");
			m_selection_tool = selection;
		}

		virtual void update(rynx::scheduler::context& ctx) override {
			ctx.add_task("polygon tool tick", [this](
				rynx::ecs& game_ecs,
				rynx::collision_detection& detection,
				rynx::mapped_input& gameInput,
				rynx::camera& gameCamera)
				{
					auto id = m_selection_tool->selected_entity();
					if (game_ecs.exists(id)) {
						auto entity = game_ecs[id];
						if (entity.has<rynx::components::boundary>()) {
							auto mouseRay = gameInput.mouseRay(gameCamera);
							auto [mouse_z_plane, hit] = mouseRay.intersect(rynx::plane(0, 0, 1, 0));
							mouse_z_plane.z = 0;

							if (gameInput.isKeyPressed(m_activation_key)) {
								if (hit) {
									if (!vertex_create(game_ecs, mouse_z_plane)) {
										m_selected_vertex = vertex_select(game_ecs, mouse_z_plane);
									}
									drag_operation_start(game_ecs, mouse_z_plane);
								}
							}

							if (gameInput.isKeyPressed(m_secondary_activation_key)) {
								if (hit) {
									int32_t vertex_index = vertex_select(game_ecs, mouse_z_plane);
									if (vertex_index >= 0) {
										auto& boundary = entity.get<rynx::components::boundary>();
										auto pos = entity.get<rynx::components::position>();
										boundary.segments_local.edit().erase(vertex_index);
										boundary.segments_world = boundary.segments_local;
										boundary.update_world_positions(pos.value, pos.angle);
									}
								}
							}

							if (gameInput.isKeyDown(m_activation_key)) {
								drag_operation_update(game_ecs, mouse_z_plane);
							}

							if (gameInput.isKeyReleased(m_activation_key)) {
								drag_operation_end(game_ecs, detection);
							}

							// smooth selected polygon
							if (gameInput.isKeyClicked(m_key_smooth)) {
								auto& boundary = entity.get<rynx::components::boundary>();
								boundary.segments_local.edit().smooth(3);
								boundary.segments_local.recompute_normals();
								boundary.segments_world = boundary.segments_local;

								auto pos = entity.get<rynx::components::position>();
								boundary.update_world_positions(pos.value, pos.angle);

								// TODO: should really also update radius.
							}
						}
					}
				});
		}

		virtual void on_tool_selected() override {
		}

		virtual void on_tool_unselected() override {
			m_drag_action_active = false;
			m_selected_vertex = -1;
		}

	private:
		bool vertex_create(rynx::ecs& game_ecs, rynx::vec3f cursorWorldPos) {
			float best_distance = 1e30f;
			int best_vertex = -1;
			bool should_create = false;

			// find best selection
			auto entity = game_ecs[m_selection_tool->selected_entity()];
			auto radius = entity.get<rynx::components::radius>();
			auto pos = entity.get<rynx::components::position>();
			auto& boundary = entity.get<rynx::components::boundary>();

			for (size_t i = 0; i < boundary.segments_world.size(); ++i) {
				// some threshold for vertex picking
				const auto vertex = boundary.segments_world.segment(i);
				
				if ((vertex.p1 - cursorWorldPos).length_squared() < best_distance) {
					best_distance = (vertex.p1 - cursorWorldPos).length_squared();
					best_vertex = int(i);
					should_create = false;
				}

				float segment_mid_dist = ((vertex.p1 + vertex.p2) * 0.5f - cursorWorldPos).length_squared();
				if (segment_mid_dist < best_distance) {
					best_distance = segment_mid_dist;
					best_vertex = int(i);
					should_create = true;
				}
			}

			if (should_create) {
				boundary.segments_local.edit().insert(best_vertex, cursorWorldPos - pos.value);
				boundary.segments_world = boundary.segments_local;
				boundary.update_world_positions(pos.value, pos.angle);
				m_selected_vertex = best_vertex + 1;
			}

			return should_create;
		}

		int32_t vertex_select(rynx::ecs& game_ecs, rynx::vec3f cursorWorldPos) {
			float best_distance = 1e30f;
			int best_vertex = -1;
			
			// find best selection
			auto entity = game_ecs[m_selection_tool->selected_entity()];
			auto radius = entity.get<rynx::components::radius>();
			auto pos = entity.get<rynx::components::position>();
			const auto& boundary = entity.get<rynx::components::boundary>();
	
			for (size_t i = 0; i < boundary.segments_world.size(); ++i) {
				// some threshold for vertex picking
				const auto vertex = boundary.segments_world.segment(i);
				float limit = (best_vertex == -1) ? 15.0f * 15.0f : best_distance;
				if ((vertex.p1 - cursorWorldPos).length_squared() < limit) {
					best_distance = (vertex.p1 - cursorWorldPos).length_squared();
					best_vertex = int(i);
				}
			}

			return best_vertex;
		}

		void drag_operation_start(rynx::ecs& game_ecs, rynx::vec3f cursorWorldPos) {
			m_drag_action_mouse_origin = cursorWorldPos;
			if (m_selected_vertex != -1) {
				auto& boundary = game_ecs[m_selection_tool->selected_entity()].get<rynx::components::boundary>();
				m_drag_action_object_origin = boundary.segments_local.vertex_position(m_selected_vertex);
			}
			else {
				m_drag_action_object_origin = game_ecs[m_selection_tool->selected_entity()].get<rynx::components::position>().value;
			}
		}

		void drag_operation_update(rynx::ecs& game_ecs, rynx::vec3f cursorWorldPos) {
			if (m_drag_action_active || (cursorWorldPos - m_drag_action_mouse_origin).length_squared() > 10.0f * 10.0f) {
				m_drag_action_active = true;
				auto entity = game_ecs[m_selection_tool->selected_entity()];
				auto& entity_pos = entity.get<rynx::components::position>();
				rynx::vec3f position_delta = cursorWorldPos - m_drag_action_mouse_origin;
				
				if (m_selected_vertex != -1) {
					auto& boundary = entity.get<rynx::components::boundary>();
					auto editor = boundary.segments_local.edit();
					editor.vertex(m_selected_vertex).position(m_drag_action_object_origin + position_delta);

					// todo: just update the edited parts
					boundary.update_world_positions(entity_pos.value, entity_pos.angle);
				}
				else {
					entity_pos.value = m_drag_action_object_origin + position_delta;
				}
			}
		}

		void drag_operation_end(rynx::ecs& game_ecs, rynx::collision_detection& detection) {
			if (m_drag_action_active) {
				auto entity = game_ecs[m_selection_tool->selected_entity()];
				auto& entity_pos = entity.get<rynx::components::position>();
				auto& boundary = entity.get<rynx::components::boundary>();

				{
					auto editor = boundary.segments_local.edit();
					auto bounding_sphere = boundary.segments_local.bounding_sphere();
					editor.translate(-bounding_sphere.first);
					entity_pos.value += bounding_sphere.first;
				}
				
				entity.get<rynx::components::radius>().r = boundary.segments_local.radius();
				boundary.update_world_positions(entity_pos.value, entity_pos.angle);
				detection.update_entity_forced(game_ecs, entity.id());
			}

			m_drag_action_active = false;
		}

		selection_tool* m_selection_tool = nullptr;
		int32_t m_selected_vertex = -1; // -1 is none, otherwise this is an index to polygon vertex array.
		rynx::key::logical m_activation_key;
		rynx::key::logical m_secondary_activation_key;

		rynx::key::logical m_key_smooth;

		rynx::vec3f m_drag_action_mouse_origin;
		rynx::vec3f m_drag_action_object_origin;
		bool m_drag_action_active = false;
	};
}

class editor_rules : public rynx::application::logic::iruleset {
	rynx::binary_config::id m_editor_state;
	rynx::binary_config::id m_game_state;

	rynx::key::logical key_createPolygon;
	rynx::key::logical key_createBox;

	rynx::key::logical key_selection_tool;
	rynx::key::logical key_polygon_tool;

	rynx::collision_detection::category_id m_static_collisions;
	rynx::collision_detection::category_id m_dynamic_collisions;

	std::shared_ptr<rynx::menu::Div> m_editor_menu;
	
	tools::selection_tool m_selection_tool;
	tools::polygon_tool m_polygon_tool;
	
	ieditor_tool* m_active_tool;
	rynx::reflection::reflections& m_reflections;

public:
	editor_rules(
		rynx::scheduler::context& ctx,
		rynx::reflection::reflections& reflections,
		Font* font,
		std::shared_ptr<rynx::menu::Div> editor_menu,
		rynx::graphics::GPUTextures& textures,
		rynx::mapped_input& gameInput,
		rynx::collision_detection::category_id dynamic_collisions,
		rynx::collision_detection::category_id static_collisions,
		rynx::binary_config::id game_state,
		rynx::binary_config::id editor_state)
	: m_editor_menu(editor_menu)
	, m_selection_tool(ctx)
	, m_polygon_tool(ctx, &m_selection_tool)
	, m_reflections(reflections)
	{
		// create editor menus
		{
			// m_editor_menu->alignToInnerEdge(rynx::menu::Align::RIGHT, +0.9f);
			auto editor_tools_side_bar = std::make_shared<rynx::menu::Div>(rynx::vec3f{0.3f, 1.0f, 0.0f});
			editor_tools_side_bar->align().right_inside().offset(+0.9f);
			editor_tools_side_bar->on_hover([ptr = editor_tools_side_bar.get()](rynx::vec3f mousePos, bool inRect) {
				if (inRect) {
					ptr->align().offset(0.0f);
				}
				else {
					ptr->align().offset(+0.9f);
				}
				return inRect;
			});

			auto editor_entity_properties_bar = std::make_shared<rynx::menu::Div>(rynx::vec3f(0.3f, 1.0f, 0.0f));
			editor_entity_properties_bar->align().left_inside().offset(+0.9f);
			editor_entity_properties_bar->on_hover([ptr = editor_entity_properties_bar.get()](rynx::vec3f mousePos, bool inRect) {
				if (inRect) {
					ptr->align().offset(0.0f);
				}
				else {
					ptr->align().offset(+0.9f);
				}
				return inRect;
			});

			m_editor_menu->addChild(editor_tools_side_bar);
			m_editor_menu->addChild(editor_entity_properties_bar);

			editor_tools_side_bar->set_background(textures, "Frame");
			editor_entity_properties_bar->set_background(textures, "Frame");
		
			// editor tools menu
			{
				auto selection_tool_button = std::make_shared<rynx::menu::Button>(textures, "Frame", rynx::vec3f(0.15f, 0.15f * 0.3f, 0.0f));
				selection_tool_button->respect_aspect_ratio();
				selection_tool_button->align().target(editor_tools_side_bar.get()).top_left_inside().offset(-0.3f);
				selection_tool_button->velocity_position(100.0f);
				// selection_tool_button->text("hehe");
				selection_tool_button->on_click([this]() { switch_to_tool(m_selection_tool); });
				editor_tools_side_bar->addChild(selection_tool_button);

				auto polygon_tool_button = std::make_shared<rynx::menu::Button>(textures, "Frame", rynx::vec3f(0.15f, 0.15f * 0.3f, 0.0f));
				polygon_tool_button->align().target(selection_tool_button.get()).right_outside().offset_x(0.3f).top_inside();
				polygon_tool_button->respect_aspect_ratio();
				polygon_tool_button->velocity_position(100.0f);
				// polygon_tool_button->text("boo");
				polygon_tool_button->on_click([this]() { switch_to_tool(m_polygon_tool); });
				editor_tools_side_bar->addChild(polygon_tool_button);
			}

			// editor entity view menu
			{
				auto entity_property_list = std::make_shared<rynx::menu::List>(textures, "Frame", rynx::vec3f(1.0f, 1.0f, 0.0f));
				entity_property_list->list_endpoint_margin(0.05f);
				entity_property_list->list_element_margin(0.05f);
				entity_property_list->list_element_velocity(2500.0f);

				editor_entity_properties_bar->addChild(entity_property_list);

				m_selection_tool.on_entity_selected([this, font, &reflections, &ctx, &textures, entity_property_list](rynx::ecs::id id) {
					rynx::ecs& ecs = ctx.get_resource<rynx::ecs>();
					auto reflections_vec = ecs[id].reflections(m_reflections);
					entity_property_list->clear_children();

					for (auto&& reflection_entry : reflections_vec) {
						auto component_sheet = std::make_shared<rynx::menu::Div>(rynx::vec3f(0.0f, 0.0f, 0.0f));
						component_sheet->set_background(textures, "Frame");
						component_sheet->set_dynamic_position_and_scale();
						entity_property_list->addChild(component_sheet);

						auto component_name = std::make_shared<rynx::menu::Button>(textures, "Frame", rynx::vec3f(0.6f, 0.025f, 0.0f));
						component_name->text()
							.text(reflection_entry.m_type_name)
							.text_align_left()
							.font(font);
						
						component_name->align()
							.top_inside()
							.left_inside()
							.offset_kind_relative_to_self()
							.offset_y(-0.5f);

						component_sheet->addChild(component_name);
						component_name->velocity_position(100.0f);
						
						rynx::editor::rynx_common_info component_common_info;
						component_common_info.component_type_id = reflection_entry.m_type_index_value;
						component_common_info.ecs = &ecs;
						component_common_info.entity_id = id;
						component_common_info.textures = &textures;

						rynx::editor::generate_menu_for_reflection(reflections, reflection_entry, component_common_info, component_sheet.get());
					}
				});
			}
		}

		m_active_tool = &m_selection_tool;
		m_active_tool->on_tool_selected();

		key_createPolygon = gameInput.generateAndBindGameKey({ 'P' }, "Create polygon");
		key_createBox = gameInput.generateAndBindGameKey({ 'O' }, "Create box");
		
		key_selection_tool = gameInput.generateAndBindGameKey('_', "selection tool");
		key_polygon_tool = gameInput.generateAndBindGameKey('.', "polygon tool");

		m_editor_state = editor_state;
		m_game_state = game_state;
		m_static_collisions = static_collisions;
		m_dynamic_collisions = dynamic_collisions;
	}

	void switch_to_tool(ieditor_tool& tool) {
		m_active_tool->on_tool_unselected();
		tool.on_tool_selected();
		m_active_tool = &tool;
	}

private:
	virtual void onFrameProcess(rynx::scheduler::context& context, float dt) override {
		
		m_active_tool->update(context);

		context.add_task("editor tick", [this, dt](
			rynx::ecs& game_ecs,
			rynx::mapped_input& gameInput,
			rynx::camera& gameCamera)
			{
				if (gameInput.isKeyClicked(key_selection_tool)) {
					switch_to_tool(m_selection_tool);
				}
				if (gameInput.isKeyClicked(key_polygon_tool)) {
					switch_to_tool(m_polygon_tool);
				}

				auto mouseRay = gameInput.mouseRay(gameCamera);
				auto mouse_z_plane = mouseRay.intersect(rynx::plane(0, 0, 1, 0));
				mouse_z_plane.first.z = 0;

				if (mouse_z_plane.second) {
					
					if (gameInput.isKeyClicked(key_createPolygon)) {
						auto p = rynx::Shape::makeTriangle(50.0f);
						game_ecs.create(
							rynx::components::position(mouse_z_plane.first, 0.0f),
							rynx::components::collisions{ m_static_collisions.value },
							rynx::components::boundary(p, mouse_z_plane.first, 0.0f),
							rynx::components::radius(p.radius()),
							rynx::components::color({ 0.2f, 1.0f, 0.3f, 1.0f }),
							rynx::components::physical_body().mass(std::numeric_limits<float>::max()).friction(1.0f).elasticity(0.0f).moment_of_inertia(std::numeric_limits<float>::max()),
							rynx::components::ignore_gravity(),
							rynx::components::dampening{ 0.50f, 1.0f }
						);
					}

					if (gameInput.isKeyClicked(key_createBox)) {
						auto p = rynx::Shape::makeBox(20.0f);
						game_ecs.create(
							rynx::components::position(mouse_z_plane.first, 0.0f),
							rynx::components::motion{},
							rynx::components::collisions{ m_dynamic_collisions.value },
							rynx::components::boundary(p, mouse_z_plane.first, 0.0f),
							rynx::components::radius(p.radius()),
							rynx::components::color({ 0.2f, 1.0f, 0.3f, 1.0f }),
							rynx::components::physical_body().mass(550.0f).friction(1.0f).elasticity(0.0f).moment_of_inertia(p, 2.0f),
							rynx::matrix4{}
						);
					}
				}
			});
	}
};

class debug_input : public rynx::application::logic::iruleset {
	rynx::key::logical zoomOut;
	rynx::key::logical zoomIn;

	rynx::key::logical camera_orientation_key;
	rynx::key::logical cameraUp;
	rynx::key::logical cameraLeft;
	rynx::key::logical cameraRight;
	rynx::key::logical cameraDown;

	rynx::key::logical yawCounterClockWise;
	rynx::key::logical yawClockWise;

	rynx::key::logical key_toggleGameState;
	rynx::key::logical key_toggleEditorState;
	rynx::key::logical key_toggleFrustumCullState;

	rynx::binary_config::id m_editor_state;
	rynx::binary_config::id m_game_state;
	rynx::binary_config::id m_frustum_cull_state;

public:
	debug_input(rynx::mapped_input& gameInput, rynx::binary_config::id editor_state, rynx::binary_config::id game_state, rynx::binary_config::id frustum_cull_update_state) {
		zoomOut = gameInput.generateAndBindGameKey('1', "zoom out");
		zoomIn = gameInput.generateAndBindGameKey('2', "zoom in");

		camera_orientation_key = gameInput.generateAndBindGameKey(gameInput.getMouseKeyPhysical(1), "camera_orientation");
		cameraUp = gameInput.generateAndBindGameKey('W', "cameraUp");
		cameraLeft = gameInput.generateAndBindGameKey('A', "cameraLeft");
		cameraRight = gameInput.generateAndBindGameKey('D', "cameraRight");
		cameraDown = gameInput.generateAndBindGameKey('S', "cameraDown");

		key_toggleGameState = gameInput.generateAndBindGameKey('1', "stateGame");
		key_toggleEditorState = gameInput.generateAndBindGameKey('2', "stateEditor");
		key_toggleFrustumCullState = gameInput.generateAndBindGameKey('3', "stateFrustum");

		yawCounterClockWise = gameInput.generateAndBindGameKey('Q', "yawCCW");
		yawClockWise = gameInput.generateAndBindGameKey('E', "yawCW");

		m_editor_state = editor_state;
		m_game_state = game_state;
		m_frustum_cull_state = frustum_cull_update_state;
	}

private:
	virtual void onFrameProcess(rynx::scheduler::context& context, float dt) override {
		context.add_task("debug camera updates", [this, dt](
			rynx::mapped_input& gameInput,
			rynx::camera& gameCamera)
		{
			{
				if (gameInput.isKeyDown(camera_orientation_key)) {
					auto mouseDelta = gameInput.mouseDelta();
					if (gameInput.isKeyDown(yawCounterClockWise))
						mouseDelta.z = +dt * 10;
					if (gameInput.isKeyDown(yawClockWise))
						mouseDelta.z = -dt * 10;

					gameCamera.rotate(mouseDelta);
				}

				const float camera_translate_multiplier = 200.4f * dt;
				const float camera_zoom_multiplier = (1.0f - dt * 3.0f);
				
				if (gameInput.isKeyReleased(key_toggleEditorState)) {
					m_editor_state.toggle();
				}
				if (gameInput.isKeyReleased(key_toggleGameState)) {
					m_game_state.toggle();
				}
				if (gameInput.isKeyReleased(key_toggleFrustumCullState)) {
					m_frustum_cull_state.toggle();
				}

				if (gameInput.isKeyDown(cameraUp)) { gameCamera.translate(gameCamera.local_forward() * camera_translate_multiplier); }
				if (gameInput.isKeyDown(cameraDown)) { gameCamera.translate(-gameCamera.local_forward() * camera_translate_multiplier); }
				if (gameInput.isKeyDown(cameraLeft)) { gameCamera.translate(gameCamera.local_left() * camera_translate_multiplier); }
				if (gameInput.isKeyDown(cameraRight)) { gameCamera.translate(-gameCamera.local_left() * camera_translate_multiplier); }
				// if (gameInput.isKeyDown(zoomOut)) { cameraPosition *= vec3<float>(1, 1.0f, 1.0f * camera_zoom_multiplier); }
				// if (gameInput.isKeyDown(zoomIn)) { cameraPosition *= vec3<float>(1, 1.0f, 1.0f / camera_zoom_multiplier); }
			}
		});
	}
};

class game_collisions {
public:
	game_collisions(rynx::collision_detection& collisionDetection) {
		collisionCategoryDynamic = collisionDetection.add_category();
		collisionCategoryStatic = collisionDetection.add_category();
		collisionCategoryProjectiles = collisionDetection.add_category();

		{
			collisionDetection.enable_collisions_between(collisionCategoryDynamic, collisionCategoryDynamic); // enable dynamic <-> dynamic collisions
			collisionDetection.enable_collisions_between(collisionCategoryDynamic, collisionCategoryStatic.ignore_collisions()); // enable dynamic <-> static collisions

			collisionDetection.enable_collisions_between(collisionCategoryProjectiles, collisionCategoryStatic.ignore_collisions()); // projectile <-> static
			collisionDetection.enable_collisions_between(collisionCategoryProjectiles, collisionCategoryDynamic); // projectile <-> dynamic
		}
	}

	rynx::collision_detection::category_id category_dynamic() const { return collisionCategoryDynamic; }
	rynx::collision_detection::category_id category_static() const { return collisionCategoryStatic; }
	rynx::collision_detection::category_id category_projectiles() const { return collisionCategoryProjectiles; }

private:
	rynx::collision_detection::category_id collisionCategoryDynamic;
	rynx::collision_detection::category_id collisionCategoryStatic;
	rynx::collision_detection::category_id collisionCategoryProjectiles;
};

class GameMenu {

	rynx::menu::System system;

	std::shared_ptr<rynx::graphics::framebuffer> fbo_menu;
	std::shared_ptr<rynx::camera> m_camera;

public:
	Font fontLenka;
	Font fontConsola;

	GameMenu(std::shared_ptr<rynx::graphics::GPUTextures> textures) :
		fontConsola(Fonts::setFontConsolaMono()),
		fontLenka(Fonts::setFontLenka())
	{
		fbo_menu = rynx::graphics::framebuffer::config()
			.set_default_resolution(1920, 1080)
			.add_rgba8_target("color")
			.construct(textures, "menu");

		m_camera = std::make_shared<rynx::camera>();

		auto sampleButton = std::make_shared<rynx::menu::Button>(
			*textures,
			"Frame",
			rynx::vec3<float>(0.4f, 0.1f, 0),
			rynx::vec3<float>(),
			0.14f
		);

		system.root()->addChild(sampleButton);

		/*
		auto sampleButton2 = std::make_shared<rynx::menu::Button>(*application.textures(), "Frame", &root, rynx::vec3<float>(0.4f, 0.1f, 0), rynx::vec3<float>(), 0.16f);
		auto sampleButton3 = std::make_shared<rynx::menu::Button>(*application.textures(), "Frame", &root, rynx::vec3<float>(0.4f, 0.1f, 0), rynx::vec3<float>(), 0.18f);
		auto sampleSlider = std::make_shared<rynx::menu::SlideBarVertical>(*application.textures(), "Frame", "Selection", &root, rynx::vec3<float>(0.4f, 0.1f, 0));
		auto megaSlider = std::make_shared<rynx::menu::SlideBarVertical>(*application.textures(), "Frame", "Selection", &root, rynx::vec3<float>(0.4f, 0.1f, 0));
		*/

		sampleButton->text().text("Log Profile").font(&fontConsola);
		sampleButton->align().bottom_left_inside();
		sampleButton->color(Color::RED);
		sampleButton->on_click([]() {
			rynx::profiling::write_profile_log();
		});

		/*
		sampleButton2->text("Dynamics").font(&fontConsola);
		sampleButton2->alignToOuterEdge(sampleButton.get(), rynx::menu::Align::RIGHT);
		sampleButton2->alignToInnerEdge(sampleButton.get(), rynx::menu::Align::BOTTOM);
		sampleButton2->onClick([&conf, self = sampleButton2.get()]() {
			bool new_value = !conf.visualize_dynamic_collisions;
			conf.visualize_dynamic_collisions = new_value;
			if (new_value) {
				self->color_frame(Color::GREEN);
			}
			else {
				self->color_frame(Color::RED);
			}
		});

		sampleButton3->text("Statics").font(&fontConsola);
		sampleButton3->alignToOuterEdge(sampleButton2.get(), rynx::menu::Align::TOP);
		sampleButton3->alignToInnerEdge(sampleButton2.get(), rynx::menu::Align::LEFT);
		sampleButton3->onClick([&conf, self = sampleButton3.get(), &root]() {
			bool new_value = !conf.visualize_static_collisions;
			conf.visualize_static_collisions = new_value;
			if (new_value) {
				self->color_frame(Color::GREEN);
			}
			else {
				self->color_frame(Color::RED);
			}
		});

		sampleSlider->alignToInnerEdge(&root, rynx::menu::Align::TOP_RIGHT);
		sampleSlider->onValueChanged([](float f) {});

		megaSlider->alignToOuterEdge(sampleSlider.get(), rynx::menu::Align::BOTTOM);
		megaSlider->alignToInnerEdge(sampleSlider.get(), rynx::menu::Align::LEFT);
		megaSlider->onValueChanged([](float f) {});
		*/

		/*
		root.addChild(sampleButton2);
		root.addChild(sampleButton3);
		root.addChild(sampleSlider);
		root.addChild(megaSlider);
		*/
	}

	rynx::menu::Component* root_node() {
		return system.root();
	}

	GameMenu& add_child(std::shared_ptr<rynx::menu::Component> child) {
		system.root()->addChild(std::move(child));
		return *this;
	}


	void logic_tick(float dt, float aspectRatio, rynx::mapped_input& gameInput) {
		system.input(gameInput);
		system.update(dt, aspectRatio);
		m_camera->tick(dt * 5.0f);
	}

	void graphics_tick(float aspectRatio, rynx::graphics::renderer& meshRenderer) {
		fbo_menu->bind_as_output();
		fbo_menu->clear();

		// 2, 2 is the size of the entire screen (in case of 1:1 aspect ratio) for menu camera. left edge is [-1, 0], top right is [+1, +1], etc.
		// so we make it size 2,2 to cover all of that. and then take aspect ratio into account by dividing the y-size.
		system.root()->scale_local({ 2 , 2 / aspectRatio, 0 });
		m_camera->setProjection(0.01f, 50.0f, aspectRatio);
		m_camera->setPosition({ 0, 0, 1 });
		m_camera->setDirection({0, 0, -1});
		m_camera->setUpVector({ 0, 1, 0 });
		m_camera->rebuild_view_matrix();

		meshRenderer.setCamera(m_camera);
		meshRenderer.cameraToGPU();
		system.draw(meshRenderer);
	}

	rynx::scoped_input_inhibitor inhibit_dedicated_inputs(rynx::mapped_input& input) {
		return system.inhibit_dedicated_inputs(input);
	}

	std::shared_ptr<rynx::graphics::framebuffer> fbo() {
		return fbo_menu;
	}
};