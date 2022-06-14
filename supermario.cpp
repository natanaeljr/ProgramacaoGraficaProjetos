#include <cstdio>
#include <glm/ext/vector_float2.hpp>
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

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/quaternion_transform.hpp>

#include "stb_image.h"

using namespace std::string_literals;

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Settings

constexpr size_t WIDTH = 900, HEIGHT = 600;

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
    gl_Position = projection * model * vec4 (vPosition, 0.0, 1.0f);
    texcoord = vTexCoord;
}
)";

    const char* fragment_shader = R"(
#version 410
in vec2 texcoord;
uniform sampler2D texture0;
uniform vec2 texoffset;
out vec4 frag_color;
void main(){
    frag_color = texture(texture0, texcoord + texoffset);
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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)offsetof(Vertex, pos));
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
        Vertex{.pos = { -v.x, -v.y }, .texcoord = { to.x + 0.0f, to.y + 0.0f }},
        Vertex{.pos = { -v.x, +v.y }, .texcoord = { to.x + 0.0f, to.y + ts.y }},
        Vertex{.pos = { +v.x, -v.y }, .texcoord = { to.x + ts.x, to.y + 0.0f }},
        Vertex{.pos = { +v.x, +v.y }, .texcoord = { to.x + ts.x, to.y + ts.y }},
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
    float duration;    // duration in seconds, negative is infinite
    size_t ebo_offset; // offset to the first index of this frame in the EBO
    size_t ebo_count;  // number of elements to render since first index
};

/// Control data required for a single Sprite Animation object
struct SpriteAnimation {
    float last_transit_dt; // deltatime between last transition and now
    size_t curr_frame_idx; // current frame index
    std::vector<SpriteFrame> frames;
    size_t curr_cycle_count; // number of cycles executed
    size_t max_cycles; // max number of cycles to execute before ending sprite animation, zero for endless

    /// Transition frames
    void update_frame(float dt) {
        last_transit_dt += dt;
        SpriteFrame& curr_frame = frames[curr_frame_idx];
        if (last_transit_dt >= curr_frame.duration) {
            last_transit_dt -= curr_frame.duration;
            if (++curr_frame_idx == frames.size()) {
                curr_frame_idx = 0;
                curr_cycle_count++;
            }
        }
    }

    /// Get current sprite frame
    SpriteFrame& curr_frame() { return frames[curr_frame_idx]; }

    /// Check if animation already ran to maximum number of cycles
    bool expired() { return max_cycles > 0 && curr_cycle_count >= max_cycles; }
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
    glm::vec2 canvas;
    glm::mat4 projection;
    glm::mat4 view;

    /// Create Orthographic Camera
    static Camera create(float aspect_ratio) {
        auto canvas = glm::vec2(30.f, 30.f / aspect_ratio);
        return {
            .canvas = canvas,
            .projection = glm::ortho(0.f, canvas.x, 0.f, canvas.y, +1.0f, -1.0f),
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
};

/// Lists of all Game Objects in a Scene, divised in layers, in order of render
struct ObjectLists {
    std::vector<GameObject> background;
    std::vector<GameObject> platform;
    std::vector<GameObject> entity;
    /// Get all layers of objects
    auto all_lists() { return std::array{ &background, &platform, &entity }; }
};

/// Generic Scene structure
struct Scene {
    ObjectLists objects;
    glm::vec4 bg_color;
    GameObject& player() { return objects.entity.front(); }
};

/// All Game data
struct Game {
    Window window;
    Viewport viewport;
    GLuint shader_program;
    GLObjectRef canvas_quad_glo;
    glm::vec2 map_size;
    GLTextureRef white_texture;
    std::optional<Camera> camera;
    std::optional<Scene> scene;
    std::optional<KeyStateMap> key_states;
    bool debug_grid;
    bool debug_aabb;
};

/// Load Main Scene
Scene load_scene(const Game& game)
{
    Scene scene;
    scene.bg_color = glm::vec4(glm::vec3(0xF8, 0xE0, 0xB0) / glm::vec3(255.f), 1.0f);

    // Backgrounds ============================================================
    auto& backgrounds = scene.objects.background;
    backgrounds.push_back({});
    auto& snow_mountains = backgrounds.back();
    snow_mountains.glo = game.canvas_quad_glo;
    snow_mountains.texture = std::make_shared<GLTexture>(*load_rgba_texture("bg-mountain-snow.png"));
    snow_mountains.transform.position = game.camera->canvas / glm::vec2(2.f);
    snow_mountains.transform.scale = game.camera->canvas / glm::vec2(2.f);
    snow_mountains.texture_slide = TextureSlide{
        .velocity = glm::vec2(0.01f, 0.f),
        .acceleration = glm::vec2(0.f),
    };
    snow_mountains.texture_offset = TextureOffset{};

    backgrounds.push_back({});
    auto& green_mountains = backgrounds.back();
    green_mountains.glo = game.canvas_quad_glo;
    green_mountains.texture = std::make_shared<GLTexture>(*load_rgba_texture("bg-mountain-green.png"));
    green_mountains.transform.position = game.camera->canvas / glm::vec2(2.f);
    green_mountains.transform.scale = game.camera->canvas / glm::vec2(2.f);
    green_mountains.texture_slide = TextureSlide{
        .velocity = glm::vec2(0.03f, 0.f),
        .acceleration = glm::vec2(0.f),
    };
    green_mountains.texture_offset = TextureOffset{};

    backgrounds.push_back({});
    auto& clouds = backgrounds.back();
    clouds.glo = game.canvas_quad_glo;
    clouds.texture = std::make_shared<GLTexture>(*load_rgba_texture("bg-clouds.png"));
    clouds.texture_slide = TextureSlide{
        .velocity = glm::vec2(0.07f, 0.f),
        .acceleration = glm::vec2(0.f),
    };
    clouds.texture_offset = TextureOffset{};
    clouds.transform.position = game.camera->canvas / glm::vec2(2.f);
    clouds.transform.scale = game.camera->canvas / glm::vec2(2.f);

    // Platform Blocks ========================================================
    GLTextureRef tileset_tex = std::make_shared<GLTexture>(*load_rgba_texture("tiles-2.png"));
    constexpr glm::vec2 tileset_size = glm::vec2(339.f, 339.f);
    constexpr glm::vec2 tile_normal_size = glm::vec2(16.f / 339.f);
    constexpr glm::vec2 tile_offset_green_middle_top = glm::vec2(34.f, 221.f);
    constexpr glm::vec2 tile_offset_green_middle_bottom = glm::vec2(34.f, 204.f);
    constexpr glm::vec2 tile_offset_green_left_top = glm::vec2(0.f, 221.f);
    constexpr glm::vec2 tile_offset_green_right_top = glm::vec2(68.f, 221.f);
    constexpr glm::vec2 tile_offset_green_left_bottom = glm::vec2(0.f, 204.f);
    constexpr glm::vec2 tile_offset_green_right_bottom = glm::vec2(68.f, 204.f);

    auto& platform = scene.objects.platform;

    // Ground =================================================================
    for (float i = 0; i < game.camera->canvas.x * 3; i++) {
        platform.push_back({});
        auto& tile_top = platform.back();
        auto [tile_top_vertices, tile_top_indices] = gen_quad_geometry(glm::vec2(1.f), tile_offset_green_middle_top / tileset_size, tile_normal_size);
        tile_top.glo = std::make_shared<GLObject>(create_gl_object(tile_top_vertices.data(), tile_top_vertices.size(), tile_top_indices.data(), tile_top_indices.size()));
        tile_top.texture = tileset_tex;
        tile_top.transform.scale = glm::vec2(0.5f);
        tile_top.transform.position = glm::vec2(tile_top.transform.scale.x + i, tile_top.transform.scale.y + 1.f);

        platform.push_back({});
        auto& tile_bottom = platform.back();
        auto [tile_bottom_vertices, tile_bottom_indices] = gen_quad_geometry(glm::vec2(1.f), tile_offset_green_middle_bottom / tileset_size, tile_normal_size);
        tile_bottom.glo = std::make_shared<GLObject>(create_gl_object(tile_bottom_vertices.data(), tile_bottom_vertices.size(), tile_bottom_indices.data(), tile_bottom_indices.size()));
        tile_bottom.texture = tileset_tex;
        tile_bottom.transform.scale = glm::vec2(0.5f);
        tile_bottom.transform.position = glm::vec2(tile_bottom.transform.scale.x + i, tile_bottom.transform.scale.y);
    }

    // Platf1 =================================================================
    constexpr glm::vec2 platf1_offset = glm::vec2(20.f, 2.f);
    {
        platform.push_back({});
        auto& tile_top_left = platform.back();
        auto [tile_top_left_vertices, tile_top_left_indices] = gen_quad_geometry(glm::vec2(1.f), tile_offset_green_left_top / tileset_size, tile_normal_size);
        tile_top_left.glo = std::make_shared<GLObject>(create_gl_object(tile_top_left_vertices.data(), tile_top_left_vertices.size(), tile_top_left_indices.data(), tile_top_left_indices.size()));
        tile_top_left.texture = tileset_tex;
        tile_top_left.transform.scale = glm::vec2(0.5f);
        tile_top_left.transform.position = glm::vec2(platf1_offset.x + tile_top_left.transform.scale.x, platf1_offset.y + tile_top_left.transform.scale.y + 2.f);

        platform.push_back({});
        auto& tile_middle_left = platform.back();
        auto [tile_middle_left_vertices, tile_middle_left_indices] = gen_quad_geometry(glm::vec2(1.f), tile_offset_green_left_bottom / tileset_size, tile_normal_size);
        tile_middle_left.glo = std::make_shared<GLObject>(create_gl_object(tile_middle_left_vertices.data(), tile_middle_left_vertices.size(), tile_middle_left_indices.data(), tile_middle_left_indices.size()));
        tile_middle_left.texture = tileset_tex;
        tile_middle_left.transform.scale = glm::vec2(0.5f);
        tile_middle_left.transform.position = glm::vec2(platf1_offset.x + tile_middle_left.transform.scale.x, platf1_offset.y + tile_middle_left.transform.scale.y + 1.f);

        platform.push_back({});
        auto& tile_bottom_left = platform.back();
        auto [tile_bottom_left_vertices, tile_bottom_left_indices] = gen_quad_geometry(glm::vec2(1.f), tile_offset_green_left_bottom / tileset_size, tile_normal_size);
        tile_bottom_left.glo = std::make_shared<GLObject>(create_gl_object(tile_bottom_left_vertices.data(), tile_bottom_left_vertices.size(), tile_bottom_left_indices.data(), tile_bottom_left_indices.size()));
        tile_bottom_left.texture = tileset_tex;
        tile_bottom_left.transform.scale = glm::vec2(0.5f);
        tile_bottom_left.transform.position = glm::vec2(platf1_offset.x + tile_bottom_left.transform.scale.x, platf1_offset.y + tile_bottom_left.transform.scale.y + 0.f);
    }

    for (float i = 1; i < 6; i++) {
        {
            platform.push_back({});
            auto& tile_top = platform.back();
            auto [tile_vertices, tile_indices] = gen_quad_geometry(glm::vec2(1.f), tile_offset_green_middle_top / tileset_size, tile_normal_size);
            tile_top.glo = std::make_shared<GLObject>(create_gl_object(tile_vertices.data(), tile_vertices.size(), tile_indices.data(), tile_indices.size()));
            tile_top.texture = tileset_tex;
            tile_top.transform.scale = glm::vec2(0.5f);
            tile_top.transform.position = glm::vec2(platf1_offset.x + tile_top.transform.scale.x + i, platf1_offset.y + tile_top.transform.scale.y + 2.f);
        }

        {
            platform.push_back({});
            auto& tile_middle = platform.back();
            auto [tile_vertices, tile_indices] = gen_quad_geometry(glm::vec2(1.f), tile_offset_green_middle_bottom / tileset_size, tile_normal_size);
            tile_middle.glo = std::make_shared<GLObject>(create_gl_object(tile_vertices.data(), tile_vertices.size(), tile_indices.data(), tile_indices.size()));
            tile_middle.texture = tileset_tex;
            tile_middle.transform.scale = glm::vec2(0.5f);
            tile_middle.transform.position = glm::vec2(platf1_offset.x + tile_middle.transform.scale.x + i, platf1_offset.y + tile_middle.transform.scale.y + 1.f);
        }

        {
            platform.push_back({});
            auto& tile_bottom = platform.back();
            auto [tile_vertices, tile_indices] = gen_quad_geometry(glm::vec2(1.f), tile_offset_green_middle_bottom / tileset_size, tile_normal_size);
            tile_bottom.glo = std::make_shared<GLObject>(create_gl_object(tile_vertices.data(), tile_vertices.size(), tile_indices.data(), tile_indices.size()));
            tile_bottom.texture = tileset_tex;
            tile_bottom.transform.scale = glm::vec2(0.5f);
            tile_bottom.transform.position = glm::vec2(platf1_offset.x + tile_bottom.transform.scale.x + i, platf1_offset.y + tile_bottom.transform.scale.y + 0.f);
        }
    }

    {
        {
            platform.push_back({});
            auto& tile_top_right = platform.back();
            auto [tile_vertices, tile_indices] = gen_quad_geometry(glm::vec2(1.f), tile_offset_green_right_top / tileset_size, tile_normal_size);
            tile_top_right.glo = std::make_shared<GLObject>(create_gl_object(tile_vertices.data(), tile_vertices.size(), tile_indices.data(), tile_indices.size()));
            tile_top_right.texture = tileset_tex;
            tile_top_right.transform.scale = glm::vec2(0.5f);
            tile_top_right.transform.position = glm::vec2(platf1_offset.x + tile_top_right.transform.scale.x + 6, platf1_offset.y + tile_top_right.transform.scale.y + 2.f);
        }

        {
            platform.push_back({});
            auto& tile_middle_right = platform.back();
            auto [tile_vertices, tile_indices] = gen_quad_geometry(glm::vec2(1.f), tile_offset_green_right_bottom / tileset_size, tile_normal_size);
            tile_middle_right.glo = std::make_shared<GLObject>(create_gl_object(tile_vertices.data(), tile_vertices.size(), tile_indices.data(), tile_indices.size()));
            tile_middle_right.texture = tileset_tex;
            tile_middle_right.transform.scale = glm::vec2(0.5f);
            tile_middle_right.transform.position = glm::vec2(platf1_offset.x + tile_middle_right.transform.scale.x + 6, platf1_offset.y + tile_middle_right.transform.scale.y + 1.f);
        }

        {
            platform.push_back({});
            auto& tile_bottom_right = platform.back();
            auto [tile_vertices, tile_indices] = gen_quad_geometry(glm::vec2(1.f), tile_offset_green_right_bottom / tileset_size, tile_normal_size);
            tile_bottom_right.glo = std::make_shared<GLObject>(create_gl_object(tile_vertices.data(), tile_vertices.size(), tile_indices.data(), tile_indices.size()));
            tile_bottom_right.texture = tileset_tex;
            tile_bottom_right.transform.scale = glm::vec2(0.5f);
            tile_bottom_right.transform.position = glm::vec2(platf1_offset.x + tile_bottom_right.transform.scale.x + 6, platf1_offset.y + tile_bottom_right.transform.scale.y + 0.f);
        }
    }

    // Platform AABBs
    {   // groud
        platform.push_back({});
        auto& obj = platform.back();
        auto [vertices, indices] = gen_quad_geometry(glm::vec2(1.f), glm::vec2(0.f), glm::vec2(1.0f));
        obj.glo = std::make_shared<GLObject>(create_gl_object(vertices.data(), vertices.size(), indices.data(), indices.size()));
        obj.aabb = Aabb{ .min= {-1.f, -1.f}, .max = {+1.f, +0.99f} };
        obj.transform.position = glm::vec2(game.map_size.x / 2.f, 1.f);
        obj.transform.scale = glm::vec2(game.map_size.x / 2.f, 1.f);
    }
    {   // platf1
        platform.push_back({});
        auto& obj = platform.back();
        auto [vertices, indices] = gen_quad_geometry(glm::vec2(1.f), glm::vec2(0.f), glm::vec2(1.0f));
        obj.glo = std::make_shared<GLObject>(create_gl_object(vertices.data(), vertices.size(), indices.data(), indices.size()));
        obj.aabb = Aabb{ .min= {-0.98f, -1.f}, .max = {+0.98f, +0.99f} };
        obj.transform.position = glm::vec2(23.5f, 4.75f);
        obj.transform.scale = glm::vec2(3.5f, 0.25f);
    }


    // Entities ===============================================================
    auto& entities = scene.objects.entity;
    entities.push_back({});
    auto& mario = entities.back();
    constexpr glm::vec2 mario_spritesheet_size = glm::vec2(201.f, 120.f);
    constexpr glm::vec2 mario_frame_size = glm::vec2(17.f, 29.f);
    constexpr glm::vec2 mario_walk_offset = glm::vec2(0.f, 91.f);
    auto [mario_vertices, mario_indices] = gen_sprite_quads(2, glm::vec2(mario_frame_size.x/mario_frame_size.y, 1.f), mario_walk_offset / mario_spritesheet_size, (mario_frame_size * glm::vec2(2.f, 1.f)) / mario_spritesheet_size);
    mario.glo = std::make_shared<GLObject>(create_gl_object(mario_vertices.data(), mario_vertices.size(), mario_indices.data(), mario_indices.size()));
    mario.texture = std::make_shared<GLTexture>(*load_rgba_texture("mario-3.png"));
    mario.transform.scale = glm::vec2(1.2f);
    mario.transform.position = glm::vec2(10.f, 2.f + mario.transform.scale.y);
    mario.sprite_animation = SpriteAnimation{
      .last_transit_dt = 0,
      .curr_frame_idx = 0,
      .frames = std::vector<SpriteFrame>{
        { .duration = 0.10, .ebo_offset = 00, .ebo_count = 6 },
        { .duration = 0.10, .ebo_offset = 12, .ebo_count = 6 },
      },
      .curr_cycle_count = 0,
      .max_cycles = 0,
    };
    mario.aabb = Aabb{ .min= {-0.45f, -0.99f}, .max = {+0.45f, +0.8f} };
    mario.gravity = Gravity{};

    return scene;
}

/// Initialize Game
int game_init(Game& game, GLFWwindow* window)
{
    game.window.glfw = window;
    game.window.size = glm::uvec2(WIDTH, HEIGHT);
    game.viewport.size = glm::uvec2(WIDTH, HEIGHT);
    game.viewport.offset = glm::uvec2(0);
    game.shader_program = load_shader_program();
    auto [quad_vertices, quad_indices] = gen_quad_geometry(glm::vec2(1.f), glm::vec2(0.f), glm::vec2((float)WIDTH / (float)HEIGHT, 1.0f));
    game.canvas_quad_glo = std::make_shared<GLObject>(create_gl_object(quad_vertices.data(), quad_vertices.size(), quad_indices.data(), quad_indices.size()));
    game.map_size = glm::vec2(90.f, 30.f);
    game.white_texture = std::make_shared<GLTexture>(*load_rgba_texture("white.png"));
    game.camera = Camera::create(game.viewport.aspect_ratio());
    game.scene = load_scene(game);
    game.key_states = KeyStateMap(GLFW_KEY_LAST);
    game.debug_grid = false;
    game.debug_aabb = false;

    return 0;
}

/// Game tick update
void game_update(Game& game, float dt)
{
    // Update all objects
    for (auto* object_list : game.scene->objects.all_lists()) {
        for (auto& obj : *object_list) {
            // Gravity system
            if (obj.gravity) {
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

    // Collision system
    {
        auto& entities = game.scene->objects.entity;
        for (auto entt = entities.begin(); entt != entities.end(); entt++) {
            auto& tiles = game.scene->objects.platform;
            for (auto& tile : tiles) {
                Aabb tile_aabb = tile.aabb->transform(tile.transform.matrix());
                Aabb entt_aabb = entt->aabb->transform(entt->transform.matrix());
                if (collision(tile_aabb, entt_aabb)) {
                    float y_top_diff = entt_aabb.max.y - tile_aabb.max.y;
                    float y_bottom_diff = entt_aabb.min.y - tile_aabb.max.y;
                    if (y_top_diff > 0.f && y_bottom_diff) {
                        entt->transform.position.y += -y_bottom_diff;
                        entt->motion.velocity.y = 0.f;
                    }
                }
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
                 const std::optional<TextureOffset> texoffset, const std::optional<SpriteFrame> sprite)
{
    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glm::vec2 texoffset_vec = texoffset ? texoffset->vec : glm::vec2(0.f);
    glUniform2fv(glGetUniformLocation(shader, "texoffset"), 1, glm::value_ptr(texoffset_vec));
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
    for (float i = 0; i < game.camera->canvas.x; i++) {
        for (float j = 0; j < game.camera->canvas.y; j++) {
            Transform transform;
            transform.position = glm::vec2(0.5f + i, 0.5f + j);
            transform.scale = glm::vec2(0.5);
            draw_object(shader, *game.white_texture, glo, transform.matrix(), std::nullopt, std::nullopt);
        }
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

/// Render AABBs for all objects that have it
void render_aabbs(Game& game, GLuint shader)
{
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

/// Render Game scene
void game_render(Game& game)
{
    begin_render(game);
    GLuint shader = game.shader_program;
    glUseProgram(shader);
    set_camera(shader, *game.camera);

    for (auto* object_list : game.scene->objects.all_lists()) {
        for (auto obj = object_list->begin(); obj != object_list->end() && obj->glo && obj->texture; obj++) {
            auto sprite = obj->sprite_animation ? std::make_optional<SpriteFrame>(obj->sprite_animation->curr_frame()) : std::nullopt;
            draw_object(shader, *obj->texture, *obj->glo, obj->transform.matrix(), obj->texture_offset, sprite);
        }
    }

    if (game.debug_aabb)
        render_aabbs(game, shader);

    if (game.debug_grid)
        render_grid(game, shader);
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

void key_left_right_handler(struct Game& game, int key, int action, int mods)
{
    assert(key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT);
    const float direction = (key == GLFW_KEY_LEFT ? -1.f : +1.f);

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        game.scene->player().motion.velocity.x = 8.f * direction;
        game.scene->player().transform.scale.x = 1.2f * direction;
    }
    else if (action == GLFW_RELEASE) {
        const int other_key = (key == GLFW_KEY_LEFT) ? GLFW_KEY_RIGHT : GLFW_KEY_LEFT;
        if (game.key_states.value()[other_key]) {
            // revert to opposite key's movement
            key_left_right_handler(game, other_key, GLFW_REPEAT, mods);
        } else {
            // both arrow keys release, cease movement
            game.scene->player().motion.velocity.x = 0.0f;
            game.scene->player().motion.acceleration.x = 0.0f;
        }
    }
}

void key_space_handler(struct Game& game, int key, int action, int mods)
{
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        game.scene->player().motion.velocity.y = 10.f;
    }
    else if (action == GLFW_RELEASE) {
        game.scene->player().motion.velocity.y = 0.0f;
    }
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
    else if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT) {
        key_left_right_handler(*game, key, action, mods);
    }
    else if (key == GLFW_KEY_SPACE) {
        key_space_handler(*game, key, action, mods);
    }
    else if (key == GLFW_KEY_F6) {
        key_f6_handler(*game, key, action, mods);
    }
    else if (key == GLFW_KEY_F7) {
        key_f7_handler(*game, key, action, mods);
    }

    game->key_states.value()[key] = (action == GLFW_PRESS);
}

/// Handle Mouse click events
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    auto game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;

    if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
    }
}

/// Handle Window framebuffer resize event
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    auto game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;

    glViewport(0, 0, width, height);
    game->camera = Camera::create((float)width / (float)height);

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
    window = glfwCreateWindow(WIDTH, HEIGHT, "Super Mario", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create GLFW Window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_event_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
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
