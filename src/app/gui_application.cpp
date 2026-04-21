#include "app/gui_application.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

// GLFW and OpenGL
#include <GLFW/glfw3.h>

// ImGui headers
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

namespace fs = std::filesystem;

// Compute 09:30 ET (market open) on the calendar day of the given UTC nanosecond timestamp.
static uint64_t MarketOpenNanos(uint64_t file_start_ns) {
  using namespace std::chrono;
  auto tp  = system_clock::time_point{nanoseconds(file_start_ns)};
  auto zt  = zoned_time{"America/New_York", tp};
  auto day = floor<days>(zt.get_local_time());
  local_time<seconds> open{day + hours(9) + minutes(30)};
  auto open_utc = zoned_time{"America/New_York", open}.get_sys_time();
  return static_cast<uint64_t>(
      duration_cast<nanoseconds>(open_utc.time_since_epoch()).count());
}

// Known event files — shown as quick-pick buttons in the file picker
static constexpr const char *kDataRoot = "/home/aleri/git-repos/thesis-code/data";

static const struct { const char *label; const char *subpath; } kKnownFiles[] = {
    {"Oct 25 2022 (event)",    "MANIPULATION_WINDOWS/MULN_20221025.dbn.zst"},
    {"Dec 15 2022 (event)",    "MANIPULATION_WINDOWS/MULN_20221215.dbn.zst"},
    {"Jun 06 2023 (event)",    "MANIPULATION_WINDOWS/MULN_20230606.dbn.zst"},
    {"Aug 17 2023 (event)",    "MANIPULATION_WINDOWS/MULN_20230817.dbn.zst"},
    {"Oct 25 2022 (baseline)", "BASELINE/MULN_20221025_BASELINE.dbn.zst"},
    {"Dec 15 2022 (baseline)", "BASELINE/MULN_20221215_BASELINE.dbn.zst"},
    {"Jun 06 2023 (baseline)", "BASELINE/MULN_20230606_BASELINE.dbn.zst"},
    {"Aug 17 2023 (baseline)", "BASELINE/MULN_20230817_BASELINE.dbn.zst"},
};

Application::Application(const Config &cfg) : m_cfg(cfg) {
  InitGLFW();
  InitImGui();

  if (!cfg.data_path.empty()) {
    std::strncpy(m_path_buf, cfg.data_path.c_str(), sizeof(m_path_buf) - 1);
    LoadFile(cfg.data_path);
  }
}

Application::~Application() {
  if (m_controller) m_controller->Stop();
  Shutdown();
}

void Application::LoadFile(const std::string &path) {
  if (m_controller) m_controller->Stop();
  m_controller =
      std::make_unique<ReplayController>(path, m_cfg.focus_instrument);
  if (!m_cfg.target_ticker.empty()) {
    m_controller->SetFocusTicker(m_cfg.target_ticker);
  }
  if (!m_dashboard) m_dashboard = std::make_unique<Dashboard>();
  m_controller->Start();
  uint64_t open_ns = MarketOpenNanos(m_controller->GetFileStartTs());
  m_controller->SeekToTime(open_ns);  // warp to 09:30 ET, pauses on arrival
  m_file_loaded = true;
}

void Application::InitGLFW() {
  if (!glfwInit()) {
    throw std::runtime_error("Failed to initialize GLFW");
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  m_window = glfwCreateWindow(m_cfg.width, m_cfg.height,
                              "Market Visualizer", nullptr, nullptr);
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
  ImPlot::CreateContext();

  ImGui_ImplGlfw_InitForOpenGL(m_window, true);
  ImGui_ImplOpenGL3_Init("#version 330");
}

void Application::RenderFilePicker() {
  ImGuiIO &io = ImGui::GetIO();
  const float W = 560.0f, H = 340.0f;
  ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - W) * 0.5f,
                                 (io.DisplaySize.y - H) * 0.5f),
                          ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
  ImGui::Begin("Load DBN File", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

  ImGui::TextDisabled("Quick pick:");
  ImGui::Spacing();

  // Two-column button grid
  ImGui::Columns(2, "QuickPick", false);
  for (const auto &f : kKnownFiles) {
    std::string full_path = std::string(kDataRoot) + "/" + f.subpath;
    bool exists = fs::exists(full_path);
    if (!exists) ImGui::BeginDisabled();
    if (ImGui::Button(f.label, ImVec2(-1, 0))) {
      std::strncpy(m_path_buf, full_path.c_str(), sizeof(m_path_buf) - 1);
      LoadFile(full_path);
    }
    if (!exists) {
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("File not found: %s", full_path.c_str());
    }
    ImGui::NextColumn();
  }
  ImGui::Columns(1);

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::TextDisabled("Or enter path manually:");
  ImGui::SetNextItemWidth(-80);
  ImGui::InputText("##Path", m_path_buf, sizeof(m_path_buf));
  ImGui::SameLine();
  if (ImGui::Button("Load", ImVec2(70, 0)) && m_path_buf[0] != '\0') {
    LoadFile(std::string(m_path_buf));
  }

  ImGui::End();
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

    if (!m_file_loaded) {
      RenderFilePicker();
    } else {
      MarketSnapshot snap = m_controller->GetLatestSnapshot();
      m_dashboard->Render(snap, *m_controller);
      if (m_dashboard->ShouldQuit())
        glfwSetWindowShouldClose(m_window, true);
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(m_window);
  }
}

void Application::Shutdown() {
  if (m_window) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(m_window);
  }
  glfwTerminate();
}
