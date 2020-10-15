
#include <rynx/math/vector.hpp>
#include <rynx/graphics/mesh/shape.hpp>
#include <rynx/graphics/texture/texturehandler.hpp>
#include <rynx/math/geometry/polygon_triangulation.hpp>
#include <rynx/graphics/renderer/meshrenderer.hpp>
#include <rynx/tech/collision_detection.hpp>

#include <rynx/tech/components.hpp>
#include <rynx/application/components.hpp>

namespace game {
	namespace components {
		struct suspension { float prev_length = 0.0f; };
	}

	// construct hero object.
	auto construct_player(rynx::ecs& ecs, rynx::graphics::GPUTextures& textures, rynx::collision_detection::category_id dynamicCollisions, rynx::mesh_collection& meshes, rynx::vec3f pos)
	{
		auto poly = rynx::Shape::makeBox(15.0f);

		{
			auto mesh = rynx::polygon_triangulation().make_boundary_mesh(poly, textures.textureLimits("Empty"), 3.0f);
			mesh->build();
			meshes.create("hero_mesh", std::move(mesh), "Empty");
		}

		float angle = 0;
		float radius = poly.radius();

		float wheel_radius_scale = 1.3f;
		float head_radius_scale = 1.0f;

		rynx::components::light_omni light;
		light.ambient = 0.3f;
		light.attenuation_linear = 1.5f;
		light.attenuation_quadratic = 0.005f;
		light.color = { 1.0f, 1.0f, 1.0f, 5.0f };

		auto head_id = ecs.create(
			light,
			rynx::components::position(pos, angle),
			rynx::components::collisions{ dynamicCollisions.value },
			rynx::components::boundary({ poly.generateBoundary_Outside(head_radius_scale) }, pos, angle),
			rynx::components::mesh(meshes.get("head")),
			rynx::matrix4(),
			rynx::components::radius(radius * head_radius_scale),
			rynx::components::color({ 1.0f, 1.0f, 1.0f, 1.0f }),
			rynx::components::motion({ 0, 0, 0 }, 0),
			rynx::components::physical_body().mass(150).elasticity(0.0f).friction(1.0f).moment_of_inertia(poly),
			rynx::components::dampening{ 0.05f, 0.05f },
			rynx::components::collision_custom_reaction{}
		);

		auto wheel_shape = rynx::Shape::makeCircle(radius * wheel_radius_scale, 50);

		auto back_wheel_id = ecs.create(
			rynx::components::position(pos + rynx::vec3f(-22, -40, 0), angle),
			rynx::components::collisions{ dynamicCollisions.value },
			rynx::components::mesh(meshes.get("wheel")),
			rynx::matrix4(),
			rynx::components::radius(radius * wheel_radius_scale),
			rynx::components::color({ 1.0f, 1.0f, 1.0f, 1.0f }),
			rynx::components::motion({ 0, 0, 0 }, -10),
			rynx::components::physical_body().mass(50).elasticity(0.0f).friction(30.0f).moment_of_inertia(wheel_shape),
			rynx::components::dampening{ 0.05f, 0.05f },
			rynx::components::collision_custom_reaction()
		);

		auto front_wheel_id = ecs.create(
			rynx::components::position(pos + rynx::vec3f(+27, -40, 0), angle),
			rynx::components::collisions{ dynamicCollisions.value },
			rynx::components::mesh(meshes.get("wheel")),
			rynx::matrix4(),
			rynx::components::radius(radius * wheel_radius_scale),
			rynx::components::color({ 1.0f, 1.0f, 1.0f, 1.0f }),
			rynx::components::motion({ 0, 0, 0 }, 0),
			rynx::components::physical_body().mass(50).elasticity(0.0f).friction(10.0f).moment_of_inertia(wheel_shape),
			rynx::components::dampening{ 0.05f, 0.05f },
			rynx::components::collision_custom_reaction()
		);

		rynx::components::light_directed bike_light;
		bike_light.ambient = 0;
		bike_light.angle = 1.55f;
		bike_light.attenuation_linear = 1.5f;
		bike_light.attenuation_quadratic = 0.001f;
		bike_light.direction = { 1.0f, 0.0f, 0.0f };
		bike_light.edge_softness = 0.3f;
		bike_light.color = { 1.0f, 0.6f, 0.2f, 50.0f };

		rynx::components::particle_emitter emitter;
		emitter.constant_force = { {0, 5, 0}, {0, 10, 0} };
		emitter.end_radius = { 0.0f, 0.0f };
		emitter.start_radius = { 5.0f, 8.0f };
		emitter.initial_angle = { rynx::math::pi - 0.45f, rynx::math::pi + 0.45f };
		emitter.initial_velocity = { 40.0f, 80.0f };
		emitter.linear_dampening = { 0.2f, 0.6f };
		emitter.position_offset = { -30.0f, 0.0f, 0.0f };
		emitter.lifetime_range = { 0.2f, 0.6f };
		emitter.rotate_with_host = true;
		emitter.spawn_rate = { 0.01f, 0.02f };
		emitter.start_color = { {0.2f, 0.2f, 0.2f, 0.5f}, {0.4f, 0.4f, 0.4f, 0.5f} };
		emitter.end_color = { {0.8f, 0.8f, 0.8f, 0.0f}, {0.9f, 0.9f, 0.9f, 0.0f} };
		emitter.time_until_next_spawn = 0.0f;

		auto bike_body_id = ecs.create(
			bike_light,
			emitter,
			rynx::components::position(pos + rynx::vec3f(+0, -25, 0), angle),
			// rynx::components::collisions{ gameCollisionsSetup.category_dynamic().value },
			rynx::components::mesh(meshes.get("bike_body")),
			rynx::matrix4(),
			rynx::components::radius(radius * 3.3f),
			rynx::components::color({ 1.0f, 1.0f, 1.0f, 1.0f }),
			rynx::components::motion({ 0, 0, 0 }, 0),
			rynx::components::physical_body().mass(650).elasticity(0.0f).friction(1.0f).moment_of_inertia(poly),
			rynx::components::dampening{ 0.05f, 0.05f }
		);

		auto connect_wheel_to_body = [&ecs](rynx::ecs::id wheel_id, rynx::ecs::id body_id, float strength, float softness, float response_time, rynx::vec3f pos, rynx::vec3f pos2 = {}) {
			rynx::components::phys::joint joint;
			joint.connect_with_spring().rotation_free();
			joint.id_a = wheel_id;
			joint.id_b = body_id;
			joint.point_a = pos2;
			joint.point_b = pos;
			joint.strength = strength;
			joint.softness = softness;
			joint.response_time = response_time;
			joint.length = rynx::components::phys::compute_current_joint_length(joint, ecs);
			return ecs.create(joint, rynx::components::invisible());
		};

		constexpr float fix_velocity = 0.55f;
		constexpr float frontback_joints_strength = 0.04f;
		constexpr float bike_joints_strength = 0.1005f;
		constexpr float front_wheel_joint_mul = 0.75f;

		ecs.attachToEntity(connect_wheel_to_body(back_wheel_id, bike_body_id, frontback_joints_strength, 3.0f, fix_velocity, { -45, +13, 0 }), game::components::suspension());
		ecs.attachToEntity(connect_wheel_to_body(back_wheel_id, bike_body_id, frontback_joints_strength, 3.0f, fix_velocity, { +15, -25, 0 }), game::components::suspension());

		ecs.attachToEntity(connect_wheel_to_body(front_wheel_id, bike_body_id, frontback_joints_strength * front_wheel_joint_mul, 3.0f, fix_velocity, { +52, +15, 0 }), game::components::suspension());
		ecs.attachToEntity(connect_wheel_to_body(front_wheel_id, bike_body_id, frontback_joints_strength * front_wheel_joint_mul, 3.0f, fix_velocity, { -15, -25, 0 }), game::components::suspension());

		ecs.attachToEntity(connect_wheel_to_body(back_wheel_id, bike_body_id, bike_joints_strength, 3.0f, fix_velocity, { +19, +27, 0 }), game::components::suspension());
		ecs.attachToEntity(connect_wheel_to_body(front_wheel_id, bike_body_id, bike_joints_strength * front_wheel_joint_mul, 3.0f, fix_velocity, { -10, +27, 0 }), game::components::suspension());

		// connect rider to bike body or something.
		connect_wheel_to_body(head_id, bike_body_id, 0.9f, 3.0f, +0.056f, { -5, 0, 0 }, { -5, 0, 0 });
		connect_wheel_to_body(head_id, bike_body_id, 0.9f, 1.0f, +0.056f, { +5, 0, 0 }, { +5, 0, 0 });
		connect_wheel_to_body(head_id, bike_body_id, 0.9f, 3.0f, +0.056f, { 0, 0, 0 }, { 0, 0, 0 });
		auto hand_joint_id = connect_wheel_to_body(head_id, bike_body_id, 1.2f, 2.0f, +0.016f, { +22, +10, 0 }, { +5, 0, 0 }); // hand to steering.

		// attach_fire_to(ecs, back_wheel_id);
		// attach_fire_to(ecs, front_wheel_id);

		return std::make_tuple(back_wheel_id, front_wheel_id, head_id, bike_body_id, hand_joint_id);
	};
}