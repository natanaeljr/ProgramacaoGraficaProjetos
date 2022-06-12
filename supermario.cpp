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

constexpr int WIDTH = 900, HEIGHT = 500; // window size

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Shader

/// Compile and link shader program
int load_shader_program()
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

    int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertex_shader, NULL);
    glCompileShader(vs);

    int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragment_shader, NULL);
    glCompileShader(fs);

    int sp = glCreateProgram();
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
    size_t count;
};

/// GLObject reference type alias
using GLObjectRef = std::shared_ptr<GLObject>;

/// Create an Object in GPU memory
GLObject create_gl_object(const Vertex vertices[], const size_t count)
{
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(Vertex), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)offsetof(Vertex, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)offsetof(Vertex, texcoord));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return {vao, vbo, count};
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Geometry

/// Generate a QUAD with given dimensions and texture offsets
constexpr auto gen_quad_vertices(glm::vec2 v, glm::vec2 to, glm::vec2 ts) -> std::array<Vertex, 6>
{
    std::array<Vertex, 6> vertices = {
        Vertex{.pos = { -v.x, -v.y }, .texcoord = { to.x + 0.0f, to.y + 0.0f }},
        Vertex{.pos = { -v.x, +v.y }, .texcoord = { to.x + 0.0f, to.y + ts.y }},
        Vertex{.pos = { +v.x, -v.y }, .texcoord = { to.x + ts.x, to.y + 0.0f }},
        Vertex{.pos = { +v.x, -v.y }, .texcoord = { to.x + ts.x, to.y + 0.0f }},
        Vertex{.pos = { -v.x, +v.y }, .texcoord = { to.x + 0.0f, to.y + ts.y }},
        Vertex{.pos = { +v.x, +v.y }, .texcoord = { to.x + ts.x, to.y + ts.y }},
    };
    return vertices;
}

/// Default Vertices of a QUAD object
static constexpr auto kQuadVertices = gen_quad_vertices(glm::vec2(1.f), glm::vec2(0.f), glm::vec2(1.f));

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

/// Texture Slide component
struct TextureSlide {
    glm::vec2 velocity     {0.0f};
    glm::vec2 acceleration {0.0f};
};

/// Texture Offset component
struct TextureOffset {
    glm::vec2 vec {0.0f};
};

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
};

/// All Game data
struct Game {
    GLFWwindow* window;
    glm::uvec2 winsize;
    int shader_program;
    glm::mat4 projection;
    GLObjectRef quad_glo;
    std::optional<Scene> scene;
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
    snow_mountains.glo = game.quad_glo;
    snow_mountains.texture = std::make_shared<GLTexture>(*load_rgba_texture("bg-mountain-snow.png"));

    backgrounds.push_back({});
    auto& green_mountains = backgrounds.back();
    green_mountains.glo = game.quad_glo;
    green_mountains.texture = std::make_shared<GLTexture>(*load_rgba_texture("bg-mountain-green.png"));

    backgrounds.push_back({});
    auto& clouds = backgrounds.back();
    clouds.glo = game.quad_glo;
    clouds.texture = std::make_shared<GLTexture>(*load_rgba_texture("bg-clouds.png"));
    clouds.texture_slide = TextureSlide{
        .velocity = glm::vec2(0.04f, 0.f),
        .acceleration = glm::vec2(0.f),
    };
    clouds.texture_offset = TextureOffset{};

    // Platform Blocks ========================================================
    GLTextureRef tile_spritesheet = std::make_shared<GLTexture>(*load_rgba_texture("tiles-2.png"));
    auto& platform = scene.objects.platform;
    platform.push_back({});
    auto& tile = platform.back();
    auto tile_vertices = gen_quad_vertices(glm::vec2(1.f), glm::vec2(34.f, (339.f - 102.f -16.f)) / glm::vec2(339.f), glm::vec2(16.f / 339.f));
    tile.glo = std::make_shared<GLObject>(create_gl_object(tile_vertices.data(), tile_vertices.size()));
    tile.texture = tile_spritesheet;
    tile.transform.scale = glm::vec2(0.05f);
    tile.transform.position = glm::vec2(-1.f + tile.transform.scale.x, -1.f + tile.transform.scale.y);

    return scene;
}

/// Initialize Game
int game_init(Game& game, GLFWwindow* window)
{
    game.window = window;
    game.winsize = glm::uvec2(WIDTH, HEIGHT);
    game.shader_program = load_shader_program();
    game.projection = glm::mat4(1.0f);
    game.quad_glo = std::make_shared<GLObject>(create_gl_object(kQuadVertices.data(), kQuadVertices.size()));
    game.scene = load_scene(game);

    return 0;
}

/// Game tick update
void game_update(Game& game, float dt)
{
    // Update all objects
    for (auto* object_list : game.scene->objects.all_lists()) {
        for (auto& obj : *object_list) {
            // Motion system
            obj.motion.velocity += obj.motion.acceleration * dt;
            obj.transform.position += obj.motion.velocity * dt;
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

/// Render a textured GLObject
void draw_object(const GLuint& shader, const GLTexture& texture, const GLObject& glo,
                 const glm::mat4& model, std::optional<TextureOffset> texoffset)
{
    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glm::vec2 texoffset_vec = texoffset ? texoffset->vec : glm::vec2(0.f);
    glUniform2fv(glGetUniformLocation(shader, "texoffset"), 1, glm::value_ptr(texoffset_vec));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(glo.vao);
    glDrawArrays(GL_TRIANGLES, 0, glo.count);
}

/// Render Game scene
void game_render(Game& game)
{
    begin_render(game);
    int shader = game.shader_program;
    glUseProgram(shader);
    glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, glm::value_ptr(game.projection));

    for (auto* object_list : game.scene->objects.all_lists()) {
        for (auto obj = object_list->begin(); obj != object_list->end() && obj->glo && obj->texture; obj++) {
            draw_object(shader, *obj->texture, *obj->glo, obj->transform.matrix(), obj->texture_offset);
        }
    }
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
        float dt = last_time - now_time;
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

/// Handle Key input event
void key_event_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(game->window, 1);
    }
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
    game->winsize.x = width;
    game->winsize.x = height;
    glViewport(0, 0, width, height);
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
    window = glfwCreateWindow(WIDTH, HEIGHT, "COLOR GAME", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create GLFW Window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_event_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetWindowAspectRatio(window, WIDTH, HEIGHT);
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
