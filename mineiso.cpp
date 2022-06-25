#include <cstdio>
#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <array>
#include <ctime>
#include <vector>
#include <optional>
#include <algorithm>
#include <memory>
#include <functional>
#include <unordered_map>
#include <thread>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/quaternion_transform.hpp>

#include "stb_image.h"

using namespace std::string_literals;
using namespace std::chrono_literals;

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Settings

constexpr size_t WIDTH = 1280, HEIGHT = 720;

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Shader

/// Compile and link shader program
GLuint load_shader_program()
{
    const char* vertex_shader = R"(
#version 410
layout ( location = 0 ) in vec2 vPosition;
layout ( location = 1 ) in vec2 vTexCoord;
uniform mat4 projection;
uniform mat4 model;
out vec2 texcoord;
void main() {
    gl_Position = projection * model * vec4(vPosition, 0.0f, 1.0f);
    texcoord = vTexCoord;
}
)";

    const char* fragment_shader = R"(
#version 410
in vec2 texcoord;
uniform sampler2D texture0;
uniform vec4 color;
out vec4 frag_color;
void main(){
    frag_color = texture(texture0, texcoord) * color;
}
)";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertex_shader, NULL);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragment_shader, NULL);
    glCompileShader(fs);

    GLuint sp = glCreateProgram();
    glAttachShader(sp, fs);
    glAttachShader(sp, vs);
    glLinkProgram(sp);
    return sp;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////
/// GLObject

/// Vertex data
struct Vertex {
    glm::vec2 pos;
    glm::vec2 texcoord;
};

/// Represents data of an object in GPU memory
struct GLObject {
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    size_t num_vertices;
    size_t num_indices;
};

/// GLObject reference type alias
using GLObjectRef = std::shared_ptr<GLObject>;

/// Create an Object in GPU memory
GLObject create_gl_object(const Vertex vertices[], const size_t num_vertices, const GLushort indices[], const size_t num_indices, GLenum usage = GL_STATIC_DRAW)
{
    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, num_vertices * sizeof(Vertex), vertices, usage);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)offsetof(Vertex, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)offsetof(Vertex, texcoord));
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, num_indices * sizeof(GLushort), indices, usage);
    glBindVertexArray(0);
    return {vao, vbo, ebo, num_vertices, num_indices};
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Geometry

/// Generate a QUAD with given dimensions and texture offsets
constexpr auto gen_quad_geometry(glm::vec2 v, glm::vec2 to, glm::vec2 ts) -> std::tuple<std::array<Vertex, 4>, std::array<GLushort, 6>>
{
    std::array<Vertex, 4> vertices = {
        Vertex{.pos = { 0.f,  0.f }, .texcoord = { to.x + 0.0f, to.y + 0.0f }},
        Vertex{.pos = { 0.f,  v.y }, .texcoord = { to.x + 0.0f, to.y + ts.y }},
        Vertex{.pos = { v.x,  0.f }, .texcoord = { to.x + ts.x, to.y + 0.0f }},
        Vertex{.pos = { v.x,  v.y }, .texcoord = { to.x + ts.x, to.y + ts.y }},
    };
    std::array<GLushort, 6> indices = {
        0, 1, 2,
        2, 1, 3
    };
    return {vertices, indices};
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Sprites

/// Generate quad vertices for a spritesheet texture with frames laid out linearly.
/// count=3:        .texcoord (U,V)
/// (0,1) +-----+-----+-----+ (1,1)
///       |     |     |     |
///       |  1  |  2  |  3  |
///       |     |     |     |
/// (0,0) +-----+-----+-----+ (1,0)
auto gen_sprite_quads(size_t count, glm::vec2 v, glm::vec2 to, glm::vec2 ts) -> std::tuple<std::vector<Vertex>, std::vector<GLushort>>
{
    float offx = ts.x / count;
    std::vector<Vertex> vertices;
    std::vector<GLushort> indices;
    vertices.reserve(4 * count);
    indices.reserve(6 * count);
    for (size_t i = 0; i < count; i++) {
        vertices.emplace_back(Vertex{ .pos = { -v.x, -v.y }, .texcoord = { to.x + (i+0)*offx, to.y + 0.0f } });
        vertices.emplace_back(Vertex{ .pos = { -v.x, +v.y }, .texcoord = { to.x + (i+0)*offx, to.y + ts.y } });
        vertices.emplace_back(Vertex{ .pos = { +v.x, -v.y }, .texcoord = { to.x + (i+1)*offx, to.y + 0.0f } });
        vertices.emplace_back(Vertex{ .pos = { +v.x, +v.y }, .texcoord = { to.x + (i+1)*offx, to.y + ts.y } });
        for (auto v : {0, 1, 2, 2, 1, 3})
            indices.emplace_back(4*i+v);
    }
    return {vertices, indices};
}

/// Information required to render one frame of a Sprite Animation
struct SpriteFrame {
    float duration;        // duration in seconds, negative is infinite
    size_t ebo_offset;     // offset to the first index of this frame in the EBO
    size_t ebo_count;      // number of elements to render since first index
    size_t next_frame_idx; // next frame index in array of frames
};

/// Control data required for a single Sprite Animation object
struct SpriteAnimation {
    bool freeze;
    float last_transit_dt; // deltatime between last transition and now
    size_t curr_frame_idx; // current frame index
    std::vector<SpriteFrame> frames;

    /// Transition frames
    void update_frame(float dt) {
        if (freeze) return;
        last_transit_dt += dt;
        SpriteFrame& curr_frame = frames[curr_frame_idx];
        if (last_transit_dt >= curr_frame.duration) {
            last_transit_dt -= curr_frame.duration;
            curr_frame_idx = curr_frame.next_frame_idx % frames.size();
        }
    }

    /// Get current sprite frame
    SpriteFrame& curr_frame() { return frames[curr_frame_idx]; }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Textures

/// GLTexture type alias
using GLTexture = GLuint;
/// GLObject reference type alias
using GLTextureRef = std::shared_ptr<GLTexture>;

/// Read file and upload RGB/RBGA texture to GPU memory
auto load_rgba_texture(const std::string& inpath) -> std::optional<GLTexture>
{
    const std::string filepath = ASSETS_PATH + "/"s + inpath;
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(filepath.data(), &width, &height, &channels, 0);
    if (!data) {
        fprintf(stderr, "Failed to load texture path (%s)\n", filepath.data());
        return std::nullopt;
    }
    GLenum type = (channels == 4) ? GL_RGBA : GL_RGB;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, type, width, height, 0, type, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);
    return texture;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Window | Viewport | Camera

/// Window controller
struct Window {
    struct GLFWwindow* glfw;
    glm::uvec2 size;

    /// Get window aspect ratio
    float aspect_ratio() const { return (float)size.x / size.y; }
    float aspect_ratio_inverse() const { return (float)size.y / size.x; }
};

/// Viewport controller
struct Viewport {
    glm::uvec2 offset;
    glm::uvec2 size;

    /// Get viewport aspect ratio
    float aspect_ratio() const { return (float)size.x / size.y; }
    float aspect_ratio_inverse() const { return (float)size.y / size.x; }
};

/// Camera controller
struct Camera {
    glm::vec3 canvas_size;
    glm::mat4 projection;
    glm::mat4 view;

    /// Create Orthographic Camera
    static Camera create(float aspect_ratio, float zoom) {
        auto canvas = glm::vec3(20.f, 20.f / aspect_ratio, 1000.f);
        return {
            .canvas_size = canvas,
            .projection = glm::ortho(-canvas.x/2.f * zoom, +canvas.x/2.f * zoom, -canvas.y/2.f * zoom, +canvas.y/2.f * zoom, -canvas.z, +canvas.z),
            .view = glm::inverse(glm::mat4(1.0f)),
        };
    }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Components

/// Transform component
struct Transform {
    glm::vec2 position {0.0f};
    glm::vec2 scale    {1.0f};
    glm::quat rotation {1.0f, glm::vec3(0.0f)};

    glm::mat4 matrix() {
        glm::mat4 translation_mat = glm::translate(glm::mat4(1.0f), glm::vec3(position, 0.f));
        glm::mat4 rotation_mat = glm::toMat4(rotation);
        glm::mat4 scale_mat = glm::scale(glm::mat4(1.0f), glm::vec3(scale, 0.f));
        return translation_mat * rotation_mat * scale_mat;
    }
};

/// Motion component
struct Motion {
    glm::vec2 velocity     {0.0f};
    glm::vec2 acceleration {0.0f};
};

/// Gravity component
struct Gravity {
};

/// Highlight component
struct Highlight {
};

/// Timed Action
struct TimedAction {
    float tick_dt;  // deltatime between last round and now
    float duration; // time to go off, in seconds
    std::function<void(struct Game& game, float dt, float time)> action  { [](auto&&...){} };

    /// Update timer and invoke action on expire
    void update(Game& game, float dt, float time) {
        tick_dt += dt;
        if (tick_dt >= duration) {
            tick_dt -= duration;
            action(game, dt, time);
        }
    }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Forward-declarations

/// GLFW_KEY_*
using Key = int;
/// Key event handler
using KeyHandler = std::function<void(struct Game&, int key, int action, int mods)>;
/// Map key code to event handlers
using KeyHandlerMap = std::unordered_map<Key, KeyHandler>;
/// Map key to its state, pressed = true, released = false
using KeyStateMap = std::unordered_map<Key, bool>;

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Game

enum class GameMode {
    COLLECT_BOOKS,
    CREATIVE,
};

/// Game Object represents an Entity's data
struct GameObject {
    Transform transform;
    Motion motion;
    GLObjectRef glo;
    GLTextureRef texture;
    std::optional<SpriteAnimation> sprite_animation;
    std::optional<Gravity> gravity;
    std::optional<Highlight> highlight;
};

enum ObjectType {
    AIR = 0,
    GRASS,
    STONE,
    WOOD,
    WOODPLANK,
    BOOK,
    COUNT,
};

struct Map {
    glm::uvec3 size;
    std::vector<std::vector<std::vector<ObjectType>>> tilemap;
};

/// Generic Scene structure
struct Scene {
    glm::vec4 bg_color;
    std::vector<std::vector<std::vector<GameObject>>> platform;
    glm::uvec3 player_idx;
    std::optional<glm::uvec3> highlight_idx;
    GameObject& player() { auto p = player_idx; return platform[p.x][p.y][p.z]; }
};

/// All Game data
struct Game {
    bool over;
    GameMode mode;
    Window window;
    Viewport viewport;
    glm::vec2 cursor;
    float zoom;
    GLuint shader_program;
    GLObjectRef canvas_quad_glo;
    GLTextureRef white_texture;
    GLTextureRef black_texture;
    GLTextureRef block_texture;
    std::optional<Camera> camera;
    std::optional<Map> map;
    std::optional<Scene> scene;
    std::optional<KeyStateMap> key_states;
    std::vector<TimedAction> timed_actions;
    int books_collected_count;
    bool debug_triangles;
    ObjectType target_objtype;
    GameObject target_obj;
};

/// Load entire map
Map load_map(const Game& game)
{
    Map map;
    map.size = {20, 20, 10};
    map.tilemap = std::vector(map.size.x, std::vector(map.size.y, std::vector(map.size.z, ObjectType::AIR)));

    ObjectType ground_blocks[] = {
        ObjectType::GRASS,
        ObjectType::GRASS,
        ObjectType::GRASS,
        ObjectType::STONE,
        ObjectType::STONE,
        ObjectType::STONE,
        ObjectType::WOODPLANK,
    };

    for (int i = 0; i < (int)map.tilemap.size(); i++) {
        for (int j = 0; j < (int)map.tilemap[i].size(); j++) {
            int index = 0;
            if (game.mode == GameMode::COLLECT_BOOKS)
                index = std::rand() % sizeof(ground_blocks) / sizeof(ground_blocks[0]);
            map.tilemap[i][j][0] = ground_blocks[index];
        }
    }

    //size_t i = 5;
    //size_t j = 8;
    //map.tilemap[i][j][1] = ObjectType::STONE;
    //map.tilemap[i][j][2] = ObjectType::STONE;
    //map.tilemap[i][j][3] = ObjectType::STONE;

    //map.tilemap[i][j+1][1] = ObjectType::STONE;
    //map.tilemap[i][j+1][2] = ObjectType::STONE;
    //map.tilemap[i][j+1][3] = ObjectType::STONE;
    //map.tilemap[i-1][j+1][3] = ObjectType::WOODPLANK;
    //map.tilemap[i-1][j][3] = ObjectType::WOODPLANK;

    if (game.mode == GameMode::COLLECT_BOOKS) {
        // spawn books
        for (int x = 0; x < 10; x++) {
            int i = std::rand() % map.size.x;
            int j = std::rand() % map.size.y;
            if (map.tilemap[i][j][1] == ObjectType::BOOK) {
                x--;
                continue;
            }
            map.tilemap[i][j][1] = ObjectType::BOOK;
        }
    }

    return map;
}

constexpr glm::vec2 blocks_tileset_size{526.f, 232.f};
constexpr glm::vec2 blocks_tile_size{52.f, 58.f};
constexpr float tile_surface_height = 26.f;

static const std::unordered_map<ObjectType, glm::vec2> blocks_offset = {
    {ObjectType::GRASS, {0.f, 0.f}},
    {ObjectType::STONE, {53.f, 0.f}},
    {ObjectType::WOODPLANK, {53.f, 116.f}},
    {ObjectType::WOOD, {0.f, 116.f}},
};

GameObject create_block_object(const Game& game, glm::ivec3 p, ObjectType block)
{
    int i = p.x, j = p.y, k = p.z;
    GameObject obj{};
    auto [vertices, indices] = gen_quad_geometry(glm::vec2(1.f), blocks_offset.at(block) / blocks_tileset_size, blocks_tile_size / blocks_tileset_size);
    obj.glo = std::make_shared<GLObject>(create_gl_object(vertices.data(), vertices.size(), indices.data(), indices.size()));
    obj.texture = game.block_texture;
    obj.transform.position.x = i * 0.5f + j * 0.5f - /*canvas offset*/(game.map->tilemap.size() / 2.f);
    obj.transform.position.y = i * 0.25f - j * 0.25f + k * 0.5f - /*canvas offset*/0.5f;
    obj.transform.scale = glm::vec2(1.f);
    return obj;
}

GameObject create_book_object(const Game& game, glm::ivec3 p)
{
    int i = p.x, j = p.y, k = p.z;
    GameObject obj{};
    obj.transform.scale = glm::vec2(0.4f);
    obj.transform.position.x = i * 0.5f + j * 0.5f - (game.map->tilemap.size() / 2.f) + 0.5f;
    obj.transform.position.y = i * 0.25f - j * 0.25f + k * 0.5f + 0.3f;
    obj.texture = std::make_shared<GLTexture>(*load_rgba_texture("mine-book.png"));
    constexpr glm::vec2 sprite_size = {467.f, 42};
    constexpr glm::vec2 sprite_frame_size = {31.f, 42.f};
    auto [vertices, indices] = gen_sprite_quads(5, glm::vec2(sprite_frame_size.x / sprite_frame_size.y, 1.f), glm::vec2(0.f), glm::vec2(5.f, 1.f) * sprite_frame_size / sprite_size);
    obj.glo = std::make_shared<GLObject>(create_gl_object(vertices.data(), vertices.size(), indices.data(), indices.size()));
    float d1 = (float)(std::rand() % 10) / 10.f;
    obj.sprite_animation = SpriteAnimation{
        .freeze = false,
        .last_transit_dt = 0,
        .curr_frame_idx = 0,
        .frames = std::vector<SpriteFrame>{
            { .duration = (0.75f + d1), .ebo_offset = 00, .ebo_count = 6, .next_frame_idx = 1 },
            { .duration = 0.15, .ebo_offset = 12, .ebo_count = 6, .next_frame_idx = 2 },
            { .duration = 0.15, .ebo_offset = 24, .ebo_count = 6, .next_frame_idx = 3 },
            { .duration = 0.15, .ebo_offset = 36, .ebo_count = 6, .next_frame_idx = 4 },
            { .duration = 0.75, .ebo_offset = 48, .ebo_count = 6, .next_frame_idx = 5 },
            { .duration = 0.15, .ebo_offset = 36, .ebo_count = 6, .next_frame_idx = 6 },
            { .duration = 0.15, .ebo_offset = 24, .ebo_count = 6, .next_frame_idx = 7 },
            { .duration = 0.15, .ebo_offset = 12, .ebo_count = 6, .next_frame_idx = 0 },
        },
    };
    return obj;
}

GameObject create_game_object(const Game& game, glm::ivec3 p, ObjectType block)
{
    if (block == ObjectType::BOOK) {
        return create_book_object(game, p);
    }
    else {
        return create_block_object(game, p, block);
    }
}

GameObject create_player_object(const Game& game, glm::ivec3 p)
{
    int i = p.x, j = p.y, k = p.z;
    GameObject obj;
    obj.transform.scale = glm::vec2(0.7f);
    obj.transform.position.x = i * 0.5f + j * 0.5f - (game.map->tilemap.size() / 2.f) + 0.5f;
    obj.transform.position.y = i * 0.25f - j * 0.25f + k * 0.5f + 0.3f;
    obj.texture = std::make_shared<GLTexture>(*load_rgba_texture("mine-steve.png"));
    constexpr glm::vec2 sprite_frame_size = {38.f, 72.f};
    auto [vertices, indices] = gen_sprite_quads(8, glm::vec2(sprite_frame_size.x / sprite_frame_size.y, 1.f), glm::vec2(0.f), glm::vec2(1.f));
    obj.glo = std::make_shared<GLObject>(create_gl_object(vertices.data(), vertices.size(), indices.data(), indices.size()));
    obj.sprite_animation = SpriteAnimation{
      .freeze = true,
      .last_transit_dt = 0,
      .curr_frame_idx = 0,
      .frames = std::vector<SpriteFrame>{
        { .duration = -0.10, .ebo_offset = 00, .ebo_count = 6, .next_frame_idx = 0 },
        { .duration = -0.10, .ebo_offset = 12, .ebo_count = 6, .next_frame_idx = 0 },
        { .duration = -0.10, .ebo_offset = 24, .ebo_count = 6, .next_frame_idx = 0 },
        { .duration = -0.10, .ebo_offset = 36, .ebo_count = 6, .next_frame_idx = 0 },
        { .duration = -0.10, .ebo_offset = 48, .ebo_count = 6, .next_frame_idx = 0 },
        { .duration = -0.10, .ebo_offset = 60, .ebo_count = 6, .next_frame_idx = 0 },
        { .duration = -0.10, .ebo_offset = 72, .ebo_count = 6, .next_frame_idx = 0 },
        { .duration = -0.10, .ebo_offset = 84, .ebo_count = 6, .next_frame_idx = 0 },
      },
    };
    return obj;
}

/// Load Main Scene
Scene load_scene(const Game& game)
{
    Scene scene;
    scene.bg_color = glm::vec4(glm::vec3(0x2E, 0x3E, 0x69) / glm::vec3(255.f), 1.0f);

    auto& platform = scene.platform;
    for (int i = 0; i < (int)game.map->tilemap.size(); i++) {
        platform.resize(game.map->tilemap.size());
        for (int j = 0; j < (int)game.map->tilemap[i].size(); j++) {
            platform[i].resize(game.map->tilemap[i].size());
            for (int k = 0; k < (int)game.map->tilemap[i][j].size(); k++) {
                platform[i][j].resize(game.map->tilemap[i][j].size());
                ObjectType block = game.map->tilemap[i][j][k];
                if (block == ObjectType::AIR) continue;
                platform[i][j][k] = create_game_object(game, {i, j, k}, block);
            }
        }
    }

    { // Player
        // random position
        int i, j, k = 1;
        do {
            i = std::rand() % game.map->size.x;
            j = std::rand() % game.map->size.y;
            if (game.map->tilemap[i][j][1] == ObjectType::AIR && game.map->tilemap[i][j][2] == ObjectType::AIR)
                break;
        } while(1);
        // create player object
        scene.player_idx = glm::vec3(i, j, k);
        platform[i][j][k] = create_player_object(game, {i, j, k});
    }

    return scene;
}

/// Drop a tile block with gravity
void drop_tile_block(Game& game, float dt, float time)
{
    do {
        int i = std::rand() % game.map->size.x;
        int j = std::rand() % game.map->size.y;
        if (game.map->tilemap[i][j][1] == ObjectType::BOOK || glm::uvec3(i, j, 1) == game.scene->player_idx) {
          continue;
        }
        auto& platform = game.scene->platform;
        for (size_t k = 0; k < game.map->size.z; k++) {
            if (platform[i][j][k].glo)
                platform[i][j][k].gravity = Gravity{};
        }
        break;
    } while(1);
}

/// Initialize Game
int game_init(Game& game, GLFWwindow* window)
{
    std::srand(std::time(nullptr));
    game.over = false;
    game.mode = GameMode::COLLECT_BOOKS;
    game.window.glfw = window;
    game.window.size = glm::uvec2(WIDTH, HEIGHT);
    game.viewport.size = glm::uvec2(WIDTH, HEIGHT);
    game.viewport.offset = glm::uvec2(0);
    game.cursor = glm::vec2(0.f);
    game.zoom = 1.0f;
    game.shader_program = load_shader_program();
    auto [quad_vertices, quad_indices] = gen_quad_geometry(glm::vec2(1.f), glm::vec2(0.f), glm::vec2((float)WIDTH / (float)HEIGHT, 1.0f));
    game.canvas_quad_glo = std::make_shared<GLObject>(create_gl_object(quad_vertices.data(), quad_vertices.size(), quad_indices.data(), quad_indices.size()));
    game.white_texture = std::make_shared<GLTexture>(*load_rgba_texture("white.png"));
    game.black_texture = std::make_shared<GLTexture>(*load_rgba_texture("black.png"));
    game.block_texture = std::make_shared<GLTexture>(*load_rgba_texture("mine-blocks.png"));
    game.camera = Camera::create(game.viewport.aspect_ratio(), 1.f);
    game.map = load_map(game);
    game.scene = load_scene(game);
    game.key_states = KeyStateMap(GLFW_KEY_LAST);
    game.books_collected_count = 0;
    game.debug_triangles = false;
    game.target_objtype = ObjectType::STONE;
    game.target_obj = create_block_object(game, glm::ivec3(0), ObjectType::STONE);
    game.target_obj.transform.position = -game.camera->canvas_size / 2.f + glm::vec3(0.5f);

    game.timed_actions = {};
    game.timed_actions.push_back(TimedAction{
        .tick_dt = 0,
        .duration = 0.4,
        .action =
            [](Game &game, auto &&...args) {
              if (game.mode != GameMode::COLLECT_BOOKS) return;
              drop_tile_block(game, args...);
            },
    });
    game.timed_actions.push_back(TimedAction{
        .tick_dt = 0,
        .duration = 3,
        .action =
            [](Game &game, auto &&...) {
                if (game.mode != GameMode::COLLECT_BOOKS) return;
                if (game.books_collected_count == 10) {
                    std::cout << "YOU WIN" << std::endl;
                    game.over = true;
                }
                else if (game.scene->player().gravity) {
                    std::cout << "GAME OVER" << std::endl;
                    game.over = true;
                }
            },
    });

    std::cout << "GAME START" << std::endl;

    return 0;
}

void game_restart(Game& game, GameMode mode)
{
    game.over = false;
    game.mode = mode;
    game.map = load_map(game);
    game.scene = load_scene(game);
    game.books_collected_count = 0;

    std::cout << "GAME RESTART: Mode "
        << (mode == GameMode::CREATIVE ? "CREATIVE" : "COLLECT_BOOKS")
        << std::endl;
}

/// Game tick update
void game_update(Game& game, float dt, float time)
{
    if (game.over) return;

    // Run timed actions
    for (auto& timed_action : game.timed_actions) {
        timed_action.update(game, dt, time);
    }

    // Update all objects
    for (int i = game.map->size.x-1; i >=0 ; i--) {
        for (int j = 0; j < (int)game.map->size.y; j++) {
            for (int k = 0; k < (int)game.map->size.z; k++) {
                auto* obj = &game.scene->platform[i][j][k];
                // Gravity system
                if (obj->gravity) {
                    constexpr float kGravityFactor = 10.f;
                    obj->motion.acceleration.y = -kGravityFactor;
                }
                // Motion system
                obj->motion.velocity += obj->motion.acceleration * dt;
                obj->transform.position += obj->motion.velocity * dt;
                // Sprite Animation system
                if (obj->sprite_animation) {
                    obj->sprite_animation->update_frame(dt);
                }
            }
        }
    }

    if (game.target_obj.sprite_animation)
        game.target_obj.sprite_animation->update_frame(dt);
}

/// Prepare to render
void begin_render(Game& game)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const glm::vec4& color = game.scene->bg_color;
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

/// Upload camera matrix to shader
void set_camera(const GLuint shader, const Camera& camera)
{
    glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, glm::value_ptr(camera.view));
    glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, glm::value_ptr(camera.projection));
}

/// Render a textured GLObject
void draw_object(const GLuint shader, const GLTexture& texture, const GLObject& glo, const glm::mat4& model,
                 const std::optional<SpriteFrame> sprite, const glm::vec4 color = glm::vec4(1.f))
{
    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform4fv(glGetUniformLocation(shader, "color"), 1, glm::value_ptr(color));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(glo.vao);
    size_t ebo_offset = sprite ? sprite->ebo_offset : 0;
    size_t ebo_count = sprite ? sprite->ebo_count : glo.num_indices;
    glDrawElements(GL_TRIANGLES, ebo_count, GL_UNSIGNED_SHORT, (const void*)ebo_offset);
}

/// Render triangles for all objects
void render_triangles(Game& game, GLuint shader)
{
    glLineWidth(1.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    for (int i = game.map->size.x-1; i >=0 ; i--) {
        for (int j = 0; j < (int)game.map->size.y; j++) {
            for (int k = 0; k < (int)game.map->size.z; k++) {
                auto* obj = &game.scene->platform[i][j][k];
                if (obj->glo) {
                    glBindVertexArray(obj->glo->vao);
                    Transform transform = obj->transform;
                    draw_object(shader, *game.black_texture, *obj->glo, transform.matrix(), std::nullopt);
                }
            }
        }
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}


/// Render surface of tiles
void render_surface(Game& game, GLuint shader)
{
    glLineWidth(1.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    float h/*height_normal*/ = tile_surface_height / blocks_tile_size.y;
    std::array<Vertex, 4> vertices = {
        Vertex{.pos = { 0.0f,  0.5f * h }, .texcoord = { 0.0f, 0.5f }},
        Vertex{.pos = { 0.5f,  0.0f     }, .texcoord = { 0.5f, 0.0f }},
        Vertex{.pos = { 1.0f,  0.5f * h }, .texcoord = { 1.0f, 0.5f }},
        Vertex{.pos = { 0.5f,  1.0f * h }, .texcoord = { 0.5f, 1.0f }},
    };
    std::array<GLushort, 6> indices = {
        0, 1, 3,
        1, 3, 2
    };
    GLObject glo = create_gl_object(vertices.data(), vertices.size(), indices.data(), indices.size(), GL_STREAM_DRAW);
    glBindVertexArray(glo.vao);
    for (int i = game.map->size.x-1; i >=0 ; i--) {
        for (int j = 0; j < (int)game.map->size.y; j++) {
            for (int k = 0; k < (int)game.map->size.z; k++) {
                auto* obj = &game.scene->platform[i][j][k];
                if (obj->glo) {
                    Transform transform = obj->transform;
                    transform.position.y += 1.f - h;
                    draw_object(shader, *game.black_texture, glo, transform.matrix(), std::nullopt);
                }
            }
        }
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

/// Render Game scene
void game_render(Game& game)
{
    begin_render(game);
    GLuint shader = game.shader_program;
    glUseProgram(shader);
    set_camera(shader, *game.camera);

    for (int i = game.map->size.x-1; i >=0 ; i--) {
        for (int j = 0; j < (int)game.map->size.y; j++) {
            for (int k = 0; k < (int)game.map->size.z; k++) {
                auto* obj = &game.scene->platform[i][j][k];
                if (obj->glo && obj->texture) {
                    //transperency
                    //auto color = (k > 0 && glm::uvec3(i, j, k) != game.scene->player_idx) ? glm::vec4(glm::vec3(1.0f), 0.6f) : glm::vec4(1.f);
                    auto color = glm::vec4(1.f);
                    if (obj->highlight)
                        color = glm::vec4(glm::vec3(0.65f), 1.f);
                    auto sprite = obj->sprite_animation ? std::make_optional<SpriteFrame>(obj->sprite_animation->curr_frame()) : std::nullopt;
                    draw_object(shader, *obj->texture, *obj->glo, obj->transform.matrix(), sprite, color);
                }
            }
        }
    }

    if (game.mode == GameMode::CREATIVE) {
        GameObject& obj = game.target_obj;
        auto sprite = obj.sprite_animation ? std::make_optional<SpriteFrame>(obj.sprite_animation->curr_frame()) : std::nullopt;
        draw_object(shader, *obj.texture, *obj.glo, obj.transform.matrix(), sprite);
    }

    if (game.debug_triangles)
        render_surface(game, shader);
}

/// Game Loop, only returns when game finishes
int game_loop(GLFWwindow* window)
{
    Game game;
    int ret = game_init(game, window);
    if (ret) return ret;
    glfwSetWindowUserPointer(window, &game);
    float last_time = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        float now_time = glfwGetTime();
        float dt = now_time - last_time;
        last_time = now_time;
        glfwPollEvents();
        game_update(game, dt, now_time);
        game_render(game);
        glfwSwapBuffers(window);
    }
    glfwSetWindowUserPointer(window, NULL);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Events

void key_arrows_handler(struct Game& game, int key, int action, int mods)
{
    if (game.scene->player().gravity) return;

    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_UP) {
            int i = game.scene->player_idx.x + 1, j = game.scene->player_idx.y, k = game.scene->player_idx.z;
            game.scene->platform[i-1][j][k].sprite_animation->curr_frame_idx = 3;
            if (i < (int)game.map->size.x ) {
                if ((!game.scene->platform[i][j][k].glo && !game.scene->platform[i][j][k+1].glo) || (game.mode == GameMode::COLLECT_BOOKS && game.map->tilemap[i][j][k] == ObjectType::BOOK)) {
                    game.scene->player_idx.x = i;
                    auto& obj = game.scene->platform[i][j][k] = std::move(game.scene->platform[i-1][j][k]);
                    obj.transform.position.x = i * 0.5f + j * 0.5f - (game.map->tilemap.size() / 2.f) + 0.5f;
                    obj.transform.position.y = i * 0.25f - j * 0.25f + k * 0.5f + 0.3f;
                    if (game.scene->platform[i][j][0].gravity) { obj.gravity = Gravity{}; }
                    if (game.mode == GameMode::COLLECT_BOOKS && game.map->tilemap[i][j][k] == ObjectType::BOOK) {
                        game.map->tilemap[i][j][k] = ObjectType::AIR;
                        game.books_collected_count++;
                    }
                }
            }
        }
        else if (key == GLFW_KEY_DOWN) {
            int i = game.scene->player_idx.x - 1, j = game.scene->player_idx.y, k = game.scene->player_idx.z;
            game.scene->platform[i+1][j][k].sprite_animation->curr_frame_idx = 1;
            if (i >= 0) {
                if ((!game.scene->platform[i][j][k].glo && !game.scene->platform[i][j][k+1].glo) || (game.mode == GameMode::COLLECT_BOOKS && game.map->tilemap[i][j][k] == ObjectType::BOOK)) {
                    game.scene->player_idx.x = i;
                    auto& obj = game.scene->platform[i][j][k] = std::move(game.scene->platform[i+1][j][k]);
                    obj.transform.position.x = i * 0.5f + j * 0.5f - (game.map->tilemap.size() / 2.f) + 0.5f;
                    obj.transform.position.y = i * 0.25f - j * 0.25f + k * 0.5f + 0.3f;
                    if (game.scene->platform[i][j][0].gravity) { obj.gravity = Gravity{}; }
                    if (game.mode == GameMode::COLLECT_BOOKS && game.map->tilemap[i][j][k] == ObjectType::BOOK) {
                        game.map->tilemap[i][j][k] = ObjectType::AIR;
                        game.books_collected_count++;
                    }
                }
            }
        }
        else if (key == GLFW_KEY_RIGHT) {
            int i = game.scene->player_idx.x, j = game.scene->player_idx.y + 1, k = game.scene->player_idx.z;
            game.scene->platform[i][j-1][k].sprite_animation->curr_frame_idx = 0;
            if (j < (int)game.map->size.y) {
                if ((!game.scene->platform[i][j][k].glo && !game.scene->platform[i][j][k+1].glo) || (game.mode == GameMode::COLLECT_BOOKS && game.map->tilemap[i][j][k] == ObjectType::BOOK)) {
                    game.scene->player_idx.y = j;
                    auto& obj = game.scene->platform[i][j][k] = std::move(game.scene->platform[i][j-1][k]);
                    obj.transform.position.x = i * 0.5f + j * 0.5f - (game.map->tilemap.size() / 2.f) + 0.5f;
                    obj.transform.position.y = i * 0.25f - j * 0.25f + k * 0.5f + 0.3f;
                    if (game.scene->platform[i][j][0].gravity) { obj.gravity = Gravity{}; }
                    if (game.mode == GameMode::COLLECT_BOOKS && game.map->tilemap[i][j][k] == ObjectType::BOOK) {
                        game.map->tilemap[i][j][k] = ObjectType::AIR;
                        game.books_collected_count++;
                    }
                }
            }
        }
        else if (key == GLFW_KEY_LEFT) {
            int i = game.scene->player_idx.x, j = game.scene->player_idx.y - 1, k = game.scene->player_idx.z;
            game.scene->platform[i][j+1][k].sprite_animation->curr_frame_idx = 2;
            if (j >= 0) {
                if ((!game.scene->platform[i][j][k].glo && !game.scene->platform[i][j][k+1].glo) || (game.mode == GameMode::COLLECT_BOOKS && game.map->tilemap[i][j][k] == ObjectType::BOOK)) {
                    game.scene->player_idx.y = j;
                    auto& obj = game.scene->platform[i][j][k] = std::move(game.scene->platform[i][j+1][k]);
                    obj.transform.position.x = i * 0.5f + j * 0.5f - (game.map->tilemap.size() / 2.f) + 0.5f;
                    obj.transform.position.y = i * 0.25f - j * 0.25f + k * 0.5f + 0.3f;
                    if (game.scene->platform[i][j][0].gravity) { obj.gravity = Gravity{}; }
                    if (game.mode == GameMode::COLLECT_BOOKS && game.map->tilemap[i][j][k] == ObjectType::BOOK) {
                        game.map->tilemap[i][j][k] = ObjectType::AIR;
                        game.books_collected_count++;
                    }
                }
            }
        }
    }
}

void key_space_handler(struct Game& game, int key, int action, int mods)
{
    if (game.mode != GameMode::CREATIVE) return;
    if (action != GLFW_PRESS) return;

    ((int&)(game.target_objtype))++;

    if (game.target_objtype == ObjectType::COUNT) {
        game.target_objtype = (ObjectType)((int)ObjectType::AIR + 1);
    }

    game.target_obj = create_game_object(game, glm::vec3(0), game.target_objtype);
    game.target_obj.transform.position = -game.camera->canvas_size / 2.f + glm::vec3(0.5f);
}

void key_f5_handler(struct Game& game, int key, int action, int mods)
{
  if (action == GLFW_PRESS)
    game.debug_triangles = !game.debug_triangles;
}

/// Handle Key input event
void key_event_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(game->window.glfw, 1);
    }
    else if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        game_restart(*game, game->mode);
    }
    else if (key == GLFW_KEY_C && action == GLFW_PRESS) {
        if (game->mode != GameMode::CREATIVE)
            game_restart(*game, GameMode::CREATIVE);
    }
    else if (key == GLFW_KEY_B && action == GLFW_PRESS) {
        if (game->mode != GameMode::COLLECT_BOOKS)
            game_restart(*game, GameMode::COLLECT_BOOKS);
    }

    if (game->over) return;

    if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT || key == GLFW_KEY_UP || key == GLFW_KEY_DOWN) {
        key_arrows_handler(*game, key, action, mods);
    }
    else if (key == GLFW_KEY_SPACE) {
        key_space_handler(*game, key, action, mods);
    }
    else if (key == GLFW_KEY_F5) {
        key_f5_handler(*game, key, action, mods);
    }

    game->key_states.value()[key] = (action == GLFW_PRESS);
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);

/// Handle Mouse click events
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    auto game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;

    if (game->mode != GameMode::CREATIVE) return;

    if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (game->scene->highlight_idx) {
            glm::uvec3 s = *game->scene->highlight_idx;
            glm::uvec3 v = s; v.z += 1;
            if (v != game->scene->player_idx) {
                auto& block_above = game->map->tilemap[v.x][v.y][v.z];
                if (block_above == ObjectType::AIR) {
                    block_above = game->target_objtype;
                    game->scene->platform[v.x][v.y][v.z] = create_game_object(*game, v, block_above);
                }
            }
        }
    }
    else if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
        if (game->scene->highlight_idx) {
            glm::uvec3 v = *game->scene->highlight_idx;
            if (v.z != 0) {
                game->map->tilemap[v.x][v.y][v.z] = ObjectType::AIR;
                game->scene->platform[v.x][v.y][v.z].highlight = std::nullopt;
                auto& obj = game->scene->platform[v.x][v.y][v.z];
                obj.glo = nullptr;
                double mx, my;
                glfwGetCursorPos(window, &mx, &my);
                cursor_position_callback(game->window.glfw, mx, my);
            }
        }
    }
}

/// Handle cursor movement events
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos)
{
    auto game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;
    game->cursor = {xpos, ypos};

    if (game->mode != GameMode::CREATIVE) return;

    ypos = game->window.size.y - ypos;
    glm::vec2 cursor_pos{xpos, ypos};
    glm::vec2 normal_pos = cursor_pos / glm::vec2(game->window.size);
    glm::vec2 canvas_pos = normal_pos * glm::vec2(game->camera->canvas_size) - glm::vec2(game->camera->canvas_size) / 2.f;
    canvas_pos *= glm::vec2(game->zoom);
    canvas_pos.y -= 1.25f - (tile_surface_height / blocks_tile_size.y);

    // cartesian to isometric
    float i = canvas_pos.x + 2.f * canvas_pos.y + (game->map->size.x / 2.f) + 1.f;
    float j = i - 4.f * canvas_pos.y - 2.f;
    float k = 0;

    glm::ivec3 mapsize = game->map->size;
    if (i >= 0 && i < mapsize.x && j >= 0 && j < mapsize.y && k >= 0 && k < mapsize.z) {

        // check other k layers
        for (int n = mapsize.z-1; n > 0; n--) {
            glm::vec3 p = {i - n, j + n, n};
            if (p.x < 0 || p.y >= mapsize.y || p.z >= mapsize.z)
                continue;
            auto& block = game->map->tilemap[p.x][p.y][p.z];
            if (block != ObjectType::AIR) {
                i = p.x;
                j = p.y;
                k = p.z;
            }
        }

        if (game->scene->highlight_idx) {
            glm::uvec3 v = *game->scene->highlight_idx;
            game->scene->platform[v.x][v.y][v.z].highlight = std::nullopt;
        }
        game->scene->platform[i][j][k].highlight = Highlight{};
        game->scene->highlight_idx = {i, j, k};
    }
}

/// Handle Scrool wheel events
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    auto game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;

    game->zoom += -yoffset * 0.05f;
    game->camera = Camera::create(game->window.aspect_ratio(), game->zoom);
}

/// Handle Window framebuffer resize event
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    auto game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;

    glViewport(0, 0, width, height);
    game->camera = Camera::create((float)width / (float)height, game->zoom);

    auto [quad_vertices, quad_indices] = gen_quad_geometry(glm::vec2(1.f), glm::vec2(0.f), glm::vec2((float)width / (float)height, 1.0f));
    game->canvas_quad_glo = std::make_shared<GLObject>(create_gl_object(quad_vertices.data(), quad_vertices.size(), quad_indices.data(), quad_indices.size()));

    game->window.size.y = height;
    game->window.size.x = width;
    game->viewport.size.x = (width);
    game->viewport.size.y = (height);
    game->viewport.offset.x = 0;
    game->viewport.offset.y = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Setup

/// Create window with GLFW
int create_window(GLFWwindow*& window)
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Mineiso", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create GLFW Window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_event_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSwapInterval(1); // vsync
    return 0;
}

/// Load OpenGL with GLEW
int load_opengl()
{
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to init GLEW" << std::endl;
        return -2;
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Main

int main()
{
    int ret;

    // Create Window ==========================================================
    GLFWwindow* window;
    ret = create_window(window);
    if (ret) return ret;

    // Load OpenGL ============================================================
    ret = load_opengl();
    if (ret) return ret;

    // Game Loop ==============================================================
    ret = game_loop(window);
    if (ret) return ret;

    // Terminate ==============================================================
    glfwTerminate();
    return EXIT_SUCCESS;
}

// vim: tabstop=4 shiftwidth=4
