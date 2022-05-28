#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

const GLint WIDTH = 800, HEIGHT = 600;
glm::mat4 matrix = glm::mat4(1);
GLuint VAO, VBO;
std::vector<float> points;

void mouse(float mx, float my) {
    //float dx = mx - WIDTH / 2.f;
    //float dy = my - HEIGHT / 2.f;

    //matrix = glm::translate(glm::mat4(1), glm::vec3(dx, dy, 0));

    points.insert(points.end(), {mx, my, +0.0f});//    0.0f, 1.0f, 0.0f,

    if (points.size() >= 9) {
      glBindBuffer( GL_ARRAY_BUFFER, VBO);
      glBufferData( GL_ARRAY_BUFFER, points.size() * sizeof(float), points.data(), GL_STATIC_DRAW );
    }
}


int main() {

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
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


    const char* vertex_shader =
        "#version 410\n"
        "layout ( location = 0 ) in vec3 vPosition;"
        //"layout ( location = 1 ) in vec3 vColor;"
        "uniform mat4 proj;"
        "uniform mat4 matrix;"
        "out vec3 color;"
        "void main() {"
        "    color = vec3(0.3, 0.5, 0.2);"
        "    gl_Position = proj * matrix * vec4 ( vPosition, 1.0);"
        "}";

    const char* fragment_shader =
        "#version 410\n"
        "in vec3 color;"
        "out vec4 frag_color;"
        "void main(){"
        "  frag_color = vec4(color, 1.0f);"
        "}";

    int vs = glCreateShader (GL_VERTEX_SHADER);
    glShaderSource (vs, 1, &vertex_shader, NULL);
    glCompileShader (vs);
    int fs = glCreateShader (GL_FRAGMENT_SHADER);
    glShaderSource (fs, 1, &fragment_shader, NULL);
    glCompileShader (fs);

    int shader_programme = glCreateProgram ();
    glAttachShader (shader_programme, fs);
    glAttachShader (shader_programme, vs);
    glLinkProgram (shader_programme);

    //points = std::vector<float>{
        //// Positions                             // Colors
        //WIDTH * 0.25f, HEIGHT * 0.75f, +0.0f,//    0.0f, 0.0f, 1.0f,
        //WIDTH * 0.50f, HEIGHT * 0.25f, +0.0f,//    1.0f, 0.0f, 0.0f,
        //WIDTH * 0.75f, HEIGHT * 0.75f, +0.0f,//    0.0f, 1.0f, 0.0f,
        //WIDTH * 1.00f, HEIGHT * 1.00f, +0.0f,//    0.0f, 1.0f, 0.0f,
        //WIDTH * 0.50f, HEIGHT * 1.00f, +0.0f,//    0.0f, 1.0f, 0.0f,
    //};

    glm::mat4 proj = glm::ortho(0.0f, (float)WIDTH, (float)HEIGHT, 0.0f, -1.0f, 1.0f);


    glGenVertexArrays( 1, &VAO );
    glGenBuffers( 1, &VBO );
    glBindVertexArray( VAO );
    glBindBuffer( GL_ARRAY_BUFFER, VBO);
    //glBufferData( GL_ARRAY_BUFFER, 0, NULL, GL_STATIC_DRAW );
    glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof( GLfloat ), ( GLvoid * ) 0 );
    glEnableVertexAttribArray( 0 );
    //glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof( GLfloat ), (GLvoid *)(3*sizeof(float)) );
    //glEnableVertexAttribArray( 1 );

    glBindVertexArray( 0 );

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        const int state = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
        if (state == GLFW_PRESS) {
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            mouse(mx, my);
        }


        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }


        glClearColor(0.4f, 0.65f, 0.8f, 1.0f);
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

        int screenWidth, screenHeight;
        glfwGetWindowSize(window, &screenWidth, &screenHeight);
        glViewport(0, 0, screenWidth, screenHeight);

        // PASSAGEM DE PARÂMETROS PRA SHADERS
        glUseProgram (shader_programme);
        glUniformMatrix4fv(glGetUniformLocation(shader_programme, "proj"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniformMatrix4fv(glGetUniformLocation(shader_programme, "matrix"), 1, GL_FALSE, glm::value_ptr(matrix));

        if (points.size() >= 9) {
          glBindVertexArray( VAO );
          glDrawArrays(GL_TRIANGLE_FAN, 0, points.size() / 3);
          glBindVertexArray( 0 );
        }

        glfwSwapBuffers(window);
    }

    glfwTerminate();

    return EXIT_SUCCESS;
}
