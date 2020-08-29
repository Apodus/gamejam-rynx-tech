
#include <rynx/application/application.hpp>
#include <rynx/application/visualisation/debug_visualisation.hpp>
#include <rynx/application/logic.hpp>
#include <rynx/application/render.hpp>
#include <rynx/application/simulation.hpp>
#include <rynx/application/render.hpp>

#include <rynx/menu/Button.hpp>
#include <rynx/menu/Slider.hpp>
#include <rynx/menu/Div.hpp>

#include <rynx/graphics/framebuffer.hpp>
#include <rynx/graphics/renderer/screenspace.hpp>
#include <rynx/graphics/renderer/meshrenderer.hpp>
#include <rynx/graphics/mesh/shape.hpp>
#include <rynx/math/geometry/polygon_triangulation.hpp>
#include <rynx/graphics/text/fontdata/lenka.hpp>
#include <rynx/graphics/text/fontdata/consolamono.hpp>

#include <rynx/rulesets/frustum_culling.hpp>
#include <rynx/rulesets/motion.hpp>
#include <rynx/rulesets/physics/springs.hpp>
#include <rynx/rulesets/collisions.hpp>
#include <rynx/rulesets/particles.hpp>
#include <rynx/rulesets/lifetime.hpp>

#include <rynx/tech/smooth_value.hpp>
#include <rynx/tech/timer.hpp>
#include <rynx/tech/ecs.hpp>

#include <rynx/tech/components.hpp>
#include <rynx/application/components.hpp>

#include <rynx/input/mapped_input.hpp>
#include <rynx/scheduler/task_scheduler.hpp>

#include <iostream>
#include <thread>

#include <cmath>

#include <rynx/audio/audio.hpp>
#include <game/menu.hpp>

#include <rynx/math/geometry/plane.hpp>
#include <rynx/math/geometry/ray.hpp>

#include <game/components.hpp>
#include <game/hero.hpp>

template<typename T>
struct range {
	range(T b, T e) : begin(b), end(e) {}
	range() = default;

	T begin;
	T end;

	T operator()(float v) const {
		return static_cast<T>(begin * (1.0f - v) + end * v);
	}
};

void attach_fire_to(rynx::ecs& ecs, rynx::ecs::id id) {
	rynx::components::particle_emitter emitter;
	emitter.edit().color_ranges(
		{ rynx::floats4{0.5f, 0.3f, 0.0f, 0.3f}, rynx::floats4{0.6f, 0.4f, 0.0f, 0.3f} },
		{ rynx::floats4{1.0f, 0.3f, 0.0f, 0.0f}, rynx::floats4{1.0f, 0.6f, 0.1f, 0.0f} }
	).radius_ranges(
		{ 4.0f, 7.5f },
		{ 0.0f, 0.1f }
	).lifetime_range({ 0.6f, 1.2f })
		.linear_dampening_range({ 0.3f, 0.6f })
		.spawn_rate_range({ 50.0f, 100.0f })
		.initial_velocity_range({ 100.0f, 300.0f })
		.initial_angle_range({0.0f, 1.0f})
		.rotate_with_host(true)
		.position_offset({0, -10, 0});

	ecs.attachToEntity(id, emitter);
}

int main(int argc, char** argv) {

	// uses this thread services of rynx, for example in cpu performance profiling.
	rynx::this_thread::rynx_thread_raii rynx_thread_services_required_token;

	rynx::application::Application application;
	application.openWindow(1920, 1080);
	application.loadTextures("../textures/textures.txt");
	application.meshRenderer().loadDefaultMesh("Empty");

	auto meshes = application.meshRenderer().meshes();
	{
		meshes->create("ball", rynx::Shape::makeCircle(1.0f, 32), "Hero");
		meshes->create("circle_empty", rynx::Shape::makeCircle(1.0f, 32), "Empty");
		auto* bg = meshes->create("background", rynx::Shape::makeRectangle(1, 1), "background");
		int i = 0;
		
		// background image should be equally lit from all directions, so normals point along the z-axis.
		while (i < bg->normals.size()) {
			bg->normals[i + 0] = 0;
			bg->normals[i + 1] = 0;
			bg->normals[i + 2] = 1;
			i += 3;
		}
		bg->rebuildNormalBuffer();
		bg->lighting_direction_bias = 0.65f;
		bg->lighting_global_multiplier = 1.0f;
	}

	rynx::scheduler::task_scheduler scheduler;
	rynx::application::simulation base_simulation(scheduler);
	rynx::ecs& ecs = base_simulation.m_ecs;

	std::shared_ptr<rynx::camera> camera = std::make_shared<rynx::camera>();
	camera->setProjection(0.02f, 20000.0f, application.aspectRatio());
	camera->setUpVector({0, 1, 0});
	camera->setDirection({ 0, 0, -1 });
	camera->setPosition({ 0, 0, +130 });

	rynx::mapped_input gameInput(application.input());

	GameMenu menu(application.textures());
	
	std::unique_ptr<rynx::collision_detection> detection = std::make_unique<rynx::collision_detection>();
	
	rynx::sound::audio_system audio;
	audio.set_default_attentuation_linear(0.01f);
	audio.set_default_attentuation_quadratic(0.000001f);
	audio.set_volume(1.0f);
	audio.adjust_volume(1.5f);

	audio.load("../sound/music/boogie01.ogg", "boogie");
	audio.load("../sound/music/boogie02.ogg", "boogie");
	audio.load("../sound/music/boogie03.ogg", "boogie");
	
	audio.load("../sound/music/bg01.ogg", "bg");
	audio.load("../sound/music/bg02.ogg", "bg");
	audio.load("../sound/music/bg03.ogg", "bg");
	audio.load("../sound/music/bg04.ogg", "bg");
	
	audio.load("../sound/music/tune1.ogg", "tune");

	game_collisions gameCollisionsSetup(*detection);
	
	{
		base_simulation.set_resource(detection.get());
		base_simulation.set_resource(&gameInput);
		base_simulation.set_resource(&audio);
		base_simulation.set_resource(camera.get());
	}


	// construct hero object.
	auto construct_player = [&](rynx::vec3f pos)
	{
		auto poly = rynx::Shape::makeBox(15.0f);

		{
			auto mesh = rynx::polygon_triangulation().generate_polygon_boundary(poly, application.textures()->textureLimits("Empty"), 3.0f);
			mesh->build();
			meshes->create("hero_mesh", std::move(mesh), "Empty");
		}

		float angle = 0;
		float radius = poly.radius();

		rynx::components::light_omni light;
		light.ambient = 0;
		light.attenuation_linear = 1.5f;
		light.attenuation_quadratic = 0.005f;
		light.color = {1.0f, 0.5f, 0.2f, 5.0f};

		auto head_id = ecs.create(
			light,
			rynx::components::position(pos, angle),
			rynx::components::collisions{ gameCollisionsSetup.category_dynamic().value },
			rynx::components::boundary({ poly.generateBoundary_Outside(1.0f) }, pos, angle),
			rynx::components::mesh(meshes->get("hero_mesh")),
			rynx::matrix4(),
			rynx::components::radius(radius),
			rynx::components::color({ 0.2f, 1.0f, 0.3f, 1.0f }),
			rynx::components::motion({ 0, 0, 0 }, 0),
			rynx::components::physical_body(150, 15000, 0.0f, 1.0f),
			rynx::components::dampening{ 0.05f, 0.05f },
			rynx::components::collision_custom_reaction{}
		);

		auto back_wheel_id = ecs.create(
			rynx::components::position(pos + rynx::vec3f(-22, -40, 0), angle),
			rynx::components::collisions{ gameCollisionsSetup.category_dynamic().value },
			rynx::components::mesh(meshes->get("hero_mesh")),
			rynx::matrix4(),
			rynx::components::radius(radius),
			rynx::components::color({ 0.2f, 1.0f, 0.3f, 1.0f }),
			rynx::components::motion({ 0, 0, 0 }, -10),
			rynx::components::physical_body(50, 4000, 0.0f, 30.0f),
			rynx::components::dampening{ 0.05f, 0.05f }
		);

		auto front_wheel_id = ecs.create(
			rynx::components::position(pos + rynx::vec3f(+25, -40, 0), angle),
			rynx::components::collisions{ gameCollisionsSetup.category_dynamic().value },
			rynx::components::mesh(meshes->get("hero_mesh")),
			rynx::matrix4(),
			rynx::components::radius(radius),
			rynx::components::color({ 0.2f, 1.0f, 0.3f, 1.0f }),
			rynx::components::motion({ 0, 0, 0 }, 0),
			rynx::components::physical_body(50, 4000, 0.0f, 10.0f),
			rynx::components::dampening{ 0.05f, 0.05f }
		);

		rynx::components::light_directed bike_light;
		bike_light.ambient = 0;
		bike_light.angle = 1.55f;
		bike_light.attenuation_linear = 1.5f;
		bike_light.attenuation_quadratic = 0.001f;
		bike_light.direction = {1.0f, 0.0f, 0.0f};
		bike_light.edge_softness = 0.3f;
		bike_light.color = {1.0f, 0.6f, 0.2f, 50.0f};

		auto bike_body_id = ecs.create(
			bike_light,
			rynx::components::position(pos + rynx::vec3f(+0, -25, 0), angle),
			rynx::components::collisions{ gameCollisionsSetup.category_dynamic().value },
			rynx::components::mesh(meshes->get("hero_mesh")),
			rynx::matrix4(),
			rynx::components::radius(radius),
			rynx::components::color({ 0.2f, 1.0f, 0.3f, 1.0f }),
			rynx::components::motion({ 0, 0, 0 }, 0),
			rynx::components::physical_body(250, 45000, 0.0f, 1.0f),
			rynx::components::dampening{ 0.05f, 0.05f }
		);

		auto connect_wheel_to_body = [&ecs](rynx::ecs::id wheel_id, rynx::ecs::id body_id, float strength, float softness, rynx::vec3f pos, rynx::vec3f pos2 = {}) {
			rynx::components::phys::joint joint;
			joint.connect_with_rod().rotation_free();
			joint.id_a = wheel_id;
			joint.id_b = body_id;
			joint.point_a = pos2;
			joint.point_b = pos;
			joint.strength = strength;
			joint.softness = softness;
			joint.length = rynx::components::phys::compute_current_joint_length(joint, ecs);
			return ecs.create(joint);
		};

		connect_wheel_to_body(back_wheel_id, bike_body_id, 1.2f, 3.0f, { -5, +5, 0 });
		connect_wheel_to_body(back_wheel_id, bike_body_id, 1.2f, 3.0f, { +5, -5, 0 });

		connect_wheel_to_body(front_wheel_id, bike_body_id, 1.2f, 3.0f, { +5, +5, 0 });
		connect_wheel_to_body(front_wheel_id, bike_body_id, 1.2f, 3.0f, { -5, -5, 0 });

		// connect rider to bike body or something.
		connect_wheel_to_body(head_id, bike_body_id, 0.9f, 3.0f, { -5, 0, 0 }, { -5, 0, 0 });
		connect_wheel_to_body(head_id, bike_body_id, 0.9f, 1.0f, { +5, 0, 0 }, { +5, 0, 0 });
		connect_wheel_to_body(head_id, bike_body_id, 0.9f, 3.0f, { 0, 0, 0 }, { 0, 0, 0 });
		auto hand_joint_id = connect_wheel_to_body(head_id, bike_body_id, 1.2f, 2.0f, { +22, +10, 0 }, { +5, 0, 0 }); // hand to steering.

		attach_fire_to(ecs, back_wheel_id);
		attach_fire_to(ecs, front_wheel_id);

		return std::tie(back_wheel_id, front_wheel_id, head_id, bike_body_id, hand_joint_id);
	};

	struct background_image_tag {};
	auto background_id = ecs.create(
		background_image_tag(),
		rynx::components::position(),
		rynx::components::radius(),
		rynx::components::mesh(meshes->get("background")),
		rynx::matrix4(),
		rynx::components::draw_always(),
		rynx::components::color({ 1.0f, 1.0f, 1.0f, 1.0f })
	);

	auto bike_ids = construct_player({ 0, 35, 0 });
	const auto& [back_wheel_id, front_wheel_id, head_id, bike_body_id, hand_joint_id] = bike_ids;

	// setup game logic
	{
		auto ruleset_hero_inputs = base_simulation.create_rule_set<game::hero_control>(gameInput, back_wheel_id, front_wheel_id, head_id, bike_body_id, hand_joint_id);
		auto ruleset_collisionDetection = base_simulation.create_rule_set<rynx::ruleset::physics_2d>();
		auto ruleset_particle_update = base_simulation.create_rule_set<rynx::ruleset::particle_system>();
		auto ruleset_frustum_culling = base_simulation.create_rule_set<rynx::ruleset::frustum_culling>(camera);
		auto ruleset_motion_updates = base_simulation.create_rule_set<rynx::ruleset::motion_updates>(rynx::vec3<float>(0, -160.8f, 0));
		auto ruleset_physical_springs = base_simulation.create_rule_set<rynx::ruleset::physics::springs>();
		auto ruleset_lifetime_updates = base_simulation.create_rule_set<rynx::ruleset::lifetime_updates>();
		auto ruleset_debug_input = base_simulation.create_rule_set<debug_input>(gameInput);

		ruleset_physical_springs->depends_on(ruleset_motion_updates);
		ruleset_collisionDetection->depends_on(ruleset_motion_updates);
		ruleset_frustum_culling->depends_on(ruleset_motion_updates);
		ruleset_hero_inputs->depends_on(ruleset_motion_updates);
		ruleset_collisionDetection->depends_on(ruleset_hero_inputs);
	}
	
	gameInput.generateAndBindGameKey(gameInput.getMouseKeyPhysical(0), "menuCursorActivation");

	std::atomic<size_t> tickCounter = 0;
	std::atomic<bool> dead_lock_detector_keepalive = true;
	std::thread dead_lock_detector([&]() {
		size_t prev_tick = 994839589;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		while (dead_lock_detector_keepalive) {
			if (prev_tick == tickCounter && dead_lock_detector_keepalive) {
				scheduler.dump();
				return;
			}
			prev_tick = tickCounter;
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
	});

	rynx::graphics::screenspace_draws(); // initialize gpu buffers for screenspace ops.
	rynx::application::renderer render(application, camera);
	render.set_lights_resolution(1.0f, 1.0f); // reduce pixels to make shit look bad

	audio.open_output_device(64, 64, rynx::sound::audio_system::format::int32);

	rynx::sound::configuration music;
	rynx::timer frame_timer_dt;
	float dt = 1.0f / 120.0f;

	/*
	// construct hero object.
	{
		auto poly = rynx::Shape::makeBox(15.0f);
		
		{
			auto mesh = rynx::polygon_triangulation().generate_polygon_boundary(poly, application.textures()->textureLimits("Empty"));
			mesh->build();
			meshes->create("hero_mesh", std::move(mesh));
		}

		rynx::vec3f pos;
		float angle = 0;
		float radius = poly.radius();
		ecs.create(
			rynx::components::position(pos, angle),
			rynx::components::collisions{ gameCollisionsSetup.category_dynamic().value },
			rynx::components::boundary({ poly.generateBoundary_Outside(1.0f) }, pos, angle),
			rynx::components::mesh(meshes->get("hero_mesh")),
			rynx::matrix4(),
			rynx::components::radius(radius),
			rynx::components::color({ 0.2f, 1.0f, 0.3f, 1.0f }),
			rynx::components::motion({ 0, 0, 0 }, 0),
			rynx::components::physical_body(5, 15, 0.0f, 1.0f),
			rynx::components::ignore_gravity(),
			rynx::components::dampening{ 0.99f, 0.99f },
			game::hero_tag{}
		);
	}
	*/


	auto create_terrain = [&]() {

		std::string mesh_name("terrain");

		
		rynx::polygon p;
		rynx::polygon terrain_surface;
		{
			float x_value = -1000.0f;
			while (x_value < +5000) {
				float y_value =
					250.0f * std::sinf(x_value * 0.0017f) +
					110.0f * std::sinf(x_value * 0.0073f) +
					50.0f * std::sinf(x_value * 0.013f) - 100.0f;
				p.vertices.emplace_back(x_value, y_value, 0.0f);
				x_value += 15.0f;
			}

			terrain_surface = p;

			p.vertices.emplace_back(rynx::vec3f{ +5100.0f, -1000.0f, 0.0f });
			p.vertices.emplace_back(rynx::vec3f{-600.0f, -1000.0f, 0.0f});

			std::reverse(p.vertices.begin(), p.vertices.end());
		}

		/*
		meshes->erase(mesh_name);
		auto* mesh_p = meshes->create(mesh_name, rynx::polygon_triangulation().generate_polygon_boundary(p, application.textures()->textureLimits("Empty"), 5.0f), "Empty");
		auto* mesh_p = meshes->create(mesh_name, rynx::polygon_triangulation().triangulate(p, application.textures()->textureLimits("Empty")), "Empty");
		*/

		terrain_surface.scale(1.0f / p.radius());
		std::vector<rynx::vec3f>& vertices = terrain_surface.vertices;

		std::unique_ptr<rynx::mesh> m = std::make_unique<rynx::mesh>();
		auto uv_limits = application.textures()->textureLimits("Empty");

		for (int32_t i = 0; i < vertices.size(); ++i) {
			auto& v = vertices[i];
			m->vertices.emplace_back(v.x);
			m->vertices.emplace_back(v.y);
			m->vertices.emplace_back(v.z);

			m->vertices.emplace_back(v.x);
			m->vertices.emplace_back(-1000.0f);
			m->vertices.emplace_back(v.z);

			auto normal = [&]() {
				if (i == 0) {
					return (vertices[1] - vertices[0]).normal2d();
				}
				if (i == vertices.size() - 1) {
					return (vertices[i] - vertices[i - 1]).normal2d();
				}
				return (vertices[i + 1] - vertices[i - 1]).normal2d();
			}();
			normal.normalize();

			m->normals.emplace_back(normal.x);
			m->normals.emplace_back(normal.y);
			m->normals.emplace_back(normal.z);

			m->normals.emplace_back(0.0f);
			m->normals.emplace_back(-1.0f);
			m->normals.emplace_back(0.0f);

			if (i > 1) {
				m->indices.emplace_back(static_cast<short>(2*i-3));
				m->indices.emplace_back(static_cast<short>(2*i));
				m->indices.emplace_back(static_cast<short>(2*i-4));

				m->indices.emplace_back(static_cast<short>(2*i));
				m->indices.emplace_back(static_cast<short>(2*i-3));
				m->indices.emplace_back(static_cast<short>(2*i+1));
			}

			// UVs are total bullshit. TODO: fix.
			m->texCoords.emplace_back(uv_limits.x);
			m->texCoords.emplace_back(uv_limits.y);
			m->texCoords.emplace_back(uv_limits.z);
			m->texCoords.emplace_back(uv_limits.w);
		}

		auto* mesh_p = meshes->create(mesh_name, std::move(m), "Empty");
		float radius = p.radius();
		
		return base_simulation.m_ecs.create(
			rynx::components::position({}, 0.0f),
			rynx::components::collisions{ gameCollisionsSetup.category_static().value },
			rynx::components::boundary({ p.generateBoundary_Outside(1.0f) }, {}, 0.0f),
			rynx::components::mesh(mesh_p),
			rynx::matrix4(),
			rynx::components::radius(radius),
			rynx::components::color({ 0.2f, 1.0f, 0.3f, 1.0f }),
			rynx::components::physical_body(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), 0.0f, 1.0f),
			rynx::components::ignore_gravity(),
			rynx::components::dampening{ 0.50f, 1.0f }
		);
	};
	
	create_terrain();

	/*
	ecs.create(
		rynx::components::position({0.0f, -55.0f, 0.0f}, 0),
		rynx::components::collisions{ gameCollisionsSetup.category_static().value },
		rynx::components::boundary({ rynx::Shape::makeRectangle(1000.0f, 5.0f).generateBoundary_Outside(1.0f),{0.0f, -55.0f, 0.0f} }),
		rynx::components::radius(1000.0f),
		rynx::components::color({ 0.2f, 1.0f, 0.3f, 1.0f }),
		rynx::components::physical_body(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), 0.0f, 1.0f),
		rynx::matrix4()
	);
	*/

	while (!application.isExitRequested()) {
		rynx_profile("Main", "frame");
		
		{
			rynx_profile("Main", "start frame");
			application.startFrame();
		}

		camera->tick(dt * 3);
		audio.set_listener_position(camera->position());

		{
			rynx_profile("Main", "Input handling");
			{
				auto mousePos = application.input()->getCursorPosition();
				auto collision = camera->ray_cast(mousePos.x, mousePos.y).intersect(rynx::plane(0, 0, 1, 0));
				if (collision.second) {
					gameInput.mouseWorldPosition(collision.first);
				}
			}
		}
		
		// TODO: Move music playing to some ruleset.
		if (music.completion_rate() > 0.90f) {
			music = audio.play_sound("bg", { 0, 0, 0 }, { 0, 0, 0 }, 1.0f);
		}

		auto new_bg_pos = camera->position();
		new_bg_pos.z = -10.0f;
		ecs[background_id].get<rynx::components::position>().value = new_bg_pos;
		ecs[background_id].get<rynx::components::radius>().r = (camera->position().z - new_bg_pos.z) * application.aspectRatio();
		auto& uvs = meshes->get("background")->texCoords;

		rynx_assert(uvs.size() == 8, "size mismatch");

		auto cam_pos = camera->position();

		float uv_x_mul = 1.0f / 2048.0f;
		float uv_y_mul = 1.0f / 2048.0f;
		float uv_z_mul = 0.001f;
		int uv_i = 0;

		float uv_x_modifier = cam_pos.y * uv_x_mul + cam_pos.z * uv_z_mul;
		float uv_y_modifier = cam_pos.x * uv_y_mul + cam_pos.z * uv_z_mul;
		constexpr float back_ground_image_scale = 0.25f;

		uvs[uv_i++] = 1.0f / back_ground_image_scale + uv_x_modifier;
		uvs[uv_i++] = 0.0f / back_ground_image_scale + uv_y_modifier;

		uvs[uv_i++] = 0.0f / back_ground_image_scale + uv_x_modifier;
		uvs[uv_i++] = 0.0f / back_ground_image_scale + uv_y_modifier;

		uvs[uv_i++] = 0.0f / back_ground_image_scale + uv_x_modifier;
		uvs[uv_i++] = 1.0f / back_ground_image_scale + uv_y_modifier;

		uvs[uv_i++] = 1.0f / back_ground_image_scale + uv_x_modifier;
		uvs[uv_i++] = 1.0f / back_ground_image_scale + uv_y_modifier;

		meshes->get("background")->rebuildTextureBuffer();
		
		camera->setProjection(0.02f, 2000.0f, application.aspectRatio());
		camera->rebuild_view_matrix();

		{
			rynx_profile("Main", "Wait for frame end");
			base_simulation.generate_tasks(dt);
			scheduler.start_frame();
			scheduler.wait_until_complete();
			++tickCounter;
		}

		menu.logic_tick(dt, application.aspectRatio(), gameInput);

		// should we render or not.
		{
			rynx_profile("Main", "graphics");

			{
				rynx_profile("Main", "prepare");
				render.prepare(base_simulation.m_context);
				scheduler.start_frame();

				// while waiting for computing to be completed, draw menus.
				application.meshRenderer().setDepthTest(false);
				menu.graphics_tick(application.aspectRatio(), application.meshRenderer(), application.textRenderer());

				scheduler.wait_until_complete();
			}

			{
				rynx_profile("Main", "draw");
				
				// present result of previous frame.
				application.swapBuffers();

				// render current frame.
				render.execute();

				// TODO: debug visualisations should be drawn on their own fbo?
				application.debugVis()->prepare(base_simulation.m_context);
				application.debugVis()->execute();
			
				{
					application.shaders()->activate_shader("fbo_color_to_bb");
					menu.fbo()->bind_as_input();
					rynx::graphics::screenspace_draws::draw_fullscreen();
				}
			}
		}

		{
			rynx_profile("Main", "Clean up dead entitites");
			auto ids_dead = ecs.query().in<rynx::components::dead>().ids();
			for (auto id : ids_dead) {
				if (ecs[id].has<rynx::components::collisions>()) {
					auto collisions = ecs[id].get<rynx::components::collisions>();
					detection->erase(id.value, collisions.category);
				}
			}

			base_simulation.m_logic.entities_erased(*base_simulation.m_context, ids_dead);
			ecs.erase(ids_dead);
		}

		// update dt for next frame.
		dt = std::min(0.016f, std::max(0.0001f, frame_timer_dt.time_since_last_access_seconds_float()));
	}

	dead_lock_detector_keepalive = false;
	dead_lock_detector.join();
	return 0;
}
