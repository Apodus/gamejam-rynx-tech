
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

game::hero_control::hero_control(rynx::mapped_input& input) {
	key_walk_forward = input.generateAndBindGameKey('W', "walk forward");
	key_walk_back = input.generateAndBindGameKey('S', "walk back");
	key_walk_left = input.generateAndBindGameKey('A', "walk left");
	key_walk_right = input.generateAndBindGameKey('D', "walk right");

	key_construct = input.generateAndBindGameKey('E', "construct");
	key_shoot = input.generateAndBindGameKey(input.getMouseKeyPhysical(0), "shoot");
}

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
