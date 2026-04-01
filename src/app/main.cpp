#include <cstdint>
#include <databento/datetime.hpp>
#include <databento/dbn_file_store.hpp>
#include <databento/symbol_map.hpp>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/gui_application.hpp"
#include "core/market.hpp"
#include "core/replay_engine.hpp"
#include "feature_engineering/order_tracker.hpp"

struct Config {
  std::string command;
  std::string data_path;
  uint32_t focus_instrument = 0;
  std::vector<std::size_t> depths = {1, 2, 3, 4, 5};
};

void print_usage() {
  std::cout
      << "Thesis Research suite\n"
      << "Usage: ./thesis [command] [data_path] [options]\n\n"
      << "Commands:\n"
      << "  gui               Run high-performance GUI visualiser (OpenGL)\n"
      << "  order_analyser    Run order tracking analysis\n\n"
      << "Options:\n"
      << "  --symbol <id>     Set focus instrument ID (optional for GUI)\n";
}

Config parse_args(int argc, char **argv) {
  if (argc < 3) {
    print_usage();
    throw std::runtime_error("Missing mandatory arguments.");
  }

  Config cfg;
  cfg.command = argv[1];
  cfg.data_path = argv[2];

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--symbol" && i + 1 < argc) {
      cfg.focus_instrument = static_cast<uint32_t>(std::stoul(argv[++i]));
    } else {
      throw std::runtime_error("Unknown or malformed option: " + arg);
    }
  }
  return cfg;
}

void run_order_analyser(const Config &cfg) {
  ReplayEngine engine(cfg.data_path);
  Market market;
  OrderTracker tracker(cfg.focus_instrument, FeedType::XNAS_ITCH);

  uint64_t msg_count = 0;
  const uint64_t MAX_MSGS = 10000;
  auto callback = [&](const db::MboMsg &mbo) {
    if (msg_count < MAX_MSGS) {
      tracker.Router(mbo);
      msg_count++;
      return true;
    }
    return false;
  };

  engine.Run(market, callback);
  tracker.DumpOrders("active_orders.csv");
}

void run_gui_application(const Config &cfg) {
  Application::Config gui_cfg;
  gui_cfg.data_path = cfg.data_path;
  gui_cfg.focus_instrument = cfg.focus_instrument;

  Application app(gui_cfg);
  app.Run();
}

int main(int argc, char **argv) {
  try {
    Config cfg = parse_args(argc, argv);

    if (cfg.command == "gui") {
      run_gui_application(cfg);
    } else if (cfg.command == "order_analyser") {
      run_order_analyser(cfg);
    } else {
      std::cerr << "Error: Unknown command '" << cfg.command << "'\n";
      print_usage();
      return 1;
    }
  } catch (const std::exception &e) {
    std::cerr << "Fatal Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
