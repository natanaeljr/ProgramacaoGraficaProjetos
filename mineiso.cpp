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

constexpr size_t WIDTH = 900, HEIGHT = 500;

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
uniform vec2 texoffset;
uniform vec4 color;
out vec4 frag_color;
void main(){
    frag_color = texture(texture0, texcoord + texoffset) * color;
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

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Collision

/// Axis-aligned Bounding Box component
/// in respect to the object's local space
/// (no rotation support)
///     +---+ max
///     | x |
/// min +---+    x = center = origin = transform.position
struct Aabb {
  glm::vec2 min {-1.0f};
  glm::vec2 max {+1.0f};

  Aabb transform(const glm::mat4& matrix) const {
    glm::vec2 a = matrix * glm::vec4(min, 0.0f, 1.0f);
    glm::vec2 b = matrix * glm::vec4(max, 0.0f, 1.0f);
    return Aabb{glm::min(a, b), glm::max(a, b)};
  }
};

/// Check for collision between two AABBs
bool collision(const Aabb& a, const Aabb& b)
{
  return a.min.x < b.max.x &&
         a.max.x > b.min.x &&
         a.min.y < b.max.y &&
         a.max.y > b.min.y;
}

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

/// Texture Slide component
struct TextureSlide {
    glm::vec2 velocity     {0.0f};
    glm::vec2 acceleration {0.0f};
};

/// Texture Offset component
struct TextureOffset {
    glm::vec2 vec {0.0f};
};

/// Entity State component
enum class EntityState {
    IDLE,
    WALKING,
    JUMPING,
};

/// Highlight component
struct Highlight {
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

/// Game Object represents an Entity's data
struct GameObject {
    Transform transform;
    Motion motion;
    GLObjectRef glo;
    GLTextureRef texture;
    std::optional<TextureSlide> texture_slide;
    std::optional<TextureOffset> texture_offset;
    std::optional<SpriteAnimation> sprite_animation;
    std::optional<EntityState> entity_state;
    std::optional<Gravity> gravity;
    std::optional<Aabb> aabb;
    std::optional<Highlight> highlight;
};

enum BlockIdx {
    AIR = 0,
    GRASS,
    STONE,
    WOOD,
    WOODPLANK,
};

struct Map {
    glm::uvec3 size;
    std::vector<std::vector<std::vector<BlockIdx>>> tilemap;
};

/// Lists of all Game Objects in a Scene, divised in layers, in order of render
struct ObjectLists {
    std::vector<GameObject> entity;
    /// Get all layers of objects
    auto all_lists() { return std::array{ &entity }; }
};

/// Generic Scene structure
struct Scene {
    ObjectLists objects;
    glm::vec4 bg_color;
    std::vector<std::vector<std::vector<GameObject>>> platform;
    glm::uvec3 player_idx;
    std::optional<glm::uvec3> highlight_idx;
    GameObject& player() { return objects.entity.front(); }
};

/// All Game data
struct Game {
    Window window;
    Viewport viewport;
    glm::vec2 cursor;
    float zoom;
    GLuint shader_program;
    GLObjectRef canvas_quad_glo;
    GLTextureRef white_texture;
    GLTextureRef black_texture;
    std::optional<Camera> camera;
    std::optional<Map> map;
    std::optional<Scene> scene;
    std::optional<KeyStateMap> key_states;
    bool debug_grid;
    bool debug_aabb;
    bool debug_triangles;
};

/// Load entire map
Map load_map(const Game& game)
{
    Map map;
    map.size = {20, 20, 10};
    map.tilemap = std::vector(map.size.x, std::vector(map.size.y, std::vector(map.size.z, BlockIdx::AIR)));

    for (int i = 0; i < (int)map.tilemap.size(); i++) {
        for (int j = 0; j < (int)map.tilemap[i].size(); j++) {
            map.tilemap[i][j][0] = BlockIdx::GRASS;
        }
    }

    // Add some randomness to the map
    std::srand(time(nullptr));
    //size_t i = std::rand() % 21;
    //size_t j = std::rand() % 21;
    size_t i = 5;
    size_t j = 8;
    map.tilemap[i][j][1] = BlockIdx::STONE;
    map.tilemap[i][j][2] = BlockIdx::STONE;
    map.tilemap[i][j][3] = BlockIdx::STONE;

    map.tilemap[i][j+1][1] = BlockIdx::STONE;
    map.tilemap[i][j+1][2] = BlockIdx::STONE;
    map.tilemap[i][j+1][3] = BlockIdx::STONE;
    //map.tilemap[i-1][j+1][1] = BlockIdx::WOODPLANK;
    //map.tilemap[i-1][j+1][2] = BlockIdx::WOODPLANK;
    map.tilemap[i-1][j+1][3] = BlockIdx::WOODPLANK;
    //map.tilemap[i-1][j][1] = BlockIdx::WOODPLANK;
    //map.tilemap[i-1][j][2] = BlockIdx::WOODPLANK;
    map.tilemap[i-1][j][3] = BlockIdx::WOODPLANK;

    return map;
}

constexpr glm::vec2 blocks_tileset_size{526.f, 232.f};
constexpr glm::vec2 blocks_tile_size{52.f, 58.f};
constexpr float tile_surface_height = 26.f;

static const std::unordered_map<BlockIdx, glm::vec2> blocks_offset = {
    {BlockIdx::GRASS, {0.f, 0.f}},
    {BlockIdx::STONE, {53.f, 0.f}},
    {BlockIdx::WOODPLANK, {53.f, 116.f}},
    {BlockIdx::WOOD, {0.f, 116.f}},
};

/// Load Main Scene
Scene load_scene(const Game& game)
{
    Scene scene;
    scene.bg_color = glm::vec4(glm::vec3(0x2E, 0x3E, 0x69) / glm::vec3(255.f), 1.0f);

    GLTextureRef block_texture = std::make_shared<GLTexture>(*load_rgba_texture("mine-blocks.png"));

    auto& platform = scene.platform;
    for (int i = 0; i < (int)game.map->tilemap.size(); i++) {
        platform.resize(game.map->tilemap.size());
        for (int j = 0; j < (int)game.map->tilemap[i].size(); j++) {
            platform[i].resize(game.map->tilemap[i].size());
            for (int k = 0; k < (int)game.map->tilemap[i][j].size(); k++) {
                platform[i][j].resize(game.map->tilemap[i][j].size());
                BlockIdx block = game.map->tilemap[i][j][k];
                if (block == BlockIdx::AIR) continue;
                auto& obj = platform[i][j][k];
                auto [vertices, indices] = gen_quad_geometry(glm::vec2(1.f), blocks_offset.at(block) / blocks_tileset_size, blocks_tile_size / blocks_tileset_size);
                obj.glo = std::make_shared<GLObject>(create_gl_object(vertices.data(), vertices.size(), indices.data(), indices.size()));
                obj.texture = block_texture;
                obj.transform.position.x = i * 0.5f + j * 0.5f - (game.map->tilemap.size() / 2.f);
                obj.transform.position.y = i * 0.25f - j * 0.25f + k * 0.5f;
                obj.transform.scale = glm::vec2(1.f);
            }
        }
    }

    { // Player Steve
        int i = game.map->size.x/2, j = game.map->size.y/2, k = 1;
        scene.player_idx = glm::vec3(i, j, k);
        auto& obj = platform[i][j][k];
        obj.transform.scale = glm::vec2(0.7f);
        obj.transform.position.x = i * 0.5f + j * 0.5f - (game.map->tilemap.size() / 2.f) + 0.5f;
        obj.transform.position.y = i * 0.25f - j * 0.25f + k * 0.5f + 0.8f;
        obj.texture = std::make_shared<GLTexture>(*load_rgba_texture("mine-steve.png"));
        //constexpr glm::vec2 sprite_size = {307.f, 72.f};
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
    }


    return scene;
}

/// Initialize Game
int game_init(Game& game, GLFWwindow* window)
{
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
    game.camera = Camera::create(game.viewport.aspect_ratio(), 1.f);
    game.map = load_map(game);
    game.scene = load_scene(game);
    game.key_states = KeyStateMap(GLFW_KEY_LAST);
    game.debug_grid = false;
    game.debug_aabb = false;
    game.debug_triangles = false;

    return 0;
}

/// Game tick update
void game_update(Game& game, float dt)
{
    // Update all objects
    for (auto* object_list : game.scene->objects.all_lists()) {
        for (auto& obj : *object_list) {
            // Gravity system
            if (obj.gravity && (!obj.entity_state || obj.entity_state != EntityState::JUMPING)) {
                constexpr float kGravityFactor = 20.f;
                obj.motion.acceleration.y = -kGravityFactor;
            }
            // Motion system
            obj.motion.velocity += obj.motion.acceleration * dt;
            obj.transform.position += obj.motion.velocity * dt;
            // Sprite Animation system
            if (obj.sprite_animation) {
                obj.sprite_animation->update_frame(dt);
            }
            // Texture Sliding system
            if (obj.texture_slide && obj.texture_offset) {
                obj.texture_slide->velocity += obj.texture_slide->acceleration * dt;
                obj.texture_offset->vec += obj.texture_slide->velocity * dt;
            }
        }
    }
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
                 const std::optional<TextureOffset> texoffset, const std::optional<SpriteFrame> sprite, const glm::vec4 color = glm::vec4(1.f))
{
    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glm::vec2 texoffset_vec = texoffset ? texoffset->vec : glm::vec2(0.f);
    glUniform2fv(glGetUniformLocation(shader, "texoffset"), 1, glm::value_ptr(texoffset_vec));
    glUniform4fv(glGetUniformLocation(shader, "color"), 1, glm::value_ptr(color));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(glo.vao);
    size_t ebo_offset = sprite ? sprite->ebo_offset : 0;
    size_t ebo_count = sprite ? sprite->ebo_count : glo.num_indices;
    glDrawElements(GL_TRIANGLES, ebo_count, GL_UNSIGNED_SHORT, (const void*)ebo_offset);
}

/// Render the canvas grid
void render_grid(Game& game, GLuint shader)
{
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    auto [vertices, indices] = gen_quad_geometry(glm::vec2(1.f), glm::vec2(0.f), glm::vec2(1.0f));
    GLObject glo = create_gl_object(vertices.data(), vertices.size(), indices.data(), indices.size());
    glBindVertexArray(glo.vao);
    for (float i = 0; i < game.camera->canvas_size.x; i++) {
        for (float j = 0; j < game.camera->canvas_size.y; j++) {
            Transform transform;
            transform.position = glm::vec3(0.5f + i, 0.5f + j, 0.0f);
            transform.scale = glm::vec2(0.5);
            draw_object(shader, *game.black_texture, glo, transform.matrix(), std::nullopt, std::nullopt);
        }
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

/// Render AABBs for all objects that have it
void render_aabbs(Game& game, GLuint shader)
{
    glLineWidth(1.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    auto [vertices, indices] = gen_quad_geometry(glm::vec2(1.f), glm::vec2(0.f), glm::vec2(1.0f));
    GLObject glo = create_gl_object(vertices.data(), vertices.size(), indices.data(), indices.size(), GL_STREAM_DRAW);
    glBindVertexArray(glo.vao);
    for (auto* object_list : game.scene->objects.all_lists()) {
        for (auto obj = object_list->rbegin(); obj != object_list->rend(); obj++) {
            if (obj->aabb) {
                Aabb& aabb = *obj->aabb;
                auto vertices = std::vector<Vertex>{
                    { .pos = { aabb.min.x, aabb.min.y }, .texcoord = { 1.0f, 0.0f } },
                    { .pos = { aabb.min.x, aabb.max.y }, .texcoord = { 1.0f, 1.0f } },
                    { .pos = { aabb.max.x, aabb.min.y }, .texcoord = { 0.0f, 1.0f } },
                    { .pos = { aabb.max.x, aabb.max.y }, .texcoord = { 0.0f, 0.0f } },
                };
                //glBindVertexArray(bbox_glo.vao);
                glBindBuffer(GL_ARRAY_BUFFER, glo.vbo);
                glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(Vertex), vertices.data());
                draw_object(shader, *game.white_texture, glo, obj->transform.matrix(), std::nullopt, std::nullopt);
            }
        }
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
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
                    draw_object(shader, *game.black_texture, *obj->glo, transform.matrix(), std::nullopt, std::nullopt);
                }
            }
        }
    }

    for (auto* object_list : game.scene->objects.all_lists()) {
        for (auto obj = object_list->rbegin(); obj != object_list->rend(); obj++) {
            glBindVertexArray(obj->glo->vao);
            Transform transform = obj->transform;
            draw_object(shader, *game.black_texture, *obj->glo, transform.matrix(), std::nullopt, std::nullopt);
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
                    draw_object(shader, *game.black_texture, glo, transform.matrix(), std::nullopt, std::nullopt);
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
                    draw_object(shader, *obj->texture, *obj->glo, obj->transform.matrix(), obj->texture_offset, sprite, color);
                }
            }
        }
    }

    for (auto* object_list : game.scene->objects.all_lists()) {
        for (auto obj = object_list->begin(); obj != object_list->end(); obj++) {
            if (obj->glo && obj->texture) {
                auto sprite = obj->sprite_animation ? std::make_optional<SpriteFrame>(obj->sprite_animation->curr_frame()) : std::nullopt;
                draw_object(shader, *obj->texture, *obj->glo, obj->transform.matrix(), obj->texture_offset, sprite);
            }
        }
    }

    if (game.debug_aabb)
        render_aabbs(game, shader);

    if (game.debug_grid)
        render_grid(game, shader);

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
        game_update(game, dt);
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
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_UP) {
            int i = game.scene->player_idx.x + 1, j = game.scene->player_idx.y, k = game.scene->player_idx.z;
            game.scene->platform[i-1][j][k].sprite_animation->curr_frame_idx = 3;
            if (i < (int)game.map->size.x ) {
                if (!game.scene->platform[i][j][k].glo && !game.scene->platform[i][j][k+1].glo) {
                    game.scene->player_idx.x = i;
                    auto& obj = game.scene->platform[i][j][k] = std::move(game.scene->platform[i-1][j][k]);
                    obj.transform.position.x = i * 0.5f + j * 0.5f - (game.map->tilemap.size() / 2.f) + 0.5f;
                    obj.transform.position.y = i * 0.25f - j * 0.25f + k * 0.5f + 0.8f;
                }
            }
        }
        else if (key == GLFW_KEY_DOWN) {
            int i = game.scene->player_idx.x - 1, j = game.scene->player_idx.y, k = game.scene->player_idx.z;
            game.scene->platform[i+1][j][k].sprite_animation->curr_frame_idx = 1;
            if (i >= 0) {
                if (!game.scene->platform[i][j][k].glo && !game.scene->platform[i][j][k+1].glo) {
                    game.scene->player_idx.x = i;
                    auto& obj = game.scene->platform[i][j][k] = std::move(game.scene->platform[i+1][j][k]);
                    obj.transform.position.x = i * 0.5f + j * 0.5f - (game.map->tilemap.size() / 2.f) + 0.5f;
                    obj.transform.position.y = i * 0.25f - j * 0.25f + k * 0.5f + 0.8f;
                }
            }
        }
        else if (key == GLFW_KEY_RIGHT) {
            int i = game.scene->player_idx.x, j = game.scene->player_idx.y + 1, k = game.scene->player_idx.z;
            game.scene->platform[i][j-1][k].sprite_animation->curr_frame_idx = 0;
            if (j < (int)game.map->size.y) {
                if (!game.scene->platform[i][j][k].glo && !game.scene->platform[i][j][k+1].glo) {
                    game.scene->player_idx.y = j;
                    auto& obj = game.scene->platform[i][j][k] = std::move(game.scene->platform[i][j-1][k]);
                    obj.transform.position.x = i * 0.5f + j * 0.5f - (game.map->tilemap.size() / 2.f) + 0.5f;
                    obj.transform.position.y = i * 0.25f - j * 0.25f + k * 0.5f + 0.8f;
                }
            }
        }
        else if (key == GLFW_KEY_LEFT) {
            int i = game.scene->player_idx.x, j = game.scene->player_idx.y - 1, k = game.scene->player_idx.z;
            game.scene->platform[i][j+1][k].sprite_animation->curr_frame_idx = 2;
            if (j >= 0) {
                if (!game.scene->platform[i][j][k].glo && !game.scene->platform[i][j][k+1].glo) {
                    game.scene->player_idx.y = j;
                    auto& obj = game.scene->platform[i][j][k] = std::move(game.scene->platform[i][j+1][k]);
                    obj.transform.position.x = i * 0.5f + j * 0.5f - (game.map->tilemap.size() / 2.f) + 0.5f;
                    obj.transform.position.y = i * 0.25f - j * 0.25f + k * 0.5f + 0.8f;
                }
            }
        }
    }
}

void key_space_handler(struct Game& game, int key, int action, int mods)
{
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        game.scene->player().motion.velocity.y = 10.f;
        game.scene->player().entity_state = EntityState::JUMPING;
        game.scene->player().sprite_animation->freeze = true;
        game.scene->player().sprite_animation->curr_frame_idx = 2;
    }
    else if (action == GLFW_RELEASE) {
        game.scene->player().motion.velocity.y = 0.0f;
        game.scene->player().sprite_animation->freeze = true;
        game.scene->player().sprite_animation->curr_frame_idx = 0;
        if (game.scene->player().entity_state == EntityState::JUMPING)
            game.scene->player().entity_state = EntityState::IDLE;
    }
}

void key_f5_handler(struct Game& game, int key, int action, int mods)
{
  if (action == GLFW_PRESS)
    game.debug_triangles = !game.debug_triangles;
}

void key_f6_handler(struct Game& game, int key, int action, int mods)
{
  if (action == GLFW_PRESS)
    game.debug_grid = !game.debug_grid;
}

void key_f7_handler(struct Game& game, int key, int action, int mods)
{
  if (action == GLFW_PRESS)
    game.debug_aabb = !game.debug_aabb;
}

/// Handle Key input event
void key_event_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(game->window.glfw, 1);
    }
    else if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT || key == GLFW_KEY_UP || key == GLFW_KEY_DOWN) {
        key_arrows_handler(*game, key, action, mods);
    }
    else if (key == GLFW_KEY_SPACE) {
        key_space_handler(*game, key, action, mods);
    }
    else if (key == GLFW_KEY_F5) {
        key_f5_handler(*game, key, action, mods);
    }
    else if (key == GLFW_KEY_F6) {
        key_f6_handler(*game, key, action, mods);
    }
    else if (key == GLFW_KEY_F7) {
        key_f7_handler(*game, key, action, mods);
    }

    game->key_states.value()[key] = (action == GLFW_PRESS);
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);

/// Handle Mouse click events
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    auto game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;

    if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (game->scene->highlight_idx) {
            glm::uvec3 s = *game->scene->highlight_idx;
            glm::uvec3 v = s; v.z += 1;
            auto& block_above = game->map->tilemap[v.x][v.y][v.z];
            if (block_above == BlockIdx::AIR) {
                block_above = BlockIdx::STONE;
                auto& obj = game->scene->platform[v.x][v.y][v.z];
                auto [vertices, indices] = gen_quad_geometry(glm::vec2(1.f), blocks_offset.at(block_above) / blocks_tileset_size, blocks_tile_size / blocks_tileset_size);
                obj.glo = std::make_shared<GLObject>(create_gl_object(vertices.data(), vertices.size(), indices.data(), indices.size()));
                obj.texture = game->scene->platform[s.x][s.y][s.z].texture;
                obj.transform.position.x = v.x * 0.5f + v.y * 0.5f - (game->map->tilemap.size() / 2.f);
                obj.transform.position.y = v.x * 0.25f - v.y * 0.25f + v.z * 0.5f;
                obj.transform.scale = glm::vec2(1.f);
            }
        }

    }

    if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
        if (game->scene->highlight_idx) {
            glm::uvec3 v = *game->scene->highlight_idx;
            if (v.z != 0) {
                game->map->tilemap[v.x][v.y][v.z] = BlockIdx::AIR;
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

//float area(glm::vec2 a, glm::vec2 b, glm::vec2 c)
//{
    //return std::abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2.f;
//}

//// input in canvas position
//bool collides(glm::vec2 cursor_pos, glm::vec2 target_pos)
//{
    //float h[>height_normal<] = tile_surface_height / blocks_tile_size.y;
    //glm::vec2 A = {target_pos.x + 0.0f, target_pos.y + (1.f - h) + 0.5f * h};
    //glm::vec2 B = {target_pos.x + 0.5f, target_pos.y + (1.f - h) + 0.0f * h};
    //glm::vec2 C = {target_pos.x + 1.0f, target_pos.y + (1.f - h) + 0.5f * h};
    //glm::vec2 D = {target_pos.x + 0.5f, target_pos.y + (1.f - h) + 1.0f * h};
    //auto p = cursor_pos;
    //return area(A, B, D) == area(A, p, B) + area(p, B, D) + area(A, p, D);
//}

/// Handle cursor movement events
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos)
{
    auto game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;
    game->cursor = {xpos, ypos};

    ypos = game->window.size.y - ypos;
    glm::vec2 cursor_pos{xpos, ypos};
    glm::vec2 normal_pos = cursor_pos / glm::vec2(game->window.size);
    glm::vec2 canvas_pos = normal_pos * glm::vec2(game->camera->canvas_size) - glm::vec2(game->camera->canvas_size) / 2.f;
    canvas_pos *= glm::vec2(game->zoom);
    canvas_pos.y -= 1.25f - (tile_surface_height / blocks_tile_size.y);

    // cartesian to isometric
    float i = canvas_pos.x + 2.f * canvas_pos.y + game->map->size.x / 2.f;
    float j = i - 4.f * canvas_pos.y;
    float k = 0;

    glm::ivec3 mapsize = game->map->size;
    if (i >= 0 && i < mapsize.x && j >= 0 && j < mapsize.y && k >= 0 && k < mapsize.z) {
        //bool ret = collides(canvas_pos, game->scene->platform[i][j][k].transform.position);
        //printf("collides %d with i %f j %f k %f\n", ret, i, j, k);

        // check other k layers
        for (int n = mapsize.z-1; n > 0; n--) {
            glm::vec3 p = {i - n, j + n, n};
            if (p.x < 0 || p.y >= mapsize.y || p.z >= mapsize.z)
                continue;
            auto& block = game->map->tilemap[p.x][p.y][p.z];
            if (block != BlockIdx::AIR) {
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


    // check for k level:
    // i -= 1, j
    // i =, j += 1
    // i -= 1, j +=1
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
