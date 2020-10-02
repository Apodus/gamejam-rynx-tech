
#pragma once

#include <rynx/application/logic.hpp>
#include <rynx/math/vector.hpp>
#include <rynx/audio/audio.hpp>

namespace game {
	class hero_control : public rynx::application::logic::iruleset {
	
		int32_t key_throw_torch = -1;
		int32_t key_shoot = -1;
		int32_t key_reload = -1;
		int32_t key_construct = -1;

		int32_t key_walk_forward = -1;
		int32_t key_walk_left = -1;
		int32_t key_walk_right = -1;
		int32_t key_walk_back = -1;

		rynx::ecs::id engine_wheel_id;
		rynx::ecs::id break_wheel_id;
		rynx::ecs::id head_id;
		rynx::ecs::id bike_body_id;
		rynx::ecs::id hand_joint_id;

		rynx::vec3f lookAtWorldPos;

		float engine_activity_slide = 0.0f;
		float engine_acceleration_state = 0.0f;

		rynx::sound::configuration bike_rest;
		rynx::sound::configuration bike_constant_bg;

		rynx::sound::configuration back_suspension;

		rynx::sound::configuration wheel_roll_sound;
		rynx::sound::configuration wheel_crash_sound;

	public:
		hero_control(rynx::mapped_input& input, rynx::ecs::id back_wheel, rynx::ecs::id front_wheel, rynx::ecs::id head, rynx::ecs::id bike_body, rynx::ecs::id hand_joint_id);
		virtual ~hero_control() = default;
		virtual void onFrameProcess(rynx::scheduler::context& context, float dt) override;
	};
}
