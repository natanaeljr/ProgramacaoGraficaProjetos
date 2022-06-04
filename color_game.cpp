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

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/quaternion_transform.hpp>

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Constants

constexpr int WIDTH = 800, HEIGHT = 480; // window size
constexpr int COLS = 15, ROWS = 20;      // palette size
constexpr float TOLERANCE = 0.17;        // % (0 - 1)
constexpr int PICKING_COUNT = 5;         // color matching attempts

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Shader

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

///////////////////////////////////////////////////////////////////////////////////////////////////
/// GLObject

/// Vertex data
struct Vertex {
    glm::vec3 pos;
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

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Components

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
    bool hidden[ROWS][COLS];
    std::optional<glm::uvec2> target_index = std::nullopt;
    std::vector<glm::uvec2> match_indices;

    /// Create new Palette of random colors
    static Palette create_random() {
        Palette palette;
        std::srand(std::time(nullptr));
        for (int i = 0; i < ROWS; i++) {
            for (int j = 0; j < COLS; j++) {
                palette.matrix[i][j].r = (std::rand() % 255) / 255.f;
                palette.matrix[i][j].g = (std::rand() % 255) / 255.f;
                palette.matrix[i][j].b = (std::rand() % 255) / 255.f;
                palette.hidden[i][j] = false;
            }
        }
        return palette;
    }
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

/// Game states of FSM
enum class GameState {
    PICK_TARGET_COLOR,
    MATCH_COLORS,
    END,
};

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Game

/// Game & Engine data
struct Game {
    GLFWwindow* window;
    glm::uvec2 winsize;
    int shader_program;
    glm::mat4 projection;
    GameObject quad;
    Palette palette;
    GameState state;
    size_t pick_match_count;
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
    game.palette = Palette::create_random();
    game.state = GameState::PICK_TARGET_COLOR;
    game.pick_match_count = 0;

    printf("THE COLOR PICKING GAME\n"
           "Settings:\n"
           "- cols: %d, rows: %d\n"
           "- match tolerance: %.2f\n"
           "- color picking count: %d\n\n",
           COLS, ROWS, TOLERANCE, PICKING_COUNT);

    return 0;
}

/// Render Game scene
void game_render(Game& game)
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
            if (game.palette.hidden[i][j]) continue;
            Rgb color = game.palette.matrix[i][j];
            Transform transform{};
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
        glfwPollEvents();
        game_render(game);
        glfwSwapBuffers(window);
    }
    glfwSetWindowUserPointer(window, NULL);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Logic

/// Calculate the maximum distance of RGB vector
float max_rgb_distance() {
    return std::sqrt(std::pow(255, 2) + std::pow(255, 2) + std::pow(255, 2)) / 255.f;
}

/// Calculate the distance between two RGBs
float rgb_distance(Rgb c, Rgb o)
{
    return std::sqrt(std::pow(c.b - o.b, 2) + std::pow(c.b - o.b, 2) + std::pow(c.b - o.b, 2));
}

/// Check if match color is similar to target color
auto check_color_match(Game& game, glm::uvec2 match) -> std::pair<bool, float>
{
    glm::uvec2 target = *game.palette.target_index;
    Rgb target_color = game.palette.matrix[target.y][target.x];
    Rgb match_color = game.palette.matrix[match.y][match.x];
    float distance = rgb_distance(target_color, match_color);
    bool is_similar = distance <= (TOLERANCE * max_rgb_distance());
    return {is_similar, distance};
}

/// Convert cursor position to tile index
auto cursor_to_tile_index(Game& game, glm::vec2 cursor) -> std::pair<int, int>
{
    const auto palette_size = glm::vec2{COLS, ROWS};
    const auto tile_size = glm::vec2(game.winsize) / palette_size;
    int row = cursor.y / tile_size.y;
    int col = cursor.x / tile_size.x;
    return {row, col};
}

/// Pick the target color in the palette from cursor position
void pick_target_color(Game& game, glm::vec2 cursor)
{
    auto [row, col] = cursor_to_tile_index(game, cursor);
    Rgb color = game.palette.matrix[row][col];
    printf("Picked color RGB{%3d,%3d,%3d} @ row: %d, col: %d\n", (int)(color.r * 255), (int)(color.g * 255), (int)(color.b * 255), row, col);
    printf(">> TARGET defined\n");
    game.palette.target_index = {col, row};
}

/// Pick the match color to be compared against target color for the similarity
void pick_match_color(Game& game, glm::vec2 cursor)
{
    auto [row, col] = cursor_to_tile_index(game, cursor);
    Rgb color = game.palette.matrix[row][col];
    printf("Picked color RGB{%3d,%3d,%3d} @ row: %d, col: %d\n", (int)(color.r * 255), (int)(color.g * 255), (int)(color.b * 255), row, col);

    auto it = std::find(game.palette.match_indices.begin(), game.palette.match_indices.end(), glm::uvec2(col, row));
    if (it != game.palette.match_indices.end()) {
        printf("Color already picked!\n");
    }
    else if (game.palette.target_index == glm::uvec2(col, row)) {
        printf("This is the TARGET color!");
    }
    else if (auto [match, distance] = check_color_match(game, {col, row}); match) {
        printf(">> MATCH -> distance: %.2f\n", distance * 255);
        game.palette.match_indices.push_back(glm::uvec2(col, row));
        game.palette.hidden[row][col] = true;
        game.pick_match_count++;
    }
    else {
        printf(">> TOO FAR -> distance: %.2f\n", distance * 255);
        game.pick_match_count++;
    }
}

/// Prints the game score
void print_score(Game& game)
{
    printf("Score: %02zu/%02zu\n", game.palette.match_indices.size(), game.pick_match_count);
}

/// Play the color picking logic
void play_color_picking(Game& game, glm::vec2 cursor)
{
    if (game.state == GameState::PICK_TARGET_COLOR) {
        pick_target_color(game, cursor);
        game.state = GameState::MATCH_COLORS;
    }
    else if (game.state == GameState::MATCH_COLORS) {
        pick_match_color(game, cursor);
        if (game.pick_match_count >= PICKING_COUNT) {
            print_score(game);
            game.state = GameState::END;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Events

/// Handle Key input event
void key_event_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(game->window, 1);
}

/// Handle Mouse click events
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    auto game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;

    if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        play_color_picking(*game, {(float)mx, (float)my});
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
