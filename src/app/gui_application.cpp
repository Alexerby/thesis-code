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

static constexpr const char *kDataRoot = "data";

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
  const float W = 560.0f, H = 420.0f;
  ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - W) * 0.5f,
                                 (io.DisplaySize.y - H) * 0.5f),
                          ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
  ImGui::Begin("Load DBN File", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

  // Scan ./data once; refresh on demand
  static std::vector<std::string> s_files;
  static bool s_scanned = false;
  auto rescan = [&]() {
    s_files.clear();
    const fs::path root{kDataRoot};
    if (fs::exists(root)) {
      for (const auto &entry : fs::recursive_directory_iterator(
               root, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file()) continue;
        const auto name = entry.path().filename().string();
        if (name.ends_with(".dbn.zst") || name.ends_with(".dbn"))
          s_files.push_back(entry.path().string());
      }
      std::sort(s_files.begin(), s_files.end());
    }
    s_scanned = true;
  };
  if (!s_scanned) rescan();

  ImGui::Text("./data");
  ImGui::SameLine(0, 16);
  if (ImGui::SmallButton("Refresh")) rescan();
  ImGui::Separator();

  ImGui::BeginChild("FileList", ImVec2(0, -65), true);
  if (s_files.empty()) {
    ImGui::TextDisabled("No .dbn / .dbn.zst files found under ./data");
  } else {
    const fs::path root{kDataRoot};
    for (const auto &full : s_files) {
      std::string rel = fs::relative(full, root).string();
      if (ImGui::Selectable(rel.c_str())) {
        std::strncpy(m_path_buf, full.c_str(), sizeof(m_path_buf) - 1);
        LoadFile(full);
      }
    }
  }
  ImGui::EndChild();

  ImGui::Spacing();
  ImGui::TextDisabled("Or enter path manually:");
  ImGui::SetNextItemWidth(-80);
  ImGui::InputText("##Path", m_path_buf, sizeof(m_path_buf));
  ImGui::SameLine();
  if (ImGui::Button("Load", ImVec2(70, 0)) && m_path_buf[0] != '\0')
    LoadFile(std::string(m_path_buf));

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
