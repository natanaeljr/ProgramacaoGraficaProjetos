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
/// Constants

constexpr int WIDTH = 1280, HEIGHT = 720; // window size

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Shader

/// Compile and link shader program
int load_shader_program()
{
    const char* vertex_shader = R"(
#version 410
layout ( location = 0 ) in vec3 vPosition;
layout ( location = 1 ) in vec2 vTexCoord;
uniform mat4 projection;
uniform mat4 model;
out vec2 texcoord;
void main() {
    gl_Position = projection * model * vec4 ( vPosition, 1.0);
    texcoord = vTexCoord;
}
)";

    const char* fragment_shader = R"(
#version 410
in vec2 texcoord;
uniform sampler2D texture0;
out vec4 frag_color;
void main(){
    frag_color = texture(texture0, texcoord);
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
    glm::vec3 pos;
    glm::vec2 texcoord;
};

/// Represents data of an object in GPU memory
struct GLObject {
    GLuint vao;
    GLuint vbo;
    size_t count;
};

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

/// Default Vertices of a QUAD object
static constexpr Vertex kQuadVertices[] = {
    {.pos = { -1.0f, -1.0f, +0.0f }, .texcoord = { 0.0f, 0.0f }},
    {.pos = { -1.0f, +1.0f, +0.0f }, .texcoord = { 0.0f, 1.0f }},
    {.pos = { +1.0f, -1.0f, +0.0f }, .texcoord = { 1.0f, 0.0f }},
    {.pos = { +1.0f, -1.0f, +0.0f }, .texcoord = { 1.0f, 0.0f }},
    {.pos = { -1.0f, +1.0f, +0.0f }, .texcoord = { 0.0f, 1.0f }},
    {.pos = { +1.0f, +1.0f, +0.0f }, .texcoord = { 1.0f, 1.0f }},
};

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Textures

/// Read file and upload RGB/RBGA texture to GPU memory
auto load_rgba_texture(const std::string& inpath) -> std::optional<GLuint>
{
    const std::string filepath = ASSETS_PATH + "/"s + inpath;
    int width, height, channels;
    unsigned char* data = stbi_load(filepath.data(), &width, &height, &channels, 0);
    if (!data) {
        fprintf(stderr, "Failed to load texture path (%s)\n", filepath.data());
        return std::nullopt;
    }
    GLenum type = (channels == 4) ? GL_RGBA : GL_RGB;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
    glm::vec3 position {0.0f};
    glm::vec3 scale    {1.0f};
    glm::quat rotation {1.0f, glm::vec3(0.0f)};

    glm::mat4 matrix() {
        glm::mat4 translation_mat = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 rotation_mat = glm::toMat4(rotation);
        glm::mat4 scale_mat = glm::scale(glm::mat4(1.0f), scale);
        return translation_mat * rotation_mat * scale_mat;
    }
};

/// Game Object represents an Entity's data
struct GameObject {
    GLObject glo;
    Transform transform;
};

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Game

/// All Game data
struct Game {
    GLFWwindow* window;
    glm::uvec2 winsize;
    int shader_program;
    glm::mat4 projection;
    GameObject quad;
    GLuint bg_texture0;
    GLuint bg_texture1;
    GLuint bg_texture2;
};

/// Initialize Game
int game_init(Game& game, GLFWwindow* window)
{
    game.window = window;
    game.winsize = glm::uvec2(WIDTH, HEIGHT);
    game.shader_program = load_shader_program();
    game.projection = glm::mat4(1.0f);
    game.quad.glo = create_gl_object(kQuadVertices, sizeof(kQuadVertices) / sizeof(Vertex)),
    game.quad.transform = Transform{};
    game.quad.transform.scale.y = -1.0f; // flip image
    game.bg_texture0 = *load_rgba_texture("super-mario-assets/bg-mountain-snow.png");
    game.bg_texture1 = *load_rgba_texture("super-mario-assets/bg-mountain-green.png");
    game.bg_texture2 = *load_rgba_texture("super-mario-assets/bg-clouds.png");

    return 0;
}

/// Prepare to render
void begin_render()
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0xF8 / 255.0f, 0xE0 / 255.0f, 0xB0 / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

/// Render Game scene
void game_render(Game& game)
{
    begin_render();
    glUseProgram(game.shader_program);
    glUniformMatrix4fv(glGetUniformLocation(game.shader_program, "projection"), 1, GL_FALSE, glm::value_ptr(game.projection));

    glm::mat4 model = game.quad.transform.matrix();
    glUniformMatrix4fv(glGetUniformLocation(game.shader_program, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glBindVertexArray(game.quad.glo.vao);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, game.bg_texture0);
    glUniform1i(glGetUniformLocation(game.shader_program, "texture0"), 0);
    glDrawArrays(GL_TRIANGLES, 0, game.quad.glo.count);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, game.bg_texture1);
    glUniform1i(glGetUniformLocation(game.shader_program, "texture0"), 0);
    glDrawArrays(GL_TRIANGLES, 0, game.quad.glo.count);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, game.bg_texture2);
    glUniform1i(glGetUniformLocation(game.shader_program, "texture0"), 0);
    glDrawArrays(GL_TRIANGLES, 0, game.quad.glo.count);
}

/// Game Loop, only returns when game finishes
int game_loop(GLFWwindow* window)
{
    Game game;
    int ret = game_init(game, window);
    if (ret) return ret;
    glfwSetWindowUserPointer(window, &game);
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
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
