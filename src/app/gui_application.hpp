#pragma once

#include <memory>
#include <string>

#include "app/dashboard.hpp"
#include "app/replay_controller.hpp"

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
    std::string target_ticker;
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
  void LoadFile(const std::string &path);
  void RenderFilePicker();

  GLFWwindow *m_window = nullptr;
  Config m_cfg;

  std::unique_ptr<ReplayController> m_controller;
  std::unique_ptr<Dashboard> m_dashboard;

  bool m_file_loaded = false;
  char m_path_buf[512] = {};
};
