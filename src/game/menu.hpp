
#include <rynx/menu/Div.hpp>
#include <rynx/menu/Button.hpp>
#include <rynx/menu/Slider.hpp>
#include <rynx/math/vector.hpp>

#include <rynx/tech/profiling.hpp>
#include <rynx/graphics/renderer/meshrenderer.hpp>
#include <rynx/tech/collision_detection.hpp>
#include <rynx/graphics/mesh/shape.hpp>
#include <rynx/application/components.hpp>
#include <rynx/graphics/framebuffer.hpp>
#include <rynx/graphics/text/font.hpp>
#include <rynx/graphics/text/fontdata/consolamono.hpp>
#include <rynx/graphics/text/fontdata/lenka.hpp>

#include <rynx/math/geometry/ray.hpp>
#include <rynx/math/geometry/plane.hpp>

class editor_rules : public rynx::application::logic::iruleset {
	rynx::binary_config::id m_editor_state;
	rynx::binary_config::id m_game_state;

	rynx::key::logical key_createPolygon;
	rynx::key::logical key_createBox;

	int32_t m_selected_vertex = 0;
	
	struct edited_entity {
		rynx::ecs::id m_id;
		rynx::floats4 m_original_color;
	};

	edited_entity m_selected_entity;

	bool dragging_polygon_vertex = false;

	rynx::collision_detection::category_id m_static_collisions;
	rynx::collision_detection::category_id m_dynamic_collisions;
public:
	editor_rules(rynx::mapped_input& gameInput, rynx::collision_detection::category_id dynamic_collisions, rynx::collision_detection::category_id static_collisions, rynx::binary_config::id game_state, rynx::binary_config::id editor_state) {
		key_createPolygon = gameInput.generateAndBindGameKey({ 'P' }, "Create polygon");
		key_createBox = gameInput.generateAndBindGameKey({ 'O' }, "Create box");

		m_editor_state = editor_state;
		m_game_state = game_state;
		m_static_collisions = static_collisions;
		m_dynamic_collisions = dynamic_collisions;
	}

private:
	virtual void onFrameProcess(rynx::scheduler::context& context, float dt) override {
		context.add_task("editor tick", [this, dt](
			rynx::ecs& game_ecs,
			rynx::mapped_input& gameInput,
			rynx::camera& gameCamera)
			{
				auto mouseRay = gameInput.mouseRay(gameCamera);
				auto mouse_z_plane = mouseRay.intersect(rynx::plane(0, 0, 1, 0));
				mouse_z_plane.first.z = 0;

				if (mouse_z_plane.second) {
					if (gameInput.isKeyClicked(key_createPolygon)) {
						auto p = rynx::Shape::makeTriangle(50.0f);
						game_ecs.create(
							rynx::components::position(mouse_z_plane.first, 0.0f),
							rynx::components::collisions{ m_static_collisions.value },
							rynx::components::boundary({ p.generateBoundary_Outside(1.0f) }, mouse_z_plane.first, 0.0f),
							rynx::components::radius(p.radius()),
							rynx::components::color({ 0.2f, 1.0f, 0.3f, 1.0f }),
							rynx::components::physical_body().mass(std::numeric_limits<float>::max()).friction(1.0f).elasticity(0.0f).moment_of_inertia(std::numeric_limits<float>::max()),
							rynx::components::ignore_gravity(),
							rynx::components::dampening{ 0.50f, 1.0f }
						);
					}

					if (gameInput.isKeyClicked(key_createBox)) {
						auto p = rynx::Shape::makeBox(20.0f);
						game_ecs.create(
							rynx::components::position(mouse_z_plane.first, 0.0f),
							rynx::components::motion{},
							rynx::components::collisions{ m_dynamic_collisions.value },
							rynx::components::boundary({ p.generateBoundary_Outside(1.0f) }, mouse_z_plane.first, 0.0f),
							rynx::components::radius(p.radius()),
							rynx::components::color({ 0.2f, 1.0f, 0.3f, 1.0f }),
							rynx::components::physical_body().mass(550.0f).friction(1.0f).elasticity(0.0f).moment_of_inertia(p, 2.0f),
							rynx::matrix4{}
						);
					}
				}
			});
	}
};

class debug_input : public rynx::application::logic::iruleset {
	rynx::key::logical zoomOut;
	rynx::key::logical zoomIn;

	rynx::key::logical camera_orientation_key;
	rynx::key::logical cameraUp;
	rynx::key::logical cameraLeft;
	rynx::key::logical cameraRight;
	rynx::key::logical cameraDown;

	rynx::key::logical yawCounterClockWise;
	rynx::key::logical yawClockWise;

	rynx::key::logical key_toggleGameState;
	rynx::key::logical key_toggleEditorState;
	rynx::key::logical key_toggleFrustumCullState;

	rynx::binary_config::id m_editor_state;
	rynx::binary_config::id m_game_state;
	rynx::binary_config::id m_frustum_cull_state;

public:
	debug_input(rynx::mapped_input& gameInput, rynx::binary_config::id editor_state, rynx::binary_config::id game_state, rynx::binary_config::id frustum_cull_update_state) {
		zoomOut = gameInput.generateAndBindGameKey('1', "zoom out");
		zoomIn = gameInput.generateAndBindGameKey('2', "zoom in");

		camera_orientation_key = gameInput.generateAndBindGameKey(gameInput.getMouseKeyPhysical(1), "camera_orientation");
		cameraUp = gameInput.generateAndBindGameKey('W', "cameraUp");
		cameraLeft = gameInput.generateAndBindGameKey('A', "cameraLeft");
		cameraRight = gameInput.generateAndBindGameKey('D', "cameraRight");
		cameraDown = gameInput.generateAndBindGameKey('S', "cameraDown");

		key_toggleGameState = gameInput.generateAndBindGameKey('1', "stateGame");
		key_toggleEditorState = gameInput.generateAndBindGameKey('2', "stateEditor");
		key_toggleFrustumCullState = gameInput.generateAndBindGameKey('3', "stateFrustum");

		yawCounterClockWise = gameInput.generateAndBindGameKey('Q', "yawCCW");
		yawClockWise = gameInput.generateAndBindGameKey('E', "yawCW");

		m_editor_state = editor_state;
		m_game_state = game_state;
		m_frustum_cull_state = frustum_cull_update_state;
	}

private:
	virtual void onFrameProcess(rynx::scheduler::context& context, float dt) override {
		context.add_task("debug camera updates", [this, dt](
			rynx::mapped_input& gameInput,
			rynx::camera& gameCamera)
		{
			{
				if (gameInput.isKeyDown(camera_orientation_key)) {
					auto mouseDelta = gameInput.mouseDelta();
					if (gameInput.isKeyDown(yawCounterClockWise))
						mouseDelta.z = +dt * 10;
					if (gameInput.isKeyDown(yawClockWise))
						mouseDelta.z = -dt * 10;

					gameCamera.rotate(mouseDelta);
				}

				const float camera_translate_multiplier = 200.4f * dt;
				const float camera_zoom_multiplier = (1.0f - dt * 3.0f);
				
				if (gameInput.isKeyReleased(key_toggleEditorState)) {
					m_editor_state.toggle();
				}
				if (gameInput.isKeyReleased(key_toggleGameState)) {
					m_game_state.toggle();
				}
				if (gameInput.isKeyReleased(key_toggleFrustumCullState)) {
					m_frustum_cull_state.toggle();
				}

				if (gameInput.isKeyDown(cameraUp)) { gameCamera.translate(gameCamera.local_forward() * camera_translate_multiplier); }
				if (gameInput.isKeyDown(cameraDown)) { gameCamera.translate(-gameCamera.local_forward() * camera_translate_multiplier); }
				if (gameInput.isKeyDown(cameraLeft)) { gameCamera.translate(gameCamera.local_left() * camera_translate_multiplier); }
				if (gameInput.isKeyDown(cameraRight)) { gameCamera.translate(-gameCamera.local_left() * camera_translate_multiplier); }
				// if (gameInput.isKeyDown(zoomOut)) { cameraPosition *= vec3<float>(1, 1.0f, 1.0f * camera_zoom_multiplier); }
				// if (gameInput.isKeyDown(zoomIn)) { cameraPosition *= vec3<float>(1, 1.0f, 1.0f / camera_zoom_multiplier); }
			}
		});
	}
};

class game_collisions {
public:
	game_collisions(rynx::collision_detection& collisionDetection) {
		collisionCategoryDynamic = collisionDetection.add_category();
		collisionCategoryStatic = collisionDetection.add_category();
		collisionCategoryProjectiles = collisionDetection.add_category();

		{
			collisionDetection.enable_collisions_between(collisionCategoryDynamic, collisionCategoryDynamic); // enable dynamic <-> dynamic collisions
			collisionDetection.enable_collisions_between(collisionCategoryDynamic, collisionCategoryStatic.ignore_collisions()); // enable dynamic <-> static collisions

			collisionDetection.enable_collisions_between(collisionCategoryProjectiles, collisionCategoryStatic.ignore_collisions()); // projectile <-> static
			collisionDetection.enable_collisions_between(collisionCategoryProjectiles, collisionCategoryDynamic); // projectile <-> dynamic
		}
	}

	rynx::collision_detection::category_id category_dynamic() const { return collisionCategoryDynamic; }
	rynx::collision_detection::category_id category_static() const { return collisionCategoryStatic; }
	rynx::collision_detection::category_id category_projectiles() const { return collisionCategoryProjectiles; }

private:
	rynx::collision_detection::category_id collisionCategoryDynamic;
	rynx::collision_detection::category_id collisionCategoryStatic;
	rynx::collision_detection::category_id collisionCategoryProjectiles;
};

class GameMenu {

	rynx::menu::Div root;

	std::shared_ptr<rynx::graphics::framebuffer> fbo_menu;
	std::shared_ptr<rynx::camera> m_camera;

public:
	Font fontLenka;
	Font fontConsola;

	GameMenu(std::shared_ptr<rynx::graphics::GPUTextures> textures) :
		root({ 1, 1, 0 }),
		fontConsola(Fonts::setFontConsolaMono()),
		fontLenka(Fonts::setFontLenka())
	{
		fbo_menu = rynx::graphics::framebuffer::config()
			.set_default_resolution(1920, 1080)
			.add_rgba8_target("color")
			.construct(textures, "menu");

		m_camera = std::make_shared<rynx::camera>();

		auto sampleButton = std::make_shared<rynx::menu::Button>(
			*textures,
			"Frame",
			rynx::vec3<float>(0.4f, 0.1f, 0),
			rynx::vec3<float>(),
			0.14f
		);

		/*
		auto sampleButton2 = std::make_shared<rynx::menu::Button>(*application.textures(), "Frame", &root, rynx::vec3<float>(0.4f, 0.1f, 0), rynx::vec3<float>(), 0.16f);
		auto sampleButton3 = std::make_shared<rynx::menu::Button>(*application.textures(), "Frame", &root, rynx::vec3<float>(0.4f, 0.1f, 0), rynx::vec3<float>(), 0.18f);
		auto sampleSlider = std::make_shared<rynx::menu::SlideBarVertical>(*application.textures(), "Frame", "Selection", &root, rynx::vec3<float>(0.4f, 0.1f, 0));
		auto megaSlider = std::make_shared<rynx::menu::SlideBarVertical>(*application.textures(), "Frame", "Selection", &root, rynx::vec3<float>(0.4f, 0.1f, 0));
		*/

		sampleButton->text("Log Profile").font(&fontConsola);
		sampleButton->alignToInnerEdge(&root, rynx::menu::Align::BOTTOM_LEFT);
		sampleButton->color_frame(Color::RED);
		sampleButton->onClick([]() {
			rynx::profiling::write_profile_log();
		});

		/*
		sampleButton2->text("Dynamics").font(&fontConsola);
		sampleButton2->alignToOuterEdge(sampleButton.get(), rynx::menu::Align::RIGHT);
		sampleButton2->alignToInnerEdge(sampleButton.get(), rynx::menu::Align::BOTTOM);
		sampleButton2->onClick([&conf, self = sampleButton2.get()]() {
			bool new_value = !conf.visualize_dynamic_collisions;
			conf.visualize_dynamic_collisions = new_value;
			if (new_value) {
				self->color_frame(Color::GREEN);
			}
			else {
				self->color_frame(Color::RED);
			}
		});

		sampleButton3->text("Statics").font(&fontConsola);
		sampleButton3->alignToOuterEdge(sampleButton2.get(), rynx::menu::Align::TOP);
		sampleButton3->alignToInnerEdge(sampleButton2.get(), rynx::menu::Align::LEFT);
		sampleButton3->onClick([&conf, self = sampleButton3.get(), &root]() {
			bool new_value = !conf.visualize_static_collisions;
			conf.visualize_static_collisions = new_value;
			if (new_value) {
				self->color_frame(Color::GREEN);
			}
			else {
				self->color_frame(Color::RED);
			}
		});

		sampleSlider->alignToInnerEdge(&root, rynx::menu::Align::TOP_RIGHT);
		sampleSlider->onValueChanged([](float f) {});

		megaSlider->alignToOuterEdge(sampleSlider.get(), rynx::menu::Align::BOTTOM);
		megaSlider->alignToInnerEdge(sampleSlider.get(), rynx::menu::Align::LEFT);
		megaSlider->onValueChanged([](float f) {});
		*/

		root.addChild(sampleButton);

		/*
		root.addChild(sampleButton2);
		root.addChild(sampleButton3);
		root.addChild(sampleSlider);
		root.addChild(megaSlider);
		*/
	}

	rynx::menu::Component* root_node() {
		return &root;
	}

	GameMenu& add_child(std::shared_ptr<rynx::menu::Component> child) {
		root.addChild(std::move(child));
	}


	void logic_tick(float dt, float aspectRatio, rynx::mapped_input& gameInput) {
		root.input(gameInput);
		root.tick(dt, aspectRatio);
		m_camera->tick(dt * 5.0f);
	}

	void graphics_tick(float aspectRatio, rynx::MeshRenderer& meshRenderer, rynx::TextRenderer& textRenderer) {
		fbo_menu->bind_as_output();
		fbo_menu->clear();

		// 2, 2 is the size of the entire screen (in case of 1:1 aspect ratio) for menu camera. left edge is [-1, 0], top right is [+1, +1], etc.
		// so we make it size 2,2 to cover all of that. and then take aspect ratio into account by dividing the y-size.
		root.scale_local({ 2 , 2 / aspectRatio, 0 });
		m_camera->setProjection(0.01f, 50.0f, aspectRatio);
		m_camera->setPosition({ 0, 0, 1 });
		m_camera->setDirection({0, 0, -1});
		m_camera->setUpVector({ 0, 1, 0 });
		m_camera->rebuild_view_matrix();

		meshRenderer.setCamera(m_camera);
		textRenderer.setCamera(m_camera);
		meshRenderer.cameraToGPU();
		root.visualise(meshRenderer, textRenderer);
	}

	std::shared_ptr<rynx::graphics::framebuffer> fbo() {
		return fbo_menu;
	}
};