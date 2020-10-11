
#include <rynx/application/application.hpp>
#include <rynx/application/visualisation/debug_visualisation.hpp>
#include <rynx/application/visualisation/geometry/scrolling_background_2d.hpp>
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

#include <rynx/math/spline.hpp>

#include <game/terrain.hpp>
#include <game/bike_creation.hpp>

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
		
		meshes->create("wheel", rynx::Shape::makeCircle(1.0f, 32), "Wheel");
		meshes->create("head", rynx::Shape::makeBox(2.0f), "Head");
		meshes->create("bike_body", rynx::Shape::makeRectangle(4.0f, 2.0f), "BikeBody");

		auto* bg = meshes->create("background", rynx::Shape::makeRectangle(1.0f, 1), "background");
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

	audio.load("../sound/music/bg01.ogg", "bg");
	audio.load("../sound/music/bg02.ogg", "bg");
	audio.load("../sound/music/bg03.ogg", "bg");
	audio.load("../sound/music/bg04.ogg", "bg");

	audio.load("../sound/bike/rest01.ogg", "bike_rest");
	audio.load("../sound/bike/rest02.ogg", "bike_rest");
	audio.load("../sound/bike/rest03.ogg", "bike_rest");

	audio.load("../sound/bike/rest_base01.ogg", "bike_rest_base");
	audio.load("../sound/bike/rest_base02.ogg", "bike_rest_base");
	audio.load("../sound/bike/rest_base03.ogg", "bike_rest_base");
	audio.load("../sound/bike/rest_base04.ogg", "bike_rest_base");

	audio.load("../sound/bike/wheel_roll01.ogg", "wheel_roll");
	audio.load("../sound/bike/wheel_roll02.ogg", "wheel_roll");
	audio.load("../sound/bike/wheel_roll03.ogg", "wheel_roll");

	audio.load("../sound/bike/wheel_bump01.ogg", "wheel_bump");
	audio.load("../sound/bike/wheel_bump02.ogg", "wheel_bump");

	audio.load("../sound/bike/susp01.ogg", "suspension");

	/*
	audio.load("../sound/bike/suspension01.ogg", "suspension");
	audio.load("../sound/bike/suspension02.ogg", "suspension");
	audio.load("../sound/bike/suspension03.ogg", "suspension");
	audio.load("../sound/bike/suspension04.ogg", "suspension");
	audio.load("../sound/bike/suspension05.ogg", "suspension");
	audio.load("../sound/bike/suspension06.ogg", "suspension");
	*/

	game_collisions gameCollisionsSetup(*detection);
	
	{
		base_simulation.set_resource(detection.get());
		base_simulation.set_resource(&gameInput);
		base_simulation.set_resource(&audio);
		base_simulation.set_resource(camera.get());
	}
	
	const auto [back_wheel_id, front_wheel_id, head_id, bike_body_id, hand_joint_id] = game::construct_player(ecs, *application.textures(), gameCollisionsSetup.category_dynamic(), *meshes, { -100, 0, 0 });
	
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
	}
	
	gameInput.generateAndBindGameKey(gameInput.getMouseKeyPhysical(0), "menuCursorActivation");

	std::atomic<size_t> tickCounter = 0;
	std::atomic<bool> dead_lock_detector_keepalive = true;
	std::thread dead_lock_detector([&]() {
		size_t prev_tick = 994839589;
		bool detector_triggered = false;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		while (dead_lock_detector_keepalive) {
			if (!detector_triggered) {
				if (prev_tick == tickCounter) {
					scheduler.dump();
					detector_triggered = true;
				}
			}
			else if (prev_tick != tickCounter) {
				detector_triggered = false;
				std::cout << "Frame complete" << std::endl << std::endl;
			}

			prev_tick = tickCounter;
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
	});

	rynx::graphics::screenspace_draws(); // initialize gpu buffers for screenspace ops.
	rynx::application::renderer render(application, camera);
	
	render.set_lights_resolution(1.0f, 1.0f); // reduce pixels to make shit look bad
	render.light_global_ambient({ 1.0f, 1.0f, 1.0f, 0.25f });
	render.light_global_directed({ 1.0f, 1.0f, 1.0f, 0.0f }, { 1, 0 ,0 });

	auto bg_draw = std::make_unique<rynx::application::visualization::scrolling_background_2d>(application.meshRenderer(), camera, meshes->get("background"));
	auto* p_bg_draw = bg_draw.get();
	render.geometry_step_insert_front(std::move(bg_draw));

	audio.open_output_device(64, 64, rynx::sound::audio_system::format::int32);

	rynx::sound::configuration music;
	rynx::timer frame_timer_dt;
	float dt = 1.0f / 120.0f;
	
	game::create_terrain(base_simulation.m_ecs, *meshes, *application.textures(), "Empty", gameCollisionsSetup.category_static());

	auto marker_id = ecs.create(
		rynx::components::position({0.0f, -55.0f, 0.0f}, 0),
		rynx::components::radius(30.0f),
		rynx::components::motion(),
		rynx::components::ignore_gravity(),
		rynx::components::color({ 0.2f, 1.0f, 0.3f, 1.0f }),
		rynx::matrix4()
	);

	rynx::math::spline path;
	path.m_points.emplace_back(rynx::vec3f{ -800.0f, -100.0f, 0.0f });
	path.m_points.emplace_back(rynx::vec3f{ -400.0f, +100.0f, 0.0f });
	path.m_points.emplace_back(rynx::vec3f{ -350.0f, +0.0f, 0.0f });
	path.m_points.emplace_back(rynx::vec3f{ -250.0f, -100.0f, 0.0f });
	path.m_points.emplace_back(rynx::vec3f{ -450.0f, -50.0f, 0.0f });
	auto path_points = path.generate(10);

	rynx::polygon p;
	p.vertices = path_points;
	p.generateBoundary_Outside(1.0f);

	auto mesh_p = meshes->create("spline", rynx::polygon_triangulation().generate_polygon_boundary(p, application.textures()->textureLimits("Empty")), "Empty");

	ecs.create(
		rynx::components::position({}, 0.0f),
		// rynx::components::boundary({ p.generateBoundary_Outside(1.0f) }, {}, 0.0f),
		rynx::components::mesh(mesh_p),
		rynx::matrix4(),
		rynx::components::radius(p.radius()),
		rynx::components::color({ 0.2f, 1.0f, 0.3f, 1.0f })
	);

	float path_point = 0.0f;

	rynx::numeric_property<float> logic_fps;
	rynx::numeric_property<float> render_fps;

	while (!application.isExitRequested()) {
		rynx_profile("Main", "frame");
		
		{
			rynx_profile("Main", "start frame");
			application.startFrame();
		}

		camera->tick(dt * 3);
		audio.set_listener_position(camera->position())
			.set_listener_orientation(camera->local_forward(), camera->local_left(), camera->local_up());

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

		// todo, notify instead of poll.
		p_bg_draw->set_aspect_ratio(application.aspectRatio());
		
		// TODO: Move music playing to some ruleset.
		if (music.completion_rate() > 0.90f) {
			music = audio.play_sound("bg", { 0, 0, 0 }, { 0, 0, 0 }, 0.1f);
			music.set_ambient(true);
		}
		
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

		float length_of_current = (path_points[int32_t(path_point) % path_points.size()] - path_points[(int32_t(path_point) + 1) % path_points.size()]).length();

		path_point += 100.0f * dt / length_of_current;
		ecs[marker_id].get<rynx::components::position>().value =
			path_points[int32_t(path_point) % path_points.size()] * (1.0f - (path_point - int32_t(path_point))) +
			path_points[(int32_t(path_point) + 1) % path_points.size()] * (path_point - int32_t(path_point));

		// should we render or not.
		static float time_since_prev_rendered_frame = 1.0f;
		constexpr float target_fps = 120.0f;

		time_since_prev_rendered_frame += dt;
		if(time_since_prev_rendered_frame > 1.0f / target_fps)
		{
			render_fps.observe_value(1.0f / time_since_prev_rendered_frame);
			time_since_prev_rendered_frame = 0.0f;

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
				
				std::string logic_fps_text = "logic fps: " + std::to_string(int(100 * logic_fps.avg()) / 100.0f);
				application.textRenderer().drawText(logic_fps_text, -0.9f, +0.9f / application.aspectRatio(), 0.1f, Color::GREEN, rynx::TextRenderer::Align::Left, menu.fontConsola);

				std::string render_fps_text = "render fps: " + std::to_string(int(100 * render_fps.avg()) / 100.0f);
				application.textRenderer().drawText(render_fps_text, -0.9f, +0.8f / application.aspectRatio(), 0.1f, Color::GREEN, rynx::TextRenderer::Align::Left, menu.fontConsola);

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
					detection->erase(ecs, id.value, collisions.category);
				}
			}

			base_simulation.m_logic.entities_erased(*base_simulation.m_context, ids_dead);
			ecs.erase(ids_dead);
		}

		// update dt for next frame.
		dt = std::min(0.016f, std::max(0.0001f, frame_timer_dt.time_since_last_access_seconds_float()));
		logic_fps.observe_value(1.0f / dt);
	}

	dead_lock_detector_keepalive = false;
	dead_lock_detector.join();
	return 0;
}
