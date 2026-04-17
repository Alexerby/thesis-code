#include <cstdint>
#include <databento/datetime.hpp>
#include <databento/dbn_file_store.hpp>
#include <databento/historical.hpp>
#include <databento/symbol_map.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/gui_application.hpp"
#include "app/visualizer.hpp"
#include "data/market.hpp"
#include "data/replay_engine.hpp"
#include "features/order_tracker.hpp"
#include "model/gmm.hpp"

// const uint64_t MAX_MSGS = 100'000'000;
// const uint64_t MAX_MSGS = 100'000;
const uint64_t MAX_MSGS = 2'000'000;

namespace fs = std::filesystem;

struct Config {
  std::string command;
  std::string data_path;
  uint32_t focus_instrument = 0;
  std::vector<std::size_t> depths = {1, 2, 3, 4, 5};
  std::string api_key;
  // databento-fetch options
  std::string dataset = "XNAS.ITCH";
  std::vector<std::string> symbols;
  std::string start_time;
  std::string end_time;
  std::string output_path;
};

void print_usage() {
  std::cout
      << "Thesis Research suite\n"
      << "Usage: ./thesis [command] [args] [options]\n\n"
      << "Commands:\n"
      << "  info <data_path>             Print file metadata and instrument ID → ticker map\n"
      << "  gui <data_path>              Run high-performance GUI visualiser\n"
      << "  model <data_path>            Run order tracking + GMM analysis\n"
      << "  plot <data_path>             Plot feature distributions (PNGs)\n"
      << "  databento-fetch              Fetch historical MBO data\n\n"
      << "Options (gui / model / plot):\n"
      << "  --symbol <id>                Focus instrument ID\n\n"
      << "Options (databento-fetch):\n"
      << "  --key     <api_key>          API key (or set DATABENTO_API_KEY)\n"
      << "  --dataset <dataset>          Dataset (default: XNAS.ITCH)\n"
      << "  --symbols <A,B,...>          Comma-separated symbols (required)\n"
      << "  --start   <ISO8601>          Start time, e.g. 2026-03-18T00:00:00Z\n"
      << "  --end     <ISO8601>          End time,   e.g. 2026-03-19T00:00:00Z\n"
      << "  --output  <path>             Output path (default: "
         "./data/multi_instrument.dbn.zst)\n";
}

Config parse_args(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    throw std::runtime_error("Missing mandatory arguments.");
  }

  Config cfg;
  cfg.command = argv[1];

  if (cfg.command == "databento-fetch") {
    for (int i = 2; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--key" && i + 1 < argc) {
        cfg.api_key = argv[++i];
      } else if (arg == "--dataset" && i + 1 < argc) {
        cfg.dataset = argv[++i];
      } else if (arg == "--symbols" && i + 1 < argc) {
        std::stringstream ss(argv[++i]);
        std::string sym;
        while (std::getline(ss, sym, ','))
          if (!sym.empty()) cfg.symbols.push_back(sym);
      } else if (arg == "--start" && i + 1 < argc) {
        cfg.start_time = argv[++i];
      } else if (arg == "--end" && i + 1 < argc) {
        cfg.end_time = argv[++i];
      } else if (arg == "--output" && i + 1 < argc) {
        cfg.output_path = argv[++i];
      } else {
        throw std::runtime_error("Unknown or malformed option: " + arg);
      }
    }
    if (cfg.api_key.empty()) {
      const char *env_key = std::getenv("DATABENTO_API_KEY");
      if (env_key) cfg.api_key = env_key;
      else throw std::runtime_error(
          "No API key. Use --key <key> or set DATABENTO_API_KEY.");
    }
    if (cfg.symbols.empty())
      throw std::runtime_error("--symbols is required.");
    if (cfg.start_time.empty() || cfg.end_time.empty())
      throw std::runtime_error("--start and --end are required.");
    if (cfg.output_path.empty())
      cfg.output_path = "./data/multi_instrument.dbn.zst";
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

void run_info(const Config &cfg) {
  ReplayEngine engine(cfg.data_path, /*print_metadata=*/false);
  const auto &meta = engine.GetMetadata();

  std::cout << "=== DBN File Info ===\n"
            << std::left << std::setw(16) << "Dataset:"   << meta.dataset << "\n"
            << std::left << std::setw(16) << "Schema:"
            << (meta.schema ? db::ToString(*meta.schema) : "Unknown") << "\n"
            << std::left << std::setw(16) << "Start:"     << db::ToIso8601(meta.start) << "\n"
            << std::left << std::setw(16) << "End:"       << db::ToIso8601(meta.end) << "\n"
            << std::left << std::setw(16) << "SType out:" << db::ToString(meta.stype_out) << "\n"
            << "\n"
            << std::left << std::setw(12) << "Ticker"
            << std::setw(14) << "Instrument ID"
            << "Date range\n"
            << std::string(50, '-') << "\n";

  for (const auto &mapping : meta.mappings) {
    for (const auto &interval : mapping.intervals) {
      std::cout << std::left << std::setw(12) << mapping.raw_symbol
                << std::setw(14) << interval.symbol
                << interval.start_date << " – "
                << interval.end_date << "\n";
    }
  }
}

void run_plot(const Config &cfg) {
  ReplayEngine engine(cfg.data_path);
  Market market;
  OrderTracker tracker(cfg.focus_instrument, FeedType::XNAS_ITCH, market);

  uint64_t msg_count = 0;
  auto callback = [&](const db::MboMsg &mbo) {
    if (msg_count < MAX_MSGS) {
      tracker.Router(mbo);
      msg_count++;
      return true;
    }
    return false;
  };

  engine.Run(market, callback);

  std::cout << "Collected " << tracker.feature_records_.size()
            << " feature records.\n";
  RunVisualizer(tracker.feature_records_);
}

void run_model(const Config &cfg) {
  ReplayEngine engine(cfg.data_path);
  Market market;
  OrderTracker tracker(cfg.focus_instrument, FeedType::XNAS_ITCH, market);

  uint64_t msg_count = 0;
  auto callback = [&](const db::MboMsg &mbo) {
    if (msg_count < MAX_MSGS) {
      tracker.Router(mbo);
      msg_count++;
      return true;
    }
    return false;
  };

  engine.Run(market, callback);

  // --- Semi-supervised GMM ---
  // All records enter the model so fill-cancelled orders anchor the
  // liquidity-consistent component. Their responsibilities are pinned to 0 in
  // every E-step via the fixed_lc mask — they can never be assigned to the
  // anomalous component.
  const auto &all_records = tracker.feature_records_;

  std::vector<bool> fixed_lc(all_records.size());
  int n_pure = 0, n_fill = 0;
  for (std::size_t i = 0; i < all_records.size(); ++i) {
    fixed_lc[i] = (all_records[i].cancel_type == CancelType::Fill);
    fixed_lc[i] ? ++n_fill : ++n_pure;
  }

  std::cout << "\nCollected " << all_records.size() << " total records  ("
            << n_pure << " pure, " << n_fill << " fill-anchored).\n";

  if (n_pure < 1000) {
    std::cout << "Too few pure cancellations to fit GMM (need >=10).\n";
    return;
  }

  // delta_t, induced_imbalance, volume_ahead
  const std::vector<int> feature_indices = {0, 1, 2}; 
  auto data = GMM::ToEigen(all_records, feature_indices);
  GMM::Standardize(data);

  FitOptions opts;
  opts.fixed_lc = fixed_lc;

  GMM gmm;
  GMMResult result = gmm.Fit(data, opts);

  const std::string out_path = "features/gmm_results.txt";
  std::ofstream out(out_path);
  if (!out) {
    std::cerr << "Failed to open " << out_path << " for writing.\n";
    return;
  }

  auto write_mean = [&](const Eigen::VectorXd &mu) {
    for (int j = 0; j < static_cast<int>(feature_indices.size()); ++j) {
      out << "    " << std::left << std::setw(20)
          << kFeatures[feature_indices[j]].name << std::right
          << std::setw(14) << std::fixed << std::setprecision(6) << mu[j]
          << "\n";
    }
  };

  out << "=== GMM Results ===\n"
      << "Observations:      " << all_records.size() << "\n"
      << "Iterations:        " << result.iterations << "\n"
      << "Log-likelihood:    " << std::fixed << std::setprecision(4)
      << result.log_likelihood << "\n"
      << "pi_spoof (pi_hat): " << std::setprecision(6) << result.pi_spoof
      << "\n"
      << "\nComponent 1 — anomalous (standardised mean):\n";
  write_mean(result.params.mu1);
  out << "\nComponent 2 — liquidity-consistent (standardised mean):\n";
  write_mean(result.params.mu2);

  std::cout << "GMM results written to " << out_path << "\n";
}

void run_gui_application(const Config &cfg) {
  Application::Config gui_cfg;
  gui_cfg.data_path = cfg.data_path;
  gui_cfg.focus_instrument = cfg.focus_instrument;

  Application app(gui_cfg);
  app.Run();
}

void run_downloader(const Config &cfg) {
  const double MAX_COST_USD = 5.00;

  databento::Historical client =
      databento::Historical::Builder()
          .SetKey(cfg.api_key)
          .SetGateway(databento::HistoricalGateway::Bo1)
          .Build();

  std::cout << "\n--- Databento Fetcher ---\n";
  std::cout << "Dataset: " << cfg.dataset << "\n";
  std::cout << "Symbols: ";
  for (size_t i = 0; i < cfg.symbols.size(); ++i)
    std::cout << cfg.symbols[i] << (i == cfg.symbols.size() - 1 ? "" : ", ");
  std::cout << "\nRange:   " << cfg.start_time << " to " << cfg.end_time
            << "\nOutput:  " << cfg.output_path << "\n";

  databento::DateTimeRange<std::string> range{cfg.start_time, cfg.end_time};
  double estimated_cost =
      client.MetadataGetCost(cfg.dataset, range, cfg.symbols,
                             databento::Schema::Mbo,
                             databento::SType::RawSymbol, 0);
  uint64_t billable_size =
      client.MetadataGetBillableSize(cfg.dataset, range, cfg.symbols,
                                     databento::Schema::Mbo,
                                     databento::SType::RawSymbol, 0);

  std::cout << std::fixed << std::setprecision(2)
            << "\nEstimated Cost:  $" << estimated_cost << " USD\n"
            << "Billable Size:   " << (billable_size / (1024.0 * 1024.0))
            << " MB (uncompressed)\n"
            << std::defaultfloat;

  if (estimated_cost > MAX_COST_USD)
    throw std::runtime_error("Estimated cost exceeds $" +
                             std::to_string(MAX_COST_USD) + " limit.");

  std::cout << "\nWARN: This operation will incur real-world costs. Proceed? [y/N]: ";
  std::string response;
  std::getline(std::cin, response);
  if (response != "y" && response != "Y") {
    std::cout << "Aborted.\n";
    return;
  }

  fs::path out_path(cfg.output_path);
  if (out_path.has_parent_path())
    fs::create_directories(out_path.parent_path());

  std::cout << "\nDownloading...\n";
  client.TimeseriesGetRangeToFile(
      cfg.dataset, range, cfg.symbols, databento::Schema::Mbo,
      databento::SType::RawSymbol, databento::SType::InstrumentId, 0,
      cfg.output_path);
  std::cout << "Success!\n";
}

int main(int argc, char **argv) {
  try {
    Config cfg = parse_args(argc, argv);

    if (cfg.command == "info") {
      run_info(cfg);
    } else if (cfg.command == "gui") {
      run_gui_application(cfg);
    } else if (cfg.command == "model") {
      run_model(cfg);
    } else if (cfg.command == "plot") {
      run_plot(cfg);
    } else if (cfg.command == "databento-fetch") {
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
