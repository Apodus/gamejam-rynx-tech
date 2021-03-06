
#include <rynx/math/geometry/polygon.hpp>
#include <rynx/graphics/mesh/mesh.hpp>
#include <rynx/graphics/texture/texturehandler.hpp>
#include <rynx/tech/ecs.hpp>
#include <rynx/graphics/renderer/meshrenderer.hpp>
#include <rynx/tech/components.hpp>
#include <rynx/tech/collision_detection.hpp>
#include <rynx/application/components.hpp>

#include <string>
#include <memory>

namespace game {
	rynx::ecs::id create_terrain(rynx::ecs& ecs, rynx::graphics::mesh_collection& meshes, rynx::graphics::GPUTextures& textures, std::string terrainTexture, rynx::collision_detection::category_id terrainCollisionCategory) {

		std::string mesh_name("terrain");


		rynx::polygon p;
		auto editor = p.edit();
		rynx::polygon terrain_surface;
		{
			float x_value = -1000.0f;
			while (x_value < +5000) {
				float y_value =
					250.0f * std::sin(x_value * 0.0017f) +
					110.0f * std::sin(x_value * 0.0073f) +
					50.0f * std::sin(x_value * 0.013f) - 100.0f;
				editor.push_back({ x_value, y_value, 0.0f });
				x_value += 10.0f;
			}

			terrain_surface = p;
			terrain_surface.invert();

			editor.emplace_back(rynx::vec3f{ +5100.0f, -1000.0f, 0.0f });
			editor.emplace_back(rynx::vec3f{ -600.0f, -1000.0f, 0.0f });
			editor.reverse();
		}

		terrain_surface.scale(1.0f / p.radius());
		
		std::unique_ptr<rynx::graphics::mesh> m = std::make_unique<rynx::graphics::mesh>();
		auto uv_limits = textures.textureLimits(terrainTexture);

		for (int32_t i = 0; i < terrain_surface.size(); ++i) {
			const auto v = terrain_surface.vertex_position(i);
			m->vertices.emplace_back(v.x);
			m->vertices.emplace_back(v.y);
			m->vertices.emplace_back(v.z);

			m->vertices.emplace_back(v.x);
			m->vertices.emplace_back(-1000.0f);
			m->vertices.emplace_back(v.z);

			auto normal = terrain_surface.vertex_normal(i);
			
			m->normals.emplace_back(normal.x);
			m->normals.emplace_back(normal.y);
			m->normals.emplace_back(normal.z);

			m->normals.emplace_back(0.0f);
			m->normals.emplace_back(-1.0f);
			m->normals.emplace_back(0.0f);

			if (i > 1) {
				m->indices.emplace_back(static_cast<short>(2 * i - 3));
				m->indices.emplace_back(static_cast<short>(2 * i));
				m->indices.emplace_back(static_cast<short>(2 * i - 1));

				m->indices.emplace_back(static_cast<short>(2 * i));
				m->indices.emplace_back(static_cast<short>(2 * i - 3));
				m->indices.emplace_back(static_cast<short>(2 * i - 2));
			}

			// UVs are total bullshit. TODO: fix.
			m->texCoords.emplace_back(uv_limits.x);
			m->texCoords.emplace_back(uv_limits.y);
			m->texCoords.emplace_back(uv_limits.z);
			m->texCoords.emplace_back(uv_limits.w);
		}

		auto* mesh_p = meshes.create(mesh_name, std::move(m), "Empty");
		float radius = p.radius();

		return ecs.create(
			rynx::components::position({}, 0.0f),
			rynx::components::collisions{ terrainCollisionCategory.value },
			rynx::components::boundary(p, {}, 0.0f),
			rynx::components::mesh(mesh_p),
			rynx::matrix4(),
			rynx::components::radius(radius),
			rynx::components::color({ 0.2f, 1.0f, 0.3f, 1.0f }),
			rynx::components::physical_body()
				.mass(std::numeric_limits<float>::max())
				.friction(1.0f)
				.elasticity(0.0f)
				.moment_of_inertia(std::numeric_limits<float>::max())
				.bias(2.0f),
			rynx::components::ignore_gravity(),
			rynx::components::dampening{ 0.50f, 1.0f }
		);
	};
}
