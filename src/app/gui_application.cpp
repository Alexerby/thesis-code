#include "app/gui_application.hpp"
#include <stdexcept>

// GLFW and OpenGL
#include <GLFW/glfw3.h>

// ImGui headers
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

Application::Application(const Config &cfg) : m_cfg(cfg) {
  InitGLFW();
  InitImGui();

  m_controller =
      std::make_unique<ReplayController>(cfg.data_path, cfg.focus_instrument);
  m_dashboard = std::make_unique<Dashboard>();

  m_controller->Start();
}

Application::~Application() {
  m_controller->Stop();
  Shutdown();
}

void Application::InitGLFW() {
  if (!glfwInit()) {
    throw std::runtime_error("Failed to initialize GLFW");
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  m_window = glfwCreateWindow(m_cfg.width, m_cfg.height,
                              "Thesis Market Visualizer", nullptr, nullptr);
  if (!m_window) {
    glfwTerminate();
    throw std::runtime_error("Failed to create GLFW window");
  }

  glfwMakeContextCurrent(m_window);
  glfwSwapInterval(1);
}

void Application::InitImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForOpenGL(m_window, true);
  ImGui_ImplOpenGL3_Init("#version 330");
}

void Application::Run() {
  while (!glfwWindowShouldClose(m_window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int display_w, display_h;
    glfwGetFramebufferSize(m_window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);

    glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Get an updated snapshot of the market
    MarketSnapshot snap = m_controller->GetLatestSnapshot();

    // Render the updated snapshot
    m_dashboard->Render(snap, *m_controller);

    // Kill condition
    if (m_dashboard->ShouldQuit()) {
      glfwSetWindowShouldClose(m_window, true);
    }

    // 
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(m_window);
  }
}

void Application::Shutdown() {
  if (m_window) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(m_window);
  }
  glfwTerminate();
}
