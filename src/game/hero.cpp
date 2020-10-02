
#include <game/components.hpp>
#include <game/hero.hpp>

#include <rynx/scheduler/context.hpp>
#include <rynx/input/mapped_input.hpp>
#include <rynx/tech/components.hpp>

#include <rynx/audio/audio.hpp>
#include <game/menu.hpp>
#include <rynx/graphics/camera/camera.hpp>
#include <rynx/math/geometry/ray.hpp>
#include <rynx/math/geometry/plane.hpp>
#include <rynx/math/matrix.hpp>

namespace game {
	namespace components {
		struct suspension { float prev_length = 0.0f; };
	}
}

game::hero_control::hero_control(rynx::mapped_input& input, rynx::ecs::id back_wheel, rynx::ecs::id front_wheel, rynx::ecs::id head, rynx::ecs::id bike_body, rynx::ecs::id hand_joint_id) {
	key_walk_forward = input.generateAndBindGameKey('W', "walk forward");
	key_walk_back = input.generateAndBindGameKey('S', "walk back");
	key_walk_left = input.generateAndBindGameKey('A', "walk left");
	key_walk_right = input.generateAndBindGameKey('D', "walk right");

	key_construct = input.generateAndBindGameKey('E', "construct");
	key_shoot = input.generateAndBindGameKey(input.getMouseKeyPhysical(0), "shoot");

	engine_wheel_id = back_wheel;
	break_wheel_id = front_wheel;
	head_id = head;
	bike_body_id = bike_body;
	this->hand_joint_id = hand_joint_id;
}

void game::hero_control::onFrameProcess(rynx::scheduler::context& context, float dt) {
	context.add_task("hero inputs", [this, dt](
		rynx::ecs::view<const rynx::components::collision_custom_reaction, game::components::suspension, const rynx::components::phys::joint, rynx::components::particle_emitter, rynx::components::position, rynx::components::motion, rynx::components::phys::joint, const rynx::components::physical_body, const game::hero_tag> ecs,
		rynx::mapped_input& input,
		rynx::sound::audio_system& audio,
		rynx::camera& camera,
		rynx::scheduler::task& task_context)
	{
		constexpr float max_speed = 75;
		constexpr float max_acceleration = 400;

		auto mouseScreenPos = input.mouseScreenPosition();
		auto mousecast = camera.ray_cast(mouseScreenPos.x, mouseScreenPos.y).intersect(rynx::plane(0, 0, 1, 0));

		if (mousecast.second) {
			lookAtWorldPos = mousecast.first;
		}

		float total_suspension_velocity = 0.0f;
		ecs.query().for_each([&](game::components::suspension& suspension, const rynx::components::phys::joint& j) {
			float current_length = rynx::components::phys::compute_current_joint_length(j, ecs);
			total_suspension_velocity += current_length - j.length;
			suspension.prev_length = current_length;
		});
		total_suspension_velocity *= 0.10f;

		if (ecs.exists(bike_body_id)) {
			auto entity = ecs[bike_body_id];
			const auto& body_pos = ecs[bike_body_id].get<const rynx::components::position>();
			const auto* mot = entity.try_get<const rynx::components::motion>();
			camera.setPosition(body_pos.value + mot->velocity * 1.0f + rynx::vec3f{0, 0, 750});

			if (bike_constant_bg.completion_rate() > 0.8f) {
				bike_constant_bg = audio.play_sound("bike_rest", body_pos.value, {}, 0.4f);
			}

			if (bike_rest.completion_rate() > 0.9173f - engine_activity_slide * 0.503913f) {
				bike_rest = audio.play_sound("bike_rest_base", body_pos.value, {}, 1.0f);
				bike_rest.set_pitch_shift(engine_activity_slide * 0.3f - 0.15f);
				bike_rest.set_loudness(engine_activity_slide * 0.6f + 0.25f);
				bike_rest.set_tempo_shift(0.9f + engine_activity_slide * 0.2f);
			}

			if (back_suspension.completion_rate() > 0.9f) {
				if (total_suspension_velocity * total_suspension_velocity > 1.0f) {
					float v = total_suspension_velocity* total_suspension_velocity - 1.0f;
					back_suspension = audio.play_sound("suspension", body_pos.value, {}, 1.0f);
					back_suspension.set_loudness(std::min(1.0f, v));
					back_suspension.set_tempo_shift(1.0f + std::clamp(v -0.3f, -0.3f, +0.3f));
				}
			}

			ecs.query().for_each([&](const rynx::components::collision_custom_reaction& collisions, const rynx::components::motion& my_motion) {
				for (auto&& event : collisions.events) {

					float wheel_roll_mul = (2.0f - event.relative_velocity.length()) * my_motion.velocity.length() / (4.0f * max_speed);
					if (wheel_roll_mul > 0) {
						wheel_roll_mul = std::clamp(wheel_roll_mul, 0.0f, 1.0f);
						if (wheel_roll_sound.completion_rate() > 0.9f) {
							wheel_roll_sound = audio.play_sound("wheel_roll", body_pos.value, {}, wheel_roll_mul);
							wheel_roll_sound.set_tempo_shift(1.0f + (wheel_roll_mul - 0.5f) * 0.5f);
						}
					}

					float wheel_collision_mul = event.relative_velocity.length();
					if (wheel_collision_mul > 100) {
						if (wheel_crash_sound.completion_rate() > 0.1f) {
							wheel_crash_sound = audio.play_sound("wheel_bump", body_pos.value, {}, std::clamp(wheel_collision_mul / 100.0f - 0.9f, 0.0f, 1.2f));
						}
					}
				}
			});

			auto& emitter = entity.get<rynx::components::particle_emitter>();
			emitter.spawn_rate = {200 * engine_activity_slide + 10, 500 * engine_activity_slide + 20 };
			emitter.start_radius = {2.0f + 4 * engine_acceleration_state * engine_activity_slide, 5.0f + 5 * engine_activity_slide * engine_activity_slide};
			emitter.initial_velocity = {40.0f * (1.0f + engine_acceleration_state), 80.0f * ( 1.0f + engine_acceleration_state) };
		}

		if (input.isKeyClicked(key_construct)) {
			rynx::vec3f pos;

			auto reset_entity_to = [&](rynx::ecs::id entity_id, rynx::vec3f local_offset)
			{
				auto entity = ecs[entity_id];
				auto* bike_mot = entity.try_get<rynx::components::motion>();
				auto* bike_pos = entity.try_get<rynx::components::position>();
				*bike_pos = rynx::components::position(pos + local_offset, 0);
				bike_mot->angularVelocity = 0;
				bike_mot->angularAcceleration = 0;
				bike_mot->velocity = {};
				bike_mot->acceleration = {};
			};

			reset_entity_to(bike_body_id, rynx::vec3f(+0, -25, 0));
			reset_entity_to(head_id, rynx::vec3f(0, 0, 0));
			reset_entity_to(engine_wheel_id, rynx::vec3f(-22, -40, 0));
			reset_entity_to(break_wheel_id, rynx::vec3f(+25, -40, 0));

			auto hand_joint = ecs[hand_joint_id];
			rynx::components::phys::joint& j = hand_joint.get<rynx::components::phys::joint>();
			j.length = 20.0f;
		}

		if (input.isKeyDown(key_walk_forward)) {
			if (ecs.exists(engine_wheel_id)) {
				auto entity = ecs[engine_wheel_id];
				auto* mot = entity.try_get<rynx::components::motion>();
				
				if (mot) {
					mot->angularAcceleration = -max_acceleration * (max_speed + mot->angularVelocity) / max_speed;
				}

				engine_activity_slide += (engine_activity_slide < 1.0f) ? dt * 2.0f : 0.0f;
			}
		}
		else {
			engine_activity_slide -= (engine_activity_slide > dt) ? dt * 0.5f: 0.0f;
		}
		
		if (input.isKeyDown(key_walk_back)) {
			if (ecs.exists(break_wheel_id)) {
				auto entity = ecs[break_wheel_id];
				auto* mot = entity.try_get<rynx::components::motion>();
				mot->angularAcceleration = -mot->angularVelocity * 15.0f + 50.0f;
			}

			if (ecs.exists(engine_wheel_id)) {
				auto entity = ecs[engine_wheel_id];
				auto* mot = entity.try_get<rynx::components::motion>();
				mot->angularAcceleration = -mot->angularVelocity * 15.0f + 50.0f;
			}
		}
		
		if (input.isKeyDown(key_walk_left) || input.isKeyDown(key_walk_right)) {
			if (ecs.exists(head_id) & ecs.exists(bike_body_id)) {
				float mul = 0;
				if (input.isKeyDown(key_walk_left)) {
					mul -= 1.0f;
				}
				if (input.isKeyDown(key_walk_right)) {
					mul += 1.0f;
				}

				// take care of leaning
				{
					auto hand_joint = ecs[hand_joint_id];
					rynx::components::phys::joint& j = hand_joint.get<rynx::components::phys::joint>();
					j.length *= 1.0f - mul * dt * 2;
					j.length = std::clamp(j.length, 12.0f, 25.0f);

					// create fake forces to allow easier control of the rotation of the bike.
					if (j.length <= 12.05f || j.length >= 24.95f) {
						{
							auto entity = ecs[head_id];
							auto bike_chassis_entity = ecs[bike_body_id];
							auto* mot = entity.try_get<rynx::components::motion>();
							const auto* pos = entity.try_get<const rynx::components::position>();
							auto* bike_mot = bike_chassis_entity.try_get<rynx::components::motion>();

							constexpr float fake_force_strength = 200;
							if (mot && pos) {
								float x = std::cos(pos->angle) * mul;
								float y = std::sin(pos->angle) * mul;

								x *= fake_force_strength;
								y *= fake_force_strength;

								// todo: take a look at masses of the objects to balance it out.
								mot->acceleration += { x, y, 0};
								bike_mot->acceleration -= { x * 0.33f, y * 0.33f, 0};
							}
						}
					}
				}
			}
		}
	});
}
