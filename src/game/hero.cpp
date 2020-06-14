
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

game::hero_control::hero_control(rynx::mapped_input& input) {
	key_walk_forward = input.generateAndBindGameKey('W', "walk forward");
	key_walk_back = input.generateAndBindGameKey('S', "walk back");
	key_walk_left = input.generateAndBindGameKey('A', "walk left");
	key_walk_right = input.generateAndBindGameKey('D', "walk right");
}

void game::hero_control::onFrameProcess(rynx::scheduler::context& context, float) {
	context.add_task("hero inputs", [this](
		rynx::ecs::view<rynx::components::position, rynx::components::motion, const game::hero_tag> ecs,
		rynx::mapped_input& input,
		rynx::sound::audio_system& audio,
		sound_mapper& mapper,
		rynx::camera& camera) {
		
		auto mouseScreenPos = input.mouseScreenPosition();
		auto mousecast = camera.ray_cast(mouseScreenPos.x, mouseScreenPos.y).intersect(rynx::plane(0, 0, 1, 0));
		
		if (mousecast.second) {
			lookAtWorldPos = mousecast.first;
		}
		
		ecs.query().in<game::hero_tag>().for_each([&input, this](rynx::components::position& pos, rynx::components::motion& mot) {
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

			rynx::vec3f forward = rynx::vec3f(std::cos(pos.angle), std::sin(pos.angle), 0);
			rynx::vec3f left = rynx::vec3f(-forward.y, forward.x, 0);
			if (input.isKeyDown(key_walk_forward)) {
				mot.acceleration +=  forward * 500;
			}
			if (input.isKeyDown(key_walk_back)) {
				mot.acceleration += forward * -200;
			}
			if (input.isKeyDown(key_walk_left)) {
				mot.acceleration += left * 350;
			}
			if (input.isKeyDown(key_walk_right)) {
				mot.acceleration += left * -350;
			}
		});
	});
}
