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

constexpr int WIDTH = 800, HEIGHT = 480; // window size

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Shader

/// Compile and link shader program
int build_shader_program()
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
uniform sampler2D leftTex;
uniform sampler2D rightTex;
uniform int formula;
out vec4 frag_color;
void main(){
    vec4 left = texture(leftTex, texcoord);
    vec4 right = texture(rightTex, texcoord);

    // Anaglifo verdadeiro:
    if (formula == 0) {
        float r = right.r * 0.299f + right.g * 0.587f + right.b * 0.114f;
        float g = 0;
        float b = left.r * 0.299f + left.g * 0.587f + left.b * 0.114f;
        frag_color = vec4(r, g, b, 1.0f);
    }

    // Anaglifo cinza:
    else if (formula == 1) {
        float r = right.r * 0.299f + right.g * 0.587f + right.b * 0.114f;
        float g = left.r * 0.299f + left.g * 0.587f + left.b * 0.114f;
        float b = left.r * 0.299f + left.g * 0.587f + left.b * 0.114f;
        frag_color = vec4(r, g, b, 1.0f);
    }

    // Anaglifo color:
    else if (formula == 2) {
        float r = right.r;
        float g = left.g;
        float b = left.b;
        frag_color = vec4(r, g, b, 1.0f);
    }
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
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

/// Engine Object represents an Entity's data
struct EngineObject {
    GLObject glo;
    Transform transform;
};

/// Supported Anaglifo Formulas
enum class AnaglifoFormula {
    VERDADEIRO = 0,
    CINZA = 1,
    COLOR = 2,
};

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Engine

/// All Engine data
struct Engine {
    GLFWwindow* window;
    glm::uvec2 winsize;
    int shader_program;
    glm::mat4 projection;
    EngineObject quad;
    GLuint left_texture;
    GLuint right_texture;
    AnaglifoFormula anaglifo_formula;
};

/// Get texture file paths from user input
auto read_user_texture_input() -> std::tuple<std::string, std::string>
{
    std::string left, right;
    std::cout << "Digite o caminho da imagem esquerda: ";
    std::cin >> left;
    std::cout << "Digite o caminho da imagem direita: ";
    std::cin >> right;
    return {left, right};
}

/// Initialize Engine
int engine_init(Engine& engine, GLFWwindow* window)
{
    engine.window = window;
    engine.winsize = glm::uvec2(WIDTH, HEIGHT);
    engine.shader_program = build_shader_program();
    engine.projection = glm::mat4(1.0f);
    engine.quad.glo = create_gl_object(kQuadVertices, sizeof(kQuadVertices) / sizeof(Vertex)),
    engine.quad.transform = Transform{};
    engine.quad.transform.scale.y = -1.0f; // flip image

    auto [left_path, right_path] = read_user_texture_input();
    engine.left_texture = *load_rgba_texture(left_path);
    engine.right_texture = *load_rgba_texture(right_path);
    engine.anaglifo_formula = AnaglifoFormula::COLOR;

    return 0;
}

/// Render Engine scene
void engine_render(Engine& engine)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(engine.shader_program);
    glUniformMatrix4fv(glGetUniformLocation(engine.shader_program, "projection"), 1, GL_FALSE, glm::value_ptr(engine.projection));
    glUniform1i(glGetUniformLocation(engine.shader_program, "formula"), static_cast<int>(engine.anaglifo_formula));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, engine.left_texture);
    glUniform1i(glGetUniformLocation(engine.shader_program, "leftTex"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, engine.right_texture);
    glUniform1i(glGetUniformLocation(engine.shader_program, "rightTex"), 1);

    glm::mat4 model = engine.quad.transform.matrix();
    glUniformMatrix4fv(glGetUniformLocation(engine.shader_program, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glBindVertexArray(engine.quad.glo.vao);
    glDrawArrays(GL_TRIANGLES, 0, engine.quad.glo.count);
}

/// Engine Loop, only returns when engine finishes
int engine_loop(GLFWwindow* window)
{
    Engine engine;
    int ret = engine_init(engine, window);
    if (ret) return ret;
    glfwSetWindowUserPointer(window, &engine);
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        engine_render(engine);
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
    auto engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
    if (!engine) return;

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(engine->window, 1);
    }
    else if (key == GLFW_KEY_1 && action == GLFW_PRESS)
        engine->anaglifo_formula = AnaglifoFormula::VERDADEIRO;
    else if (key == GLFW_KEY_2 && action == GLFW_PRESS)
        engine->anaglifo_formula = AnaglifoFormula::CINZA;
    else if (key == GLFW_KEY_3 && action == GLFW_PRESS)
        engine->anaglifo_formula = AnaglifoFormula::COLOR;
}

/// Handle Mouse click events
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    auto engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
    if (!engine) return;

    if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
    }
}

/// Handle Window framebuffer resize event
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    auto engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
    if (!engine) return;
    engine->winsize.x = width;
    engine->winsize.x = height;
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

    // Engine Loop ==============================================================
    ret = engine_loop(window);
    if (ret) return ret;

    // Terminate ==============================================================
    glfwTerminate();
    return EXIT_SUCCESS;
}

// vim: tabstop=4 shiftwidth=4
