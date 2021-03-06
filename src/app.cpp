#include <te/app.hpp>
#include <fmt/format.h>
#include <examples/imgui_impl_glfw.h>
#include <examples/imgui_impl_opengl3.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <te/maths.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace {
    double fps = 0.0;
    ImGuiIO& setup_imgui(te::window& win) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        ImGui_ImplGlfw_InitForOpenGL(win.hnd.get(), true);
        ImGui_ImplOpenGL3_Init("#version 130");
        io.Fonts->AddFontFromFileTTF("LiberationSans-Regular.ttf", 18);
        return io;
    }
}

te::app::app(te::sim& model, unsigned int seed) :
    model { model },
    rengine { seed },
    win { glfw.make_window(1920 - 200, 1080 - 200, "Hello, World!", false)},
    imgui_io { setup_imgui(win) },
    cam {
        {0.0f, 0.0f, 0.0f},
        {-0.6f, -0.6f, 1.0f},
        14.0f,
        static_cast<float>(win.width()) / win.height()
    },
    terrain_renderer{ win.gl, rengine, model.map_width, model.map_height },
    mesh_renderer { win.gl },
    colour_picker{ win },
    loader { win.gl },
    resources { loader }
{
    win.on_framebuffer_size.connect([&](int width, int height) {
                                        cam.aspect_ratio = static_cast<float>(width) / height;
                                        glViewport(0, 0, width, height);
                                    });
    win.set_attribute(GLFW_RESIZABLE, GLFW_TRUE);
    win.on_key.connect([&](int key, int scancode, int action, int mods){ on_key(key, scancode, action, mods); });
    win.on_mouse_button.connect([&](int button, int action, int mods) { on_mouse_button(button, action, mods); });
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    marker = model.entities.create();
    model.entities.assign<render_mesh>(marker, "media/dwelling.glb");
    model.entities.assign<footprint>(marker, glm::vec2{1.0f, 1.0f});
}

void te::app::on_key(int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_Q && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
        cam.offset = glm::rotate(cam.offset, -glm::half_pi<float>()/4.0f, glm::vec3{0.0f, 0.0f, 1.0f});
    }
    if (key == GLFW_KEY_E && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
        cam.offset = glm::rotate(cam.offset, glm::half_pi<float>()/4.0f, glm::vec3{0.0f, 0.0f, 1.0f});
    }
}

void te::app::on_mouse_button(int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        if (ghost) {
            auto proto = model.entities.get<te::ghost>(*ghost).proto;
            if (model.try_place(proto, model.entities.get<te::site>(*ghost).position)) {
                model.families[1].balance -= model.entities.get<te::price>(proto).price;
                model.entities.destroy(*ghost);
                ghost.reset();
                return;
            }
        }
        if (pos_under_mouse) {
            auto map_entities = model.entities.view<te::site, te::pickable>();
            for (auto entity : map_entities) {
                auto& map_site = map_entities.get<te::site>(entity);
                if (glm::distance(map_site.position, *pos_under_mouse) <= 1.0f) {
                    inspected = entity;
                    return;
                }
            }
        }
        inspected.reset();
    }
}

glm::mat4 rotate_zup = glm::mat4_cast(te::rotation_between_units (
    glm::vec3 {0.0f, 1.0f, 0.0f},
    glm::vec3 {0.0f, 0.0f, 1.0f}
));

void te::app::mouse_pick() {
    double mouse_x; double mouse_y;
    glfwGetCursorPos(win.hnd.get(), &mouse_x, &mouse_y);
    pos_under_mouse = cast_ray({mouse_x, mouse_y});
}

void te::app::render_scene() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    terrain_renderer.render(cam);

    auto instances = model.entities.group<render_mesh, site, footprint>();
    instances.sort<te::render_mesh> (
        [](const auto& lhs, const auto& rhs) {
            return lhs.filename < rhs.filename;
        }
    );

    const auto begin = instances.begin();
    const auto end = instances.end();
    auto it = begin;

    const te::market* market = nullptr;
    const te::site* market_site = nullptr;
    if (inspected) {
        market = model.entities.try_get<te::market>(*inspected);
        market_site = model.entities.try_get<te::site>(*inspected);
    }

    do {
        std::vector<te::mesh_renderer::instance_attributes> instance_attributes;
        const auto& current_rmesh = instances.get<render_mesh>(*it);
        while (it != end && instances.get<render_mesh>(*it).filename == current_rmesh.filename) {
            bool tinted = market && market_site && model.in_market(instances.get<site>(*it), *market_site, *market)
                       || inspected == *it;            
            instance_attributes.push_back (
                te::mesh_renderer::instance_attributes {
                    instances.get<site>(*it).position,
                    tinted ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f)
                }
            );
            it++;
        }
        auto& doc = resources.lazy_load<gltf>(current_rmesh.filename);
        auto& instanced = mesh_renderer.instance(*doc.primitives.begin());
        instanced.instance_attribute_buffer.bind();
        instanced.instance_attribute_buffer.upload(instance_attributes.begin(), instance_attributes.end());
        mesh_renderer.draw(instanced, rotate_zup, cam, instance_attributes.size());
    } while (it != end);
}

namespace {
    void render_ui_demo() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::ShowDemoWindow();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}

void te::app::render_inspector() {
    ImGui::Begin("Inspector", nullptr, 0);
    ImGui::Text("FPS: %f", fps);
    ImGui::Separator();
    if (inspected) {
        if (auto [maybe_site, maybe_named] = model.entities.try_get<te::site, te::named>(*inspected); maybe_site && maybe_named) {
            ImGui::Text("Map position: (%f, %f)", maybe_site->position.x, maybe_site->position.y);
            auto id_string = fmt::format("{}", *reinterpret_cast<std::uint32_t*>(&*inspected));
            ImGui::Text("%s", maybe_named->name.c_str());
            ImGui::Separator();
        }
        if (auto the_generator = model.entities.try_get<te::generator>(*inspected); the_generator) {
            auto& rendr_tex = model.entities.get<te::render_tex>(the_generator->output);
            ImGui::Image(*resources.lazy_load<te::gl::texture2d>(rendr_tex.filename).hnd, ImVec2{24, 24});
            ImGui::SameLine();
            const auto& output_commodity_name = model.entities.get<te::named>(the_generator->output);
            ImGui::Text(fmt::format("{} @ {}/s", output_commodity_name.name, the_generator->rate).c_str());
            ImGui::SameLine();
            ImGui::ProgressBar(the_generator->progress);
            ImGui::Separator();
        }
        if (auto demander = model.entities.try_get<te::demander>(*inspected); demander) {
            for (auto [demanded, rate] : demander->rate) {
                auto [name, tex] = model.entities.get<te::named, te::render_tex>(demanded);
                ImGui::Image(*resources.lazy_load<te::gl::texture2d>(tex.filename).hnd, ImVec2{24, 24});
                ImGui::SameLine();
                ImGui::Text(fmt::format("{} @ {}/s", name.name, rate).c_str());
            }
            ImGui::Separator();
        }
        if (auto inventory = model.entities.try_get<te::inventory>(*inspected); inventory && !inventory->stock.empty()) {
            for (auto [commodity_entity, stock] : inventory->stock) {
                auto& name = model.entities.get<te::named>(commodity_entity);
                ImGui::Text(fmt::format("{}x {}", stock, name.name).c_str());
            }
            ImGui::Separator();
        }
        if (auto trader = model.entities.try_get<te::trader>(*inspected); trader) {
            ImGui::Text(fmt::format("Running balance: {}", trader->balance).c_str());
            ImGui::Separator();
        }
        if (auto [producer, inventory] = model.entities.try_get<te::producer, te::inventory>(*inspected); producer && inventory) {
            ImGui::Text("Inputs");
            for (auto& [commodity_e, needed] : producer->inputs) {
                auto& commodity_tex = model.entities.get<te::render_tex>(commodity_e);
                ImGui::Image(*resources.lazy_load<te::gl::texture2d>(commodity_tex.filename).hnd, ImVec2{24, 24});
                ImGui::SameLine();
                ImGui::Text(fmt::format("{}: {}/{}", model.entities.get<named>(commodity_e).name, inventory->stock[commodity_e], needed).c_str());
            }
            ImGui::ProgressBar(producer->progress);
            ImGui::Text(fmt::format("Outputs @{}/s", producer->rate).c_str());
            for (auto& [commodity_e, produced] : producer->outputs) {
                auto& commodity_tex = model.entities.get<te::render_tex>(commodity_e);
                ImGui::Image(*resources.lazy_load<te::gl::texture2d>(commodity_tex.filename).hnd, ImVec2{24, 24});
                ImGui::SameLine();
                ImGui::Text(fmt::format("{}: ×{}", model.entities.get<named>(commodity_e).name, produced).c_str());
            }
        }
        if (auto market = model.entities.try_get<te::market>(*inspected); market) {
            ImGui::Text(fmt::format("Population: {}", market->population).c_str());
            ImGui::Text(fmt::format("Growth rate: {}", market->growth_rate).c_str());
            ImGui::Text("Growth: ");
            ImGui::SameLine();
            ImGui::ProgressBar((glm::clamp(market->growth, -1.0, 1.0) + 1.0) / 2.0);

            ImGui::Columns(5);
            float width_available = ImGui::GetWindowContentRegionWidth();

            ImGui::Text("Stock");
            ImGui::SetColumnWidth(0, 80);
            width_available -= 80;
            ImGui::NextColumn();

            ImGui::SetColumnWidth(1, 50);
            width_available -= 50;
            ImGui::NextColumn();

            ImGui::Text("Commodity");
            ImGui::SetColumnWidth(2, 300);
            width_available -= 300;
            ImGui::NextColumn();

            ImGui::NextColumn();

            ImGui::Text("Demand");
            ImGui::SetColumnWidth(4, 100);
            width_available -= 100;
            ImGui::NextColumn();

            ImGui::SetColumnWidth(3, width_available);
            for (auto [commodity_entity, price] : market->prices) {
                auto [commodity_name, commodity_tex] = model.entities.get<te::named, te::render_tex>(commodity_entity);
                ImGui::Text(fmt::format("{}", model.market_stock(*inspected, commodity_entity)).c_str());
                ImGui::NextColumn();

                ImGui::Image(*resources.lazy_load<te::gl::texture2d>(commodity_tex.filename).hnd, ImVec2{24, 24});
                ImGui::NextColumn();

                ImGui::Text(commodity_name.name.c_str());
                ImGui::NextColumn();

                double commodity_demand = market->demand[commodity_entity];
                double commodity_price = market->prices[commodity_entity];
                ImDrawList* draw = ImGui::GetWindowDrawList();
                static const auto light_blue = ImColor(ImVec4{22.9/100.0, 60.7/100.0, 85.9/100.0, 1.0f});
                static const auto dark_blue = ImColor(ImVec4{22.9/255.0, 60.7/255.0, 85.9/255.0, 1.0f});
                static const auto white = ImColor(ImVec4{1.0, 1.0, 1.0, 1.0});
                const ImVec2 cursor_pos {ImGui::GetCursorScreenPos()};
                const float bar_bottom = cursor_pos.y + 20.0f;
                const float bar_height = 10.0f;
                const float bar_top = bar_bottom - bar_height;
                const float bar_left = cursor_pos.x;
                const float bar_width = width_available - 16;
                const float bar_right = cursor_pos.x + bar_width;
                const float bar_centre = (bar_left + bar_right) / 2.0;
                draw->AddRectFilled (
                    ImVec2{bar_left, bar_top},
                    ImVec2{bar_left + bar_width * std::min(static_cast<float>(commodity_demand), 1.0f), bar_bottom},
                    light_blue, 0, 0.0f
                );
                draw->AddRectFilled (
                    ImVec2{bar_right - bar_width * (std::max(1.0f - static_cast<float>(commodity_demand), 0.0f)), bar_top},
                    ImVec2{bar_right, bar_bottom},
                    dark_blue, 0, 0.0f
                );

                std::string price_string = fmt::format("{:.1f}", commodity_price);
                const char* price_string_begin = std::to_address(price_string.begin());
                const char* price_string_end = std::to_address(price_string.end());
                ImVec2 price_text_size = ImGui::CalcTextSize(price_string_begin, price_string_end);
                const float text_left = bar_centre - price_text_size.x / 2.0f;
                draw->AddText(ImVec2{text_left, cursor_pos.y}, white, price_string_begin, price_string_end);

                int marker_count = static_cast<int>(commodity_demand);
                float gap_size = ((std::floor(commodity_demand) / commodity_demand) * bar_width) / static_cast<float>(marker_count);
                for (int i = 0; i < marker_count; i++) {
                    draw->AddRectFilled (
                        ImVec2{bar_left + (i + 1) * gap_size - 2.0f, bar_top - 1.0f},
                        ImVec2{bar_left + (i + 1) * gap_size + 2.0f, bar_bottom + 1.0f},
                        white, 0, 0.0f
                    );
                }
                ImGui::NextColumn();

                ImGui::Text(fmt::format("{}x", static_cast<int>(commodity_demand)).c_str());
                ImGui::NextColumn();
            }
            ImGui::Separator();
            ImGui::Columns();
        }
    }
    ImGui::End();
}

void te::app::render_controller() {
    ImGui::Begin("Controller", nullptr, 0);
    ImGui::Text(fmt::format("¤{}", model.families[1].balance).c_str());
    if (ImGui::BeginTabBar("MainTabbar")) {
        if (ImGui::BeginTabItem("Build")) {
            for (auto blueprint : model.blueprints) {
                if (auto [named, price, footprint] = model.entities.try_get<te::named, te::price, te::footprint>(blueprint); named && price && footprint) {
                    if (ImGui::Button(fmt::format("{}: ¤{}", named->name, price->price).c_str())) {
                        if (!ghost) {
                            ghost = model.entities.create<te::site, te::footprint, te::render_mesh>(blueprint, model.entities);
                            model.entities.assign<te::ghost>(*ghost, blueprint);
                        }
                    }
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Merchants")) {
            auto merchants = model.entities.view<te::trader, te::merchant, te::inventory, te::named>();
            for (auto merchant_entity : merchants) {
                const auto& trader = merchants.get<te::trader>(merchant_entity);
                if (trader.family_ix != 1) continue;
                const auto& merchant = merchants.get<te::merchant>(merchant_entity);
                const auto& merchant_inventory = merchants.get<te::inventory>(merchant_entity);
                const auto& merchant_name = merchants.get<te::named>(merchant_entity);
                ImGui::Text(fmt::format("{}: ¤{}", merchant_name.name, trader.balance).c_str());
                if (merchant.route) {
                    ImGui::Text(merchant.route->name.c_str());
                    auto next_stop = merchant.route->stops[(merchant.last_stop + 1) % merchant.route->stops.size()];
                    auto next_stop_name = model.entities.get<te::named>(next_stop.where);
                    if (merchant.trading) {
                        ImGui::Text(fmt::format("Trading at {}", next_stop_name.name).c_str());
                    } else {
                        ImGui::Text(fmt::format("En route to {}", next_stop_name.name).c_str());
                    }
                    ImGui::NewLine();
                    for (auto commodity : model.commodities) {
                        const auto stock_it = merchant_inventory.stock.find(commodity);
                        const int stock = stock_it == merchant_inventory.stock.end() ? 0 : stock_it->second;
                        const auto leave_with_it = next_stop.leave_with.find(commodity);
                        const int leave_with = leave_with_it == next_stop.leave_with.end() ? 0 : leave_with_it->second;
                        const int buy = std::max(0, leave_with - stock);
                        const int sell = merchant.trading ? std::max(0, stock - leave_with) : 0;
                        const int keep = stock - sell;
                        const auto& commodity_tex = resources.lazy_load<te::gl::texture2d>(model.entities.get<te::render_tex>(commodity).filename);
                        for (int i = 0; i < buy; i++) {
                            ImGui::SameLine();
                            ImGui::Image (
                                *commodity_tex.hnd, ImVec2{24, 24}, ImVec2{0, 0}, ImVec2{1, 1}, ImVec4{1, 1, 1, 1},
                                ImVec4{22.9/100.0, 60.7/100.0, 85.9/100.0, 1.0f}
                            );
                        }
                        for (int i = 0; i < sell; i++) {
                            ImGui::SameLine();
                            ImGui::Image(*commodity_tex.hnd, ImVec2{24, 24}, ImVec2{0, 0}, ImVec2{1, 1}, ImVec4{1, 1, 1, 1}, ImVec4{1, 0, 0, 1});
                        }
                        for (int i = 0; i < keep; i++) {
                            ImGui::SameLine();
                            ImGui::Image(*commodity_tex.hnd, ImVec2{24, 24}, ImVec2{0, 0}, ImVec2{1, 1}, ImVec4{1, 1, 1, 1}, ImVec4{1, 1, 1, 1});
                        }
                    }
                } else {
                    ImGui::Text("No route assigned");
                    for (auto [commodity, stock] : merchant_inventory.stock) {
                        const auto& commodity_tex = resources.lazy_load<te::gl::texture2d>(model.entities.get<te::render_tex>(commodity).filename);
                        for (int i = 0; i < stock; i++) {
                            ImGui::SameLine();
                            ImGui::Image(*commodity_tex.hnd, ImVec2{24, 24}, ImVec2{0, 0}, ImVec2{1, 1}, ImVec4{1, 1, 1, 1}, ImVec4{1, 1, 1, 1});
                        }
                    }
                }
                ImGui::Separator();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Routes")) {
            static te::route* editing = nullptr;
            static std::optional<int> selected_route_ix = std::nullopt;
            if (ImGui::BeginCombo("###route_selector", selected_route_ix ? model.routes[*selected_route_ix].name.c_str() : "")) {
                for (int i = 0; i < model.routes.size(); i++) {
                    bool current_item_selected = i == selected_route_ix;
                    if (ImGui::Selectable(model.routes[i].name.c_str(), current_item_selected)) {
                        selected_route_ix = i;
                    }
                    if (current_item_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                };
                ImGui::EndCombo();
            }
            if (ImGui::Button("New")) {
            }
            ImGui::SameLine();
            if (ImGui::Button("Save")) {
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) {
            }
            ImGui::Separator();
            auto markets = model.entities.view<te::market, te::named, te::site>();
            auto market_it = markets.begin();
            static std::optional<decltype(market_it)> selected_next_stop_it = std::nullopt;
            if (market_it == markets.end()) {
                if (ImGui::BeginCombo("###next_stop_selector", "")) {
                    // no markets so don't allow anything to be selected
                    ImGui::EndCombo();
                }
            } else {
                if (ImGui::BeginCombo("###next_stop_selector", model.entities.get<te::named>(*(selected_next_stop_it.value_or(market_it))).name.c_str())) {
                    for (;market_it != markets.end(); market_it++) {
                        bool current_next_stop_selected = market_it == selected_next_stop_it;
                        auto name = model.entities.get<te::named>(*market_it).name;
                        if (ImGui::Selectable(name.c_str(), current_next_stop_selected)) {
                            selected_next_stop_it = market_it;
                        }
                        if (current_next_stop_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Stop")) {
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void te::app::render_ui() {
    //render_ui_demo(); return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    //render_inspector();
    render_controller();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void te::app::input() {
    if (win.key(GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        win.close();
        return;
    };

    mouse_pick();
    if (ghost && pos_under_mouse) {
        model.entities.assign_or_replace<site>(*ghost, model.snap(*pos_under_mouse, glm::vec2{1.0f, 1.0f}));
    }

    glm::vec3 forward = -cam.offset;
    forward.z = 0.0f;
    forward = glm::normalize(forward);
    glm::vec3 left = glm::rotate(forward, glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f});
    if (win.key(GLFW_KEY_W) == GLFW_PRESS) cam.focus += 0.3f * forward;
    if (win.key(GLFW_KEY_A) == GLFW_PRESS) cam.focus += 0.3f * left;
    if (win.key(GLFW_KEY_S) == GLFW_PRESS) cam.focus -= 0.3f * forward;
    if (win.key(GLFW_KEY_D) == GLFW_PRESS) cam.focus -= 0.3f * left;
    if (win.key(GLFW_KEY_H) == GLFW_PRESS) cam.zoom(0.15f);
    if (win.key(GLFW_KEY_J) == GLFW_PRESS) cam.zoom(-0.15f);
    cam.use_ortho = win.key(GLFW_KEY_SPACE) != GLFW_PRESS;
}

void te::app::draw() {
    render_scene();
    render_ui();
}

void te::app::run() {
    auto then = std::chrono::high_resolution_clock::now();
    int frames = 0;
    while (!glfwWindowShouldClose(win.hnd.get())) {
        input();
        if (frames == 5) {
            auto now = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> secs = now - then;
            fps = static_cast<double>(frames) / secs.count();
            model.tick(secs.count() * 3.0);
            frames = 0;
            then = std::chrono::high_resolution_clock::now();
        }
        draw();
        glfwSwapBuffers(win.hnd.get());
        frames++;
        glfwPollEvents();
    }
}

glm::vec2 te::app::cast_ray(glm::vec2 ray_screen) const {
    const glm::vec3 ray_ndc {
        (2.0f * ray_screen.x) / win.width() - 1.0f,
        1.0f - (2.0f * ray_screen.y) / win.height(),
        1.0f
    };
    auto [ray_origin, ray_direction] = cam.cast(ray_ndc);
    
    const glm::vec3 ground_normal {0.0f, 0.0f, 1.0f};
    const auto t = -dot(ray_origin, ground_normal) / dot(ray_direction, ground_normal);
    const glm::vec3 intersection = ray_origin + ray_direction * t;
    return glm::vec2{intersection.x, intersection.y};
}
