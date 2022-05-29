#include <GL/glew.h>    // inclui lib auxiliar GLEW e a versão mais recente da OpenGL
#include <GLFW/glfw3.h> // GLFW biblioteca para interface com SO (janela, mouse, teclado, ...)
#include <stdio.h>      // biblioteca padrão C para I/O

int main () {
  // 1 - Inicialização da GLFW
  if (!glfwInit ()) {
    fprintf (stderr, "ERROR: could not start GLFW3\n");
    return 1;
  }
  // 1.1 - Necessário para Apple OS X
  glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  // 2 - Criação do contexto gráfico (window)
  GLFWwindow* window = glfwCreateWindow (640, 480, "Versao do Renderer", NULL, NULL);
  if (!window) {
    fprintf (stderr, "*** ERRO: não foi possível abrir janela com a GLFW\n");
    // 2.0 - Se não foi possível iniciar GLFW, então termina / remove lib GLFW da memória.
    glfwTerminate();
    return 1;
  }
  // 2.1 - Torna janela atual como o contexto gráfico atual para desenho
  glfwMakeContextCurrent (window);

  // 3 - Inicia manipulador da extensão GLEW 
  glewExperimental = GL_TRUE;
  glewInit ();

  // 4 - Objetivo do exemplo: detectar a versão atual do OpenGL e motor de renderização.
  const GLubyte* renderer = glGetString (GL_RENDERER);
  const GLubyte* version = glGetString (GL_VERSION);
  printf ("\n** Verificação da OpenGL ************\n");
  printf ("\tRenderer: %s\n", renderer);
  printf ("\tVersão suportada da OpenGL %s\n\n", version);

  /************** 
   * 5 - AQUI VEM TODO O CÓDIGO DA APP GRÁFICA, PRINCIPALMENTE, O MAIN LOOP
   **************/

  // 6 - Ao final, terminar / remover GLFW da memória.
  glfwTerminate();
  return 0;
}
