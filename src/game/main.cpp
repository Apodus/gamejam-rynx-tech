
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

	sound_mapper sounds;
	sounds.insert("boogie", audio.load("../sound/music/boogie01.ogg"));
	sounds.insert("boogie", audio.load("../sound/music/boogie02.ogg"));
	sounds.insert("boogie", audio.load("../sound/music/boogie03.ogg"));
	sounds.insert("bg", audio.load("../sound/music/bg01.ogg"));
	sounds.insert("bg", audio.load("../sound/music/bg02.ogg"));
	sounds.insert("bg", audio.load("../sound/music/bg03.ogg"));
	sounds.insert("bg", audio.load("../sound/music/bg04.ogg"));
	sounds.insert("tune", audio.load("../sound/music/tune1.ogg"));

	game_collisions gameCollisionsSetup(*detection);
	
	{
		base_simulation.set_resource(detection.get());
		base_simulation.set_resource(&gameInput);
		base_simulation.set_resource(&audio);
		base_simulation.set_resource(camera.get());
		base_simulation.set_resource(&sounds);
	}

	// setup game logic
	{
		auto ruleset_collisionDetection = base_simulation.create_rule_set<rynx::ruleset::physics_2d>();
		auto ruleset_particle_update = base_simulation.create_rule_set<rynx::ruleset::particle_system>();
		auto ruleset_frustum_culling = base_simulation.create_rule_set<rynx::ruleset::frustum_culling>(camera);
		auto ruleset_motion_updates = base_simulation.create_rule_set<rynx::ruleset::motion_updates>(rynx::vec3<float>(0, -160.8f, 0));
		auto ruleset_physical_springs = base_simulation.create_rule_set<rynx::ruleset::physics::springs>();
		auto ruleset_lifetime_updates = base_simulation.create_rule_set<rynx::ruleset::lifetime_updates>();
		auto ruleset_debug_input = base_simulation.create_rule_set<debug_input>(gameInput);
		auto ruleset_hero_inputs = base_simulation.create_rule_set<game::hero_control>(gameInput);

		ruleset_physical_springs->depends_on(ruleset_motion_updates);
		ruleset_collisionDetection->depends_on(ruleset_motion_updates);
		ruleset_frustum_culling->depends_on(ruleset_motion_updates);
	}
	
	// plop(ecs, *meshes, *application.textures(), gameCollisionsSetup.category_dynamic());
	
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
			music = audio.play_sound(sounds.get("bg"), { 0, 0, 0 }, { 0, 0, 0 }, 1.0f);
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
