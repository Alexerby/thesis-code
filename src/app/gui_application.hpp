#pragma once

#include "app/dashboard.hpp"
#include "app/replay_controller.hpp"
#include <memory>
#include <string>

struct GLFWwindow;

/**
 * @class Application
 * @brief Main application shell that manages windowing and orchestration.
 */
class Application {
public:
  struct Config {
    std::string data_path;
    uint32_t focus_instrument;
    int width = 1280;
    int height = 720;
  };

  Application(const Config &cfg);
  ~Application();

  void Run();

  // Disable copying
  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;

private:
  void InitGLFW();
  void InitImGui();
  void Shutdown();

  GLFWwindow *m_window = nullptr;
  Config m_cfg;

  std::unique_ptr<ReplayController> m_controller;
  std::unique_ptr<Dashboard> m_dashboard;
};
