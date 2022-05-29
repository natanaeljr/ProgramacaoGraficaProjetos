#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <array>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/quaternion_transform.hpp>

/// Constants
constexpr int WIDTH = 800, HEIGHT = 480;
constexpr int COLS = 10, ROWS = 15;

/// Compile and link shader program
int build_shader_program()
{
    const char* vertex_shader = R"(
#version 410
layout ( location = 0 ) in vec3 vPosition;
uniform mat4 projection;
uniform mat4 model;
void main() {
    gl_Position = projection * model * vec4 ( vPosition, 1.0);
}
)";

    const char* fragment_shader = R"(
#version 410
uniform vec3 color;
out vec4 frag_color;
void main(){
  frag_color = vec4(color, 1.0f);
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

/// Vertex data
struct Vertex {
    glm::vec3 pos;
};

/// Represents data of an object upload to GPU
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
    glBindVertexArray(0);
    return {vao, vbo, count};
}

/// Default Vertices of a QUAD object
static constexpr Vertex kQuadVertices[] = {
    {.pos = { -1.0f, -1.0f, +0.0f }},
    {.pos = { -1.0f, +1.0f, +0.0f }},
    {.pos = { +1.0f, -1.0f, +0.0f }},
    {.pos = { +1.0f, -1.0f, +0.0f }},
    {.pos = { -1.0f, +1.0f, +0.0f }},
    {.pos = { +1.0f, +1.0f, +0.0f }},
};

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

/// RGB Unit
struct Rgb {
    union {
        struct {
            float r;
            float g;
            float b;
        };
        glm::vec3 vec;
    };
};

/// Color Palette
struct Palette {
    Rgb matrix[ROWS][COLS];

    static Palette random() {
        Palette palette;
        for (int i = 0; i < ROWS; i++) {
            for (int j = 0; j < COLS; j++) {
                palette.matrix[i][j].r = (std::rand() % 255) / 255.f;
                palette.matrix[i][j].g = (std::rand() % 255) / 255.f;
                palette.matrix[i][j].b = (std::rand() % 255) / 255.f;
            }
        }
        return palette;
    }
};

/// Game & Engine data
struct Game {
    GLFWwindow* window;
    glm::uvec2 winsize;
    int shader_program;
    glm::mat4 projection;
    GameObject quad;
    Palette palette;
};

/// Initialize Game
int game_init(Game& game, GLFWwindow* window)
{
    game.window = window;
    game.winsize = glm::uvec2(WIDTH, HEIGHT);
    game.shader_program = build_shader_program();
    game.projection = glm::mat4(1.0f);
    game.quad.glo = create_gl_object(kQuadVertices, sizeof(kQuadVertices) / sizeof(Vertex)),
    game.quad.transform = Transform{};
    game.palette = Palette::random();
    return 0;
}

/// Render Game scene
void game_render(Game& game, float dt)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(game.shader_program);
    glUniformMatrix4fv(glGetUniformLocation(game.shader_program, "projection"), 1, GL_FALSE, glm::value_ptr(game.projection));

    // Render Palette Matrix
    const auto clip_size = glm::vec2(2.0f);
    const auto palette_size = glm::vec2{COLS, ROWS};
    const auto normal_tile_size = clip_size / palette_size;
    for (int i = 0; i < ROWS; i++) {
        for (int j = 0; j < COLS; j++) {
            Rgb color = game.palette.matrix[i][j];
            Transform transform{};
            transform.scale = glm::vec3(0.5f);
            transform.position.x = (-1.0f + normal_tile_size.x / 2.0f) + (j * normal_tile_size.x);
            transform.position.y = (+1.0f - normal_tile_size.y / 2.0f) - (i * normal_tile_size.y);
            transform.scale.x = 0.85f / COLS;
            transform.scale.y = 0.85f / ROWS;
            glm::mat4 model = transform.matrix();
            glUniformMatrix4fv(glGetUniformLocation(game.shader_program, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniform3fv(glGetUniformLocation(game.shader_program, "color"), 1, glm::value_ptr(color.vec));
            glBindVertexArray(game.quad.glo.vao);
            glDrawArrays(GL_TRIANGLES, 0, game.quad.glo.count);
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
    while (!glfwWindowShouldClose(window)) {
        float dt = glfwGetTime();
        glfwPollEvents();
        game_render(game, dt);
        glfwSwapBuffers(window);
    }
    glfwSetWindowUserPointer(window, NULL);
    return 0;
}

/// Handle Key input event
void key_event_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;
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

/// Program Entrance
int main()
{
    int ret = 0;

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
