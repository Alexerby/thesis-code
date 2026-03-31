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

#include "market.hpp"
#include "order_tracker.hpp"
#include "replay_engine.hpp"
#include "visualizer.hpp"

struct Config {
  std::string command;
  std::string data_path;
  uint32_t focus_instrument;
  std::vector<std::size_t> depths = {1, 2, 3, 4, 5};
};

void print_usage() {
  std::cout << "Thesis Research suite\n"
            << "Usage: ./thesis [command] [data_path] [options]\n\n"
            << "Commands:\n"
            << "  viz               Run interactive market visualiser\n"
            << "  order_analyser    Run order tracking analysis\n\n"
            << "Options:\n"
            << "  --symbol <id>     Set focus instrument ID\n";
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

      // Convert char* to uint32_t
      cfg.focus_instrument = static_cast<uint32_t>(std::stoul(argv[++i]));
    } else {
      throw std::runtime_error("Unknown or malformed option: " + arg);
    }
  }
  return cfg;
}

void run_visualizer(const Config &cfg) {
  ReplayEngine engine(cfg.data_path);
  Market market;
  Visualizer viz(cfg.depths);

  auto &symbol_map = engine.GetSymbolMap();

  // Get metadata for global start/end times
  const auto &metadata = engine.GetMetadata();

  bool id_found = false;
  std::map<uint32_t, std::string> available_symbols;

  for (const auto &entry : symbol_map.Map()) {
    available_symbols[entry.first.second] = *entry.second;
    if (entry.first.second == cfg.focus_instrument) {
      id_found = true;
    }
  }

  if (!id_found) {
    std::cerr << "\n"
              << "\033[1;31m" << "Error: Instrument ID " << cfg.focus_instrument
              << " not found in data file." << "\033[0m" << "\n\n"
              << "Available mappings in this file:\n"
              << std::left << std::setw(15) << "ID" << " | " << "Symbol" << "\n"
              << std::string(30, '-') << "\n";

    for (const auto &mapping : available_symbols) {
      std::cerr << std::left << std::setw(15) << mapping.first << " | "
                << mapping.second << "\n";
    }
    std::cerr << "\n"
              << "Please use one of the IDs above with --symbol <id>."
              << std::endl;
    std::exit(1);
  }

  auto callback = [&](const db::MboMsg &mbo) {
    if (mbo.hd.instrument_id != cfg.focus_instrument) {
      return;
    }

    const std::string &symbol = symbol_map.At(mbo);

    static bool focus_set = false;
    if (!focus_set) {
      viz.SetFocus(symbol);
      focus_set = true;
    }

    MarketState state = market.CaptureState(
        mbo.hd.instrument_id, cfg.depths, symbol, db::ToIso8601(mbo.ts_recv),
        mbo.ts_recv.time_since_epoch().count());

    // Pass both the state and the metadata
    viz.RecordAction(state, metadata);
  };

  engine.Run(market, callback);
}

void run_order_analyser(const Config &cfg) {
  ReplayEngine engine(cfg.data_path);
  Market market;

  auto &symbol_map = engine.GetSymbolMap();
  bool id_found = false;
  std::map<uint32_t, std::string> available_symbols;

  for (const auto &entry : symbol_map.Map()) {
    available_symbols[entry.first.second] = *entry.second;
    if (entry.first.second == cfg.focus_instrument) {
      id_found = true;
    }
  }

  if (!id_found) {
    std::cerr << "\n"
              << "\033[1;31m" << "Error: Instrument ID " << cfg.focus_instrument
              << " not found in data file." << "\033[0m" << "\n\n"
              << "Available mappings in this file:\n"
              << std::left << std::setw(15) << "ID" << " | " << "Symbol" << "\n"
              << std::string(30, '-') << "\n";

    for (const auto &mapping : available_symbols) {
      std::cerr << std::left << std::setw(15) << mapping.first << " | "
                << mapping.second << "\n";
    }
    std::cerr << "\n"
              << "Please use one of the IDs above with --symbol <id>."
              << std::endl;
    std::exit(1);
  }

  OrderTracker tracker(cfg.focus_instrument, FeedType::XNAS_ITCH);

  auto callback = [&](const db::MboMsg &mbo) { tracker.Router(mbo); };

  std::cout << "Starting Order Analysis for ID: " << cfg.focus_instrument
            << std::endl;

  engine.Run(market, callback);

  std::cout << "Final Active Orders: " << tracker.order_map.size() << std::endl;
}

int main(int argc, char **argv) {
  try {
    Config cfg = parse_args(argc, argv);

    if (cfg.command == "viz") {
      run_visualizer(cfg);
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
