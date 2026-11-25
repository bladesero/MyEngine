#include <BUI_kit.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

Json ReadJson(const fs::path& file) {
    if (!fs::is_regular_file(file) || fs::file_size(file) > 64 * 1024 * 1024)
        throw std::runtime_error("Missing, invalid, or oversized file: " + file.u8string());
    std::ifstream input(file, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read: " + file.u8string());
    return Json::parse(input);
}

bool IsBelow(const fs::path& root, const fs::path& file) {
    const auto mismatch = std::mismatch(root.begin(), root.end(), file.begin(), file.end());
    return mismatch.first == root.end() && mismatch.second != file.end();
}

constexpr std::string_view kSceneSuffix = ".scene.json";
bool IsSceneFile(const fs::path& path) {
    const auto name = path.filename().u8string();
    return name.size() > kSceneSuffix.size() &&
           name.compare(name.size() - kSceneSuffix.size(), kSceneSuffix.size(), kSceneSuffix) == 0;
}

struct Project {
    fs::path root;
    std::string name;
    fs::path startup;
    std::vector<fs::path> scenes;

    explicit Project(const fs::path& path) : root(fs::canonical(path)) {
        const auto manifestPath = fs::canonical(root / "MyEngine.project.json");
        if (!IsBelow(root, manifestPath))
            throw std::runtime_error("Manifest points outside the project");
        const auto manifest = ReadJson(manifestPath);
        name = manifest.at("name").get<std::string>();
        const auto relative = fs::u8path(manifest.at("startupScene").get<std::string>());
        if (relative.empty() || relative.has_root_path())
            throw std::runtime_error("startupScene must be a project-relative path");
        startup = fs::canonical(root / relative);
        if (!IsBelow(root, startup) || !IsSceneFile(startup))
            throw std::runtime_error("startupScene must name a .scene.json file inside the project");

        const auto content = root / "Content" / "Scenes";
        std::error_code ec;
        if (fs::is_directory(content, ec) && IsBelow(root, fs::canonical(content))) {
            for (const auto& entry :
                 fs::recursive_directory_iterator(content, fs::directory_options::skip_permission_denied)) {
                if (entry.is_symlink() || !entry.is_regular_file() || !IsSceneFile(entry.path()))
                    continue;
                scenes.push_back(fs::canonical(entry.path()));
            }
        }
        scenes.push_back(startup);
        std::sort(scenes.begin(), scenes.end());
        scenes.erase(std::unique(scenes.begin(), scenes.end()), scenes.end());
    }
};

struct Actor {
    std::uint64_t id;
    std::uint64_t parent;
    std::string name;
    bool active;
    Json transform;
    std::string components;
};

std::uint64_t ReadID(const Json& id) {
    if (!id.is_number_unsigned() && (!id.is_number_integer() || id.get<std::int64_t>() < 0))
        throw std::runtime_error("Actor IDs must be nonnegative integers");
    return id.get<std::uint64_t>();
}

struct Scene {
    std::vector<Actor> actors;
    std::unordered_map<std::uint64_t, std::size_t> byID;

    explicit Scene(const fs::path& file) {
        const auto data = ReadJson(file);
        const auto& entries = data.at("actors");
        if (!entries.is_array())
            throw std::runtime_error("Scene actors must be an array");
        for (const auto& item : entries) {
            const auto components = item.value("components", Json::array());
            if (!components.is_array())
                throw std::runtime_error("Actor components must be an array");
            std::string names;
            for (const auto& component : components) {
                if (!names.empty())
                    names += ", ";
                names += component.at("type").get<std::string>();
            }
            if (item.contains("prefabInstance")) {
                const auto asset = item.at("prefabInstance").value("asset", std::string{});
                names += (names.empty() ? "" : ", ") + std::string("Prefab instance: ") + asset;
            }
            Actor actor{ReadID(item.at("id")), ReadID(item.value("parentID", Json(0))),
                        item.at("name").get<std::string>(), item.value("active", true),
                        item.value("transform", Json::object()), std::move(names)};
            if (actor.id == 0 || !byID.emplace(actor.id, actors.size()).second)
                throw std::runtime_error("Scene has an invalid or duplicate actor ID");
            actors.push_back(std::move(actor));
        }
        // Do not pass a malformed cyclic or arbitrarily deep hierarchy into the UI toolkit.
        for (const auto& actor : actors) {
            auto parent = actor.parent;
            for (int depth = 0; parent != 0; ++depth) {
                if (depth == 128)
                    throw std::runtime_error("Scene actor hierarchy is cyclic or too deep");
                const auto next = byID.find(parent);
                if (next == byID.end())
                    throw std::runtime_error("Scene actor parent does not exist");
                parent = actors[next->second].parent;
            }
        }
    }
};

BKNode* Add(BKNode* parent, const char* type, const char* text = nullptr, bool expand = false,
            int side = 0, int width = 0) {
    BKNode* node = BK_node_new(type);
    if (!node)
        throw std::runtime_error(std::string("BUI cannot create node: ") + type);
    BK_node_add(parent, node, -1);
    if (text)
        BK_set_str(node, "text", text);
    BK_geom_set_manager(node, BK_GEOM_PACK);
    BK_geom_set(node, "side", side);
    BK_geom_set(node, "fill", side == 2 ? 3 : (expand ? 3 : 1));
    BK_geom_set(node, "expand", expand);
    BK_geom_set(node, "pady0", 3);
    if (width)
        BK_geom_set(node, "reqwidth", width);
    return node;
}

using Node = std::unique_ptr<BKNode, decltype(&BK_node_free)>;
using TreeItem = std::unique_ptr<BKTreeItem, decltype(&BK_tree_item_free)>;
using Context = std::unique_ptr<BKContext, decltype(&BK_context_free)>;

struct OwnedItems {
    std::vector<TreeItem> storage;

    ~OwnedItems() {
        // Non-null item refs make these host-owned. Tear down the parent links
        // while every child still exists (the tree itself has already detached).
        for (auto& item : storage)
            BK_tree_item_clear(item.get());
    }
};

class Workbench {
public:
    explicit Workbench(const fs::path& path)
        : project_(path), root_(BK_node_new("frame"), BK_node_free), context_(nullptr, BK_context_free) {
        if (!root_)
            throw std::runtime_error("BUI could not allocate a root");
        BK_set_num(root_.get(), "padding", 10);
        Add(root_.get(), "label", ("MyEngine | Native BUI | " + project_.name).c_str());
        scenes_ = Add(root_.get(), "dropdown");
        std::vector<std::string> labels;
        for (const auto& file : project_.scenes)
            labels.push_back(file.lexically_relative(project_.root).generic_u8string());
        std::vector<const char*> values;
        for (const auto& label : labels)
            values.push_back(label.c_str());
        BK_set_strs(scenes_, "items", values.data(), static_cast<int>(values.size()));
        reload_ = Add(root_.get(), "button", "Reload scene from disk");
        status_ = Add(root_.get(), "label");
        auto* columns = Add(root_.get(), "frame", nullptr, true);
        auto* left = Add(columns, "frame", nullptr, false, 2, 400);
        Add(left, "label", "Scene Hierarchy");
        tree_ = Add(left, "tree", nullptr, true);
        auto* right = Add(columns, "frame", nullptr, true, 2);
        Add(right, "label", "Inspector (read-only)");
        actorName_ = Add(right, "label");
        actorState_ = Add(right, "label");
        for (auto& transform : transforms_)
            transform = Add(right, "label");
        componentTypes_ = Add(right, "label");
        context_.reset(BK_context_new(root_.get()));
        if (!context_)
            throw std::runtime_error("BUI could not allocate a context");
        BKHost host{};
        host.data = this;
        host.emit = &OnEvent;
        BK_context_set_host(context_.get(), &host);
        Load(static_cast<std::size_t>(
            std::find(project_.scenes.begin(), project_.scenes.end(), project_.startup) - project_.scenes.begin()));
    }

    BKContext* GetContext() const { return context_.get(); }
    const std::string& GetProjectName() const { return project_.name; }

    void SelfTest() {
        constexpr int width = 1100, height = 720;
        std::unique_ptr<BuiRaster, decltype(&BUI_raster_free)> raster(BUI_raster_create(width, height),
                                                                     BUI_raster_free);
        if (!raster || !BK_render(context_.get(), raster.get(), width, height, 1))
            throw std::runtime_error("BUI produced no frame");
        const auto* pixels = BUI_raster_pixels(raster.get());
        if (!pixels || std::all_of(pixels, pixels + width * height, [pixels](uint32_t p) { return p == *pixels; }))
            throw std::runtime_error("BUI frame is empty or uniform");
        if (BK_tree_row_count(tree_) < 1)
            throw std::runtime_error("Sample project's hierarchy is empty");
        auto* first = BK_tree_row(tree_, 0);
        const auto expected = std::string(BK_tree_item_label(first));
        int row[4]{};
        BK_tree_row_rect(tree_, 0, row);
        if (row[2] < 40 || row[3] <= 0)
            throw std::runtime_error("BUI hierarchy row has no usable area");
        // Drive the same pointer events as the native host, not a direct app callback.
        Click(row[0] + 38, row[1] + row[3] / 2);
        if (BK_tree_selected(tree_) != first || std::string(BK_get_str(actorName_, "text")) != expected)
            throw std::runtime_error("BUI hierarchy click did not update the inspector");
        if (project_.scenes.size() > 1) {
            const auto target = (sceneIndex_ + 1) % project_.scenes.size();
            ClickCenter(reload_);
            if (BK_tree_selected(tree_))
                throw std::runtime_error("Reload did not reset the inspector selection");
            ClickCenter(scenes_);
            BK_render(context_.get(), raster.get(), width, height, 0);
            auto* menu = BK_popup(context_.get(), 0);
            if (!menu)
                throw std::runtime_error("Could not open the scene picker");
            int option[4]{};
            BK_menu_row_rect(menu, static_cast<int>(target), option);
            Click(option[0] + option[2] / 2, option[1] + option[3] / 2);
            if (sceneIndex_ != target)
                throw std::runtime_error("Picking a scene did not load it");
        }
        for (std::size_t i = 0; i < project_.scenes.size(); ++i) {
            Load(i);
            if (!scene_->actors.empty() && BK_tree_row_count(tree_) < 1)
                throw std::runtime_error("Scene switch did not rebuild the hierarchy");
        }
        std::cout << "Native BUI rendered, dispatched pointer input, and loaded " << project_.scenes.size()
                  << " project scene(s).\n";
    }

private:
    void Click(int x, int y) {
        BK_motion(context_.get(), x, y, 0);
        BK_press(context_.get(), x, y, 0);
        BK_release(context_.get(), x, y, 0);
    }

    void ClickCenter(BKNode* node) {
        int rect[4]{};
        BK_node_rect(node, rect);
        Click(rect[0] + rect[2] / 2, rect[1] + rect[3] / 2);
    }

    void Load(std::size_t index) {
        if (index >= project_.scenes.size())
            return;
        const auto& path = project_.scenes[index];
        const auto checked = fs::canonical(path);
        if (!IsBelow(project_.root, checked))
            throw std::runtime_error("Scene points outside the project");
        auto model = std::make_unique<Scene>(checked);
        auto owned = std::make_unique<OwnedItems>();
        auto& items = owned->storage;
        std::vector<BKTreeItem*> roots;
        for (auto& actor : model->actors) {
            TreeItem item(BK_tree_item_new(actor.name.c_str(), 0), BK_tree_item_free);
            if (!item)
                throw std::runtime_error("BUI could not allocate a hierarchy item");
            BK_tree_item_set_ref(item.get(), &actor);
            items.push_back(std::move(item));
        }
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto parent = model->actors[i].parent;
            if (parent == 0)
                roots.push_back(items[i].get());
            else
                BK_tree_item_add(items[model->byID.at(parent)].get(), items[i].get());
        }
        BKTreeItem* empty = nullptr;
        BK_tree_set_roots(tree_, roots.empty() ? &empty : roots.data(), static_cast<int>(roots.size()));
        ownedItems_ = std::move(owned);
        scene_ = std::move(model);
        sceneIndex_ = index;
        BK_set_num(scenes_, "value", static_cast<double>(index));
        BK_set_str(status_, "text", (path.lexically_relative(project_.root).generic_u8string() + " | " +
                                    std::to_string(items.size()) + " actors").c_str());
        Inspect(nullptr);
    }

    void Inspect(const Actor* actor) {
        BK_set_str(actorName_, "text", actor ? actor->name.c_str() : "Select an actor in Scene Hierarchy");
        BK_set_str(actorState_, "text",
                   actor ? ("ID " + std::to_string(actor->id) + (actor->active ? " | Active" : " | Inactive")).c_str()
                         : "");
        const char* keys[] = {"position", "rotation", "scale"};
        for (int i = 0; i < 3; ++i) {
            std::string text;
            if (actor && actor->transform.contains(keys[i]))
                text = std::string(keys[i]) + ": " + actor->transform.at(keys[i]).dump();
            BK_set_str(transforms_[i], "text", text.c_str());
        }
        BK_set_str(componentTypes_, "text", actor ? ("Components: " + actor->components).c_str() : "");
    }

    static void OnEvent(void* data, BKNode* node, int event, void*, double, double) noexcept {
        auto& ui = *static_cast<Workbench*>(data);
        try {
            if (node == ui.tree_ && event == BK_EV_CHANGE) {
                auto* selected = BK_tree_selected(node);
                ui.Inspect(selected ? static_cast<const Actor*>(BK_tree_item_ref(selected)) : nullptr);
            } else if (node == ui.scenes_ && event == BK_EV_CHANGE) {
                const double selected = BK_get_num(node, "value");
                if (selected >= 0 && selected < ui.project_.scenes.size())
                    ui.Load(static_cast<std::size_t>(selected));
            } else if (node == ui.reload_ && event == BK_EV_CLICK) {
                ui.Load(ui.sceneIndex_);
            }
        } catch (const std::exception& error) {
            BK_set_num(ui.scenes_, "value", static_cast<double>(ui.sceneIndex_));
            BK_set_str(ui.status_, "text", error.what());
        }
    }

    Project project_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<OwnedItems> ownedItems_;
    Node root_;
    Context context_;
    BKNode* scenes_{};
    BKNode* reload_{};
    BKNode* status_{};
    BKNode* tree_{};
    BKNode* actorName_{};
    BKNode* actorState_{};
    BKNode* transforms_[3]{};
    BKNode* componentTypes_{};
    std::size_t sceneIndex_{};
};

} // namespace

int main(int argc, char** argv) {
    try {
        fs::path project = fs::current_path();
        bool selfTest = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            if (arg == "--project" && i + 1 < argc)
                project = fs::u8path(argv[++i]);
            else if (arg == "--self-test")
                selfTest = true;
            else if (arg == "--help") {
                std::cout << "MyEngineBuiEditor [--project <directory>] [--self-test]\n";
                return 0;
            } else
                throw std::runtime_error("Invalid argument. See --help.");
        }
        Workbench ui(project);
        if (selfTest) {
            ui.SelfTest();
            return 0;
        }
        if (!BK_window_supported())
            throw std::runtime_error("BUI native window unavailable; use --self-test for headless verification");
        std::unique_ptr<BKWindow, decltype(&BK_window_free)> window(
            BK_window_new(ui.GetContext(), ("MyEngine BUI - " + ui.GetProjectName()).c_str(), 1100, 720),
            BK_window_free);
        if (!window)
            throw std::runtime_error("BUI could not create a native window");
        BK_window_show(window.get());
        BK_window_run();
    } catch (const std::exception& error) {
        std::cerr << "MyEngineBuiEditor: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
