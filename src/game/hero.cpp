
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
		rynx::ecs::view<rynx::components::position, rynx::components::motion, rynx::components::phys::joint, const rynx::components::physical_body, const game::hero_tag> ecs,
		rynx::mapped_input& input,
		rynx::sound::audio_system& audio,
		rynx::camera& camera,
		rynx::scheduler::task& task_context)
	{
		auto mouseScreenPos = input.mouseScreenPosition();
		auto mousecast = camera.ray_cast(mouseScreenPos.x, mouseScreenPos.y).intersect(rynx::plane(0, 0, 1, 0));

		if (mousecast.second) {
			lookAtWorldPos = mousecast.first;
		}

		if (ecs.exists(bike_body_id)) {
			auto entity = ecs[bike_body_id];
			const auto* mot = entity.try_get<const rynx::components::motion>();
			const auto* pos = entity.try_get<const rynx::components::position>();
			camera.setPosition(pos->value + mot->velocity * 1.0f + rynx::vec3f{0, 0, 750});
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
				
				const float max_speed = 75;
				const float max_acceleration = 200;
				if (mot) {
					mot->angularAcceleration = -max_acceleration * (max_speed + mot->angularVelocity) / max_speed;
				}
			}
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

/*
bool hero_relative_controls = false;
void game::hero_control::onFrameProcess(rynx::scheduler::context& context, float) {
	context.add_task("hero inputs", [this](
		rynx::ecs::view<rynx::components::position, rynx::components::motion, const rynx::components::physical_body, const game::hero_tag> ecs,
		rynx::mapped_input& input,
		rynx::sound::audio_system& audio,
		sound_mapper& mapper,
		rynx::camera& camera,
		rynx::scheduler::task& task_context) {
		
		auto mouseScreenPos = input.mouseScreenPosition();
		auto mousecast = camera.ray_cast(mouseScreenPos.x, mouseScreenPos.y).intersect(rynx::plane(0, 0, 1, 0));
		
		if (mousecast.second) {
			lookAtWorldPos = mousecast.first;
		}
		
		ecs.query().in<game::hero_tag>().for_each([&input, &task_context, &ecs, this](rynx::ecs::id hero_id, rynx::components::position& pos, rynx::components::motion& mot) {
			auto dstForward = (lookAtWorldPos - pos.value);
			auto dstAngle = std::atan2f(dstForward.y, dstForward.x);
			float dAngle = (dstAngle - pos.angle);

			while (dAngle > rynx::math::pi) {
				dAngle -= 2 * rynx::math::pi;
				pos.angle += 2 * rynx::math::pi;
			}
			
			while (dAngle < -rynx::math::pi) {
				dAngle += 2 * rynx::math::pi;
				pos.angle -= 2 * rynx::math::pi;
			}

			float agr = dAngle - mot.angularVelocity * 0.01f;
			mot.angularAcceleration += agr * 250;

			rynx::vec3f forward(0, 1, 0);
			rynx::vec3f left(-1, 0, 0);
			rynx::vec3f hero_local_forward = rynx::vec3f(std::cos(pos.angle), std::sin(pos.angle), 0);

			float up_mul = (hero_local_forward.dot(forward) + 1) * 200 + 150;
			float down_mul = (-hero_local_forward.dot(forward) + 1) * 200 + 150;
			float left_mul = (hero_local_forward.dot(left) + 1) * 200 + 150;
			float right_mul = (-hero_local_forward.dot(left) + 1) * 200 + 150;

			if (hero_relative_controls) {
				forward = hero_local_forward;
				left = rynx::vec3f(-forward.y, forward.x, 0);

				// in relative controls, up means "go directly forward", and this means the multipliers can be constant.
				up_mul = 500; // forward
				down_mul = 200; // backward
				left_mul = 350; // left
				right_mul = 350; // right
			}

			if (input.isKeyDown(key_walk_forward)) {
				mot.acceleration +=  forward * up_mul;
			}
			if (input.isKeyDown(key_walk_back)) {
				mot.acceleration += -forward * down_mul;
			}
			if (input.isKeyDown(key_walk_left)) {
				mot.acceleration += left * left_mul;
			}
			if (input.isKeyDown(key_walk_right)) {
				mot.acceleration += -left * right_mul;
			}

			if (input.isKeyDown(key_shoot)) {
				const auto& hero_body = ecs[hero_id].get<const rynx::components::physical_body>();
				task_context.extend_task_independent([hero_local_forward, pos, hero_collision_id = hero_body.collision_id](rynx::ecs& ecs) {
					ecs.create(
						rynx::components::projectile(),
						rynx::components::position(pos),
						rynx::components::lifetime(1.0f),
						rynx::components::radius(1.0f),
						rynx::components::physical_body(1.0f, 1.0f, 0.0f, 1.0f, hero_collision_id),
						rynx::components::motion(hero_local_forward * 500, 0),
						rynx::components::color({1, 1, 1, 1}),
						rynx::matrix4()
					);
				});
			}
		});
	});
}
*/