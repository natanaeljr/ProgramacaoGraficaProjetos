#include <glm/ext/quaternion_transform.hpp>
#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

constexpr GLint WIDTH = 800, HEIGHT = 600;

int build_shader_program()
{
    const char* vertex_shader = R"(
#version 410
layout ( location = 0 ) in vec3 vPosition;
layout ( location = 1 ) in vec3 vColor;
uniform mat4 proj;
uniform mat4 model;
out vec3 color;
void main() {
    color = vColor;
    gl_Position = proj * model * vec4 ( vPosition, 1.0);
}
)";

    const char* fragment_shader = R"(
#version 410
in vec3 color;
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

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
};

struct GLObject {
    GLuint vao;
    GLuint vbo;
    size_t count;
};

GLObject create_gl_object(const Vertex vertices[], const size_t count)
{
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(Vertex), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid *)offsetof(Vertex, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid *)offsetof(Vertex, color));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return {vao, vbo, count};
}

static constexpr Vertex kQuadVertices[] = {
    {.pos = { -1.0f, -1.0f, +0.0f }, .color = { 1.0f, 0.0f, 0.0f }},
    {.pos = { -1.0f, +1.0f, +0.0f }, .color = { 1.0f, 0.0f, 1.0f }},
    {.pos = { +1.0f, -1.0f, +0.0f }, .color = { 0.0f, 1.0f, 0.0f }},
    {.pos = { +1.0f, -1.0f, +0.0f }, .color = { 0.0f, 1.0f, 0.0f }},
    {.pos = { -1.0f, +1.0f, +0.0f }, .color = { 1.0f, 0.0f, 1.0f }},
    {.pos = { +1.0f, +1.0f, +0.0f }, .color = { 0.0f, 0.0f, 1.0f }},
};

static constexpr Vertex kTriagleVertices[] = {
    {.pos = { -1.0f, -1.0f, +0.0f}, .color = {0.0f, 0.0f, 1.0f}},
    {.pos = { +0.0f, +1.0f, +0.0f}, .color = {1.0f, 0.0f, 0.0f}},
    {.pos = { +1.0f, -1.0f, +0.0f}, .color = {0.0f, 1.0f, 0.0f}},
};

struct Transform {
  glm::vec3 position;
  glm::vec3 scale;
  glm::quat rotation;

  glm::mat4 matrix() {
      glm::mat4 translation_mat = glm::translate(glm::mat4(1.0f), position);
      glm::mat4 rotation_mat = glm::toMat4(rotation);
      glm::mat4 scale_mat = glm::scale(glm::mat4(1.0f), scale);
      return translation_mat * rotation_mat * scale_mat;
  }
};

struct GameObject {
    GLObject glo;
    Transform transform;
};

int main()
{
    glfwInit();
    glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "ORTHO + MOUSE", nullptr, nullptr);
    if (window == nullptr) {
        std::cout << "Failed to create GLFW Window" << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cout << "Failed no init GLEW." << std::endl;
        return EXIT_FAILURE;
    }

    int shader_program = build_shader_program();

    glm::mat4 proj = glm::mat4(1);
    glm::mat4 model = glm::mat4(1);

    GameObject quad;
    quad.glo = create_gl_object(kQuadVertices, sizeof(kQuadVertices) / sizeof(Vertex)),
    quad.transform = Transform{
        .position = glm::vec3(+0.4f, 0.0f, 0.0f),
        .scale = glm::vec3(0.25f),
        .rotation = glm::quat(1.0f, glm::vec3(0.0f)),
    };

    GameObject triangle;
    triangle.glo = create_gl_object(kTriagleVertices, sizeof(kTriagleVertices) / sizeof(Vertex)),
    triangle.transform = Transform{
        .position = glm::vec3(-0.4f, 0.0f, 0.0f),
        .scale = glm::vec3(0.25f),
        .rotation = glm::quat(1.0f, glm::vec3(0.0f)),
    };

    while (!glfwWindowShouldClose(window)) {
        float dt = glfwGetTime();

        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        glClearColor(0.1f, 0.3f, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        int screenWidth, screenHeight;
        glfwGetWindowSize(window, &screenWidth, &screenHeight);
        glViewport(0, 0, screenWidth, screenHeight);

        glUseProgram(shader_program);
        glUniformMatrix4fv(glGetUniformLocation(shader_program, "proj"), 1, GL_FALSE, glm::value_ptr(proj));

        // Draw QUAD
        quad.transform.position.y = std::sin(dt) * 0.4f;
        quad.transform.rotation = glm::rotate(quad.transform.rotation, 0.01f, glm::vec3(0.0f, 0.0f, 1.0f));
        model = quad.transform.matrix();
        glUniformMatrix4fv(glGetUniformLocation(shader_program, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(quad.glo.vao);
        glDrawArrays(GL_TRIANGLES, 0, quad.glo.count);

        // Draw TRIANGLE
        triangle.transform.position.y = std::sin(dt) * -0.4f;
        model = triangle.transform.matrix();
        glUniformMatrix4fv(glGetUniformLocation(shader_program, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(triangle.glo.vao);
        glDrawArrays(GL_TRIANGLES, 0, triangle.glo.count);

        glfwSwapBuffers(window);
    }

    glfwTerminate();

    return EXIT_SUCCESS;
}

// vim: tabstop=4 shiftwidth=4
