#include <cstdint>
#include <databento/datetime.hpp>
#include <databento/dbn_file_store.hpp>
#include <databento/symbol_map.hpp>
#include <databento/historical.hpp>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/gui_application.hpp"
#include "data/market.hpp"
#include "data/replay_engine.hpp"
#include "features/order_tracker.hpp"

namespace fs = std::filesystem;

struct Config {
  std::string command;
  std::string data_path;
  uint32_t focus_instrument = 0;
  std::vector<std::size_t> depths = {1, 2, 3, 4, 5};
  std::string api_key;
};

void print_usage() {
  std::cout
      << "Thesis Research suite\n"
      << "Usage: ./thesis [command] [args] [options]\n\n"
      << "Commands:\n"
      << "  gui <data_path>      Run high-performance GUI visualiser (OpenGL)\n"
      << "  order_analyser <data_path> Run order tracking analysis\n"
      << "  download <api_key>   Download historical market data from Databento\n\n"
      << "Options:\n"
      << "  --symbol <id>        Set focus instrument ID (optional for GUI)\n";
}

Config parse_args(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    throw std::runtime_error("Missing mandatory arguments.");
  }

  Config cfg;
  cfg.command = argv[1];

  if (cfg.command == "download") {
    if (argc >= 3) {
      cfg.api_key = argv[2];
    } else {
      const char *env_key = std::getenv("DATABENTO_API_KEY");
      if (env_key) {
        cfg.api_key = env_key;
      } else {
        throw std::runtime_error("No API key provided for download. Use 'download <api_key>' or set DATABENTO_API_KEY env var.");
      }
    }
  } else {
    if (argc < 3) {
      print_usage();
      throw std::runtime_error("Missing data_path for command: " + cfg.command);
    }
    cfg.data_path = argv[2];

    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--symbol" && i + 1 < argc) {
        cfg.focus_instrument = static_cast<uint32_t>(std::stoul(argv[++i]));
      } else {
        throw std::runtime_error("Unknown or malformed option: " + arg);
      }
    }
  }
  return cfg;
}

void run_order_analyser(const Config &cfg) {
  ReplayEngine engine(cfg.data_path);
  Market market;
  OrderTracker tracker(cfg.focus_instrument, FeedType::XNAS_ITCH, market);

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

void run_downloader(const Config &cfg) {
  const std::string DATASET = "XNAS.ITCH";
  const std::vector<std::string> SYMBOLS = {"AAPL", "MSFT", "AMZN", "NVDA", "GOOGL"};
  const std::string START_TIME = "2026-03-18T00:00:00Z";
  const std::string END_TIME = "2026-03-19T00:00:00Z";
  const std::string OUTPUT_PATH = "./data/multi_instrument.dbn.zst";
  const double MAX_COST_USD = 5.00;

  databento::Historical client =
      databento::Historical::Builder()
          .SetKey(cfg.api_key)
          .SetGateway(databento::HistoricalGateway::Bo1)
          .Build();

  std::cout << "\n--- Databento Downloader ---" << std::endl;
  std::cout << "Dataset: " << DATASET << std::endl;
  std::cout << "Symbols: ";
  for (size_t i = 0; i < SYMBOLS.size(); ++i) {
    std::cout << SYMBOLS[i] << (i == SYMBOLS.size() - 1 ? "" : ", ");
  }
  std::cout << "\nRange:   " << START_TIME << " to " << END_TIME << std::endl;

  databento::DateTimeRange<std::string> range{START_TIME, END_TIME};
  double estimated_cost = client.MetadataGetCost(DATASET, range, SYMBOLS, databento::Schema::Mbo, databento::SType::RawSymbol, 0);
  uint64_t billable_size = client.MetadataGetBillableSize(DATASET, range, SYMBOLS, databento::Schema::Mbo, databento::SType::RawSymbol, 0);

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "\nEstimated Cost:  $" << estimated_cost << " USD" << std::endl;
  std::cout << "Billable Size:   " << (billable_size / (1024.0 * 1024.0)) << " MB (uncompressed)" << std::endl;

  if (estimated_cost > MAX_COST_USD) {
    throw std::runtime_error("Estimated cost exceeds maximum limit.");
  }

  std::cout << "\nWARN: This operation will incur real-world costs. Proceed? [y/N]: ";
  std::string response;
  std::getline(std::cin, response);
  if (response != "y" && response != "Y") {
    std::cout << "Download aborted." << std::endl;
    return;
  }

  fs::path out_path(OUTPUT_PATH);
  if (out_path.has_parent_path()) {
    fs::create_directories(out_path.parent_path());
  }

  std::cout << "\nDownloading to " << OUTPUT_PATH << "..." << std::endl;
  client.TimeseriesGetRangeToFile(DATASET, range, SYMBOLS, databento::Schema::Mbo, databento::SType::RawSymbol, databento::SType::InstrumentId, 0, OUTPUT_PATH);
  std::cout << "\nSuccess!" << std::endl;
}

int main(int argc, char **argv) {
  try {
    Config cfg = parse_args(argc, argv);

    if (cfg.command == "gui") {
      run_gui_application(cfg);
    } else if (cfg.command == "order_analyser") {
      run_order_analyser(cfg);
    } else if (cfg.command == "download") {
      run_downloader(cfg);
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
