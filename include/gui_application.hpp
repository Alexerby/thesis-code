#pragma once

#include <functional>
#include <string>

// Forward declaration to avoid including GLFW in the header
struct GLFWwindow;

/**
 * @class Application
 * @brief Manages the GLFW, OpenGL, and ImGui lifecycle.
 */
class Application {
public:
  /**
   * @brief Initializes the windowing system and OpenGL context.
   * @param title The window title.
   * @param width Initial window width (default 1280).
   * @param height Initial window height (default 720).
   */
  Application(const std::string &title, int width = 1280, int height = 720);

  /**
   * @brief Destructor handles proper shutdown of GLFW and ImGui.
   */
  ~Application();

  /**
   * @brief Starts the main loop.
   * @param update_func A function or lambda called every frame to render the
   * UI.
   */
  void Run(std::function<void()> update_func);

  // Disable copying to prevent double-deletion of the GLFW window
  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;

private:
  void InitGLFW();
  void InitImGui();
  void Shutdown();

  GLFWwindow *m_window = nullptr;
  int m_width;
  int m_height;
  std::string m_title;
};
