#include <cstdint>
#include <databento/datetime.hpp>
#include <databento/dbn_file_store.hpp>
#include <databento/historical.hpp>
#include <databento/symbol_map.hpp>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/gui_application.hpp"
#include "data/market.hpp"
#include "data/replay_engine.hpp"
#include "features/csv.hpp"
#include "features/order_tracker.hpp"

namespace fs = std::filesystem;

struct Config {
  std::string command;
  std::string data_path;
  uint32_t focus_instrument = 0;
  std::string target_ticker;
  std::vector<std::size_t> depths = {1, 2, 3, 4, 5};
  uint64_t limit = 0;          // extract-features: max MBO messages (0 = all)
  std::string output_path;     // extract-features: output CSV path
  std::string api_key;
  // databento-fetch options
  std::string dataset = "XNAS.ITCH";
  std::string schema  = "mbo";
  std::string stype   = "raw_symbol";
  std::vector<std::string> symbols;
  std::string start_time;
  std::string end_time;
  std::string fetch_output_path;
};

void print_usage() {
  std::cout
      << "Thesis Research suite\n"
      << "Usage: ./thesis [command] [args] [options]\n\n"
      << "Commands:\n"
      << "  describe <data_path>             Print file metadata and instrument ID → ticker map\n"
      << "  gui <data_path>              Run high-performance GUI visualiser\n"
      << "  extract-features <data_path> Run order tracking and write feature CSV\n"
      << "  databento-fetch              Fetch historical MBO data\n\n"
      << "Options (gui / extract-features):\n"
      << "  --symbol <id>                Focus instrument ID (optional, selectable in-app)\n"
      << "  --ticker <name>              Focus ticker (resolves all IDs in file)\n\n"
      << "Options (extract-features):\n"
      << "  --limit  <N>                 Stop after N MBO messages (default: all)\n"
      << "  --output <path>              Output CSV path (default: features/records.csv)\n\n"
      << "Options (databento-fetch):\n"
      << "  --key     <api_key>          API key (or set DATABENTO_API_KEY)\n"
      << "  --dataset <dataset>          Dataset (default: XNAS.ITCH)\n"
      << "  --schema  <schema>           Schema (default: mbo; e.g. trades, tbbo, mbp-1)\n"
      << "  --stype   <stype>            Symbol type (default: raw_symbol; e.g. parent, continuous)\n"
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
      } else if (arg == "--schema" && i + 1 < argc) {
        cfg.schema = argv[++i];
      } else if (arg == "--stype" && i + 1 < argc) {
        cfg.stype = argv[++i];
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
        cfg.fetch_output_path = argv[++i];
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
    if (cfg.fetch_output_path.empty())
      cfg.fetch_output_path = "./data/multi_instrument.dbn.zst";
  } else {
    if (argc < 3) {
      print_usage();
      throw std::runtime_error("Missing data_path for command: " + cfg.command);
    }
    cfg.data_path = argv[2];

    bool symbol_set = false;
    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--symbol" && i + 1 < argc) {
        cfg.focus_instrument = static_cast<uint32_t>(std::stoul(argv[++i]));
        symbol_set = true;
      } else if (arg == "--ticker" && i + 1 < argc) {
        cfg.target_ticker = argv[++i];
        symbol_set = true;
      } else if (arg == "--limit" && i + 1 < argc) {
        cfg.limit = std::stoull(argv[++i]);
      } else if (arg == "--output" && i + 1 < argc) {
        cfg.output_path = argv[++i];
      } else {
        throw std::runtime_error("Unknown or malformed option: " + arg);
      }
    }
    if (cfg.command == "extract-features" && !symbol_set)
      throw std::runtime_error("--symbol <id> is required for 'extract-features'. Run 'describe' first to list instrument IDs.");
    if (cfg.command == "extract-features" && cfg.output_path.empty())
      cfg.output_path = "features/records.csv";
  }
  return cfg;
}

void run_describe(const Config &cfg) {
  ReplayEngine engine(cfg.data_path, /*print_metadata=*/false);
  const auto &meta = engine.GetMetadata();

  // Sample the first 500k messages and extrapolate total counts
  const uint64_t SAMPLE_SIZE = 500'000;
  std::unordered_map<uint32_t, uint64_t> msg_counts;
  uint64_t sampled = 0;
  Market market;
  engine.Run(market, [&](const db::MboMsg &mbo) {
    if (sampled >= SAMPLE_SIZE) return false;
    msg_counts[mbo.hd.instrument_id]++;
    sampled++;
    return true;
  });

  // Build instrument_id → raw_symbol lookup from mappings
  std::unordered_map<std::string, std::string> id_to_ticker;
  for (const auto &mapping : meta.mappings)
    for (const auto &interval : mapping.intervals)
      id_to_ticker[interval.symbol] = mapping.raw_symbol;

  std::cout << "=== DBN File Info ===\n"
            << std::left << std::setw(16) << "Dataset:"
            << meta.dataset << "\n"
            << std::left << std::setw(16) << "Schema:"
            << (meta.schema ? db::ToString(*meta.schema) : "Unknown") << "\n"
            << std::left << std::setw(16) << "Start:"
            << db::ToIso8601(meta.start) << "\n"
            << std::left << std::setw(16) << "End:"
            << db::ToIso8601(meta.end) << "\n"
            << std::left << std::setw(16) << "SType out:"
            << db::ToString(meta.stype_out) << "\n"
            << "\n"
            << std::left << std::setw(10) << "Ticker"
            << std::setw(16) << "Instrument ID"
            << std::setw(20) << "Msgs (500k sample)"
            << "Date range\n"
            << std::string(58, '-') << "\n";

  for (const auto &mapping : meta.mappings) {
    for (const auto &interval : mapping.intervals) {
      uint32_t inst_id = static_cast<uint32_t>(std::stoul(interval.symbol));
      uint64_t count = msg_counts.count(inst_id) ? msg_counts.at(inst_id) : 0;
      std::cout << std::left << std::setw(10) << mapping.raw_symbol
                << std::setw(16) << interval.symbol
                << std::setw(20) << count
                << interval.start_date << " – " << interval.end_date << "\n";
    }
  }
}

void run_extract_features(const Config &cfg) {
  ReplayEngine engine(cfg.data_path);
  const auto &meta = engine.GetMetadata();

  std::set<uint32_t> instrument_ids;
  if (!cfg.target_ticker.empty()) {
    std::cout << "Resolving ticker '" << cfg.target_ticker << "'...\n";
    for (const auto &mapping : meta.mappings) {
      if (mapping.raw_symbol == cfg.target_ticker) {
        for (const auto &interval : mapping.intervals) {
          uint32_t id = static_cast<uint32_t>(std::stoul(interval.symbol));
          instrument_ids.insert(id);
          std::cout << "  Found ID: " << id << " (" << interval.start_date
                    << " to " << interval.end_date << ")\n";
        }
      }
    }
  } else {
    instrument_ids.insert(cfg.focus_instrument);
  }

  if (instrument_ids.empty()) {
    throw std::runtime_error("No instrument IDs found for ticker or symbol.");
  }

  Market market;
  OrderTracker tracker(instrument_ids, FeedType::XNAS_ITCH, market);

  uint64_t msg_count = 0;
  engine.Run(market, [&](const db::MboMsg &mbo) {
    if (cfg.limit > 0 && msg_count >= cfg.limit) return false;
    tracker.Router(mbo);
    ++msg_count;
    return true;
  });

  const auto &records = tracker.feature_records_;
  std::cout << "Processed " << msg_count << " messages → "
            << records.size() << " feature records.\n";

  fs::path out(cfg.output_path);
  CSVWriter csv;
  if (!csv.Open(out.filename().string(), out.parent_path().string())) {
    std::cerr << "Failed to open " << cfg.output_path << " for writing.\n";
    return;
  }

  csv.Write("ts_recv");
  csv.Write("cancel_type");
  csv.Write("delta_t");
  csv.Write("volume_ahead");
  csv.Write("induced_imbalance");
  csv.Write("relative_size");
  csv.Write("price_distance_ticks");
  csv.Write("spread_bps");
  csv.Write("mid_price", /*is_last=*/true);

  for (const auto &r : records) {
    csv.Write(r.ts_recv);
    csv.Write(static_cast<int>(r.cancel_type));
    csv.Write(r.delta_t);
    csv.Write(r.volume_ahead);
    csv.Write(r.induced_imbalance);
    csv.Write(r.relative_size);
    csv.Write(r.price_distance_ticks);
    csv.Write(r.spread_bps);
    csv.Write(r.mid_price, /*is_last=*/true);
  }
  csv.Flush();
  std::cout << "Written to " << cfg.output_path << "\n";
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
            << "\nOutput:  " << cfg.fetch_output_path << "\n";

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

  fs::path out_path(cfg.fetch_output_path);
  if (out_path.has_parent_path())
    fs::create_directories(out_path.parent_path());

  databento::Schema schema = databento::Schema::Mbo;
  if      (cfg.schema == "trades")     schema = databento::Schema::Trades;
  else if (cfg.schema == "tbbo")       schema = databento::Schema::Tbbo;
  else if (cfg.schema == "mbp-1")      schema = databento::Schema::Mbp1;
  else if (cfg.schema == "mbp-10")     schema = databento::Schema::Mbp10;
  else if (cfg.schema == "bbo-1s")     schema = databento::Schema::Bbo1S;
  else if (cfg.schema == "bbo-1m")     schema = databento::Schema::Bbo1M;
  else if (cfg.schema == "ohlcv-1s")   schema = databento::Schema::Ohlcv1S;
  else if (cfg.schema == "ohlcv-1m")   schema = databento::Schema::Ohlcv1M;
  else if (cfg.schema == "ohlcv-1h")   schema = databento::Schema::Ohlcv1H;
  else if (cfg.schema == "ohlcv-1d")   schema = databento::Schema::Ohlcv1D;
  else if (cfg.schema == "imbalance")  schema = databento::Schema::Imbalance;
  else if (cfg.schema == "definition") schema = databento::Schema::Definition;
  else if (cfg.schema == "statistics") schema = databento::Schema::Statistics;
  else if (cfg.schema != "mbo")
    throw std::runtime_error("Unknown schema: " + cfg.schema);

  databento::SType stype_in = databento::SType::RawSymbol;
  if      (cfg.stype == "parent")     stype_in = databento::SType::Parent;
  else if (cfg.stype == "continuous") stype_in = databento::SType::Continuous;
  else if (cfg.stype != "raw_symbol")
    throw std::runtime_error("Unknown stype: " + cfg.stype);

  std::cout << "\nDownloading...\n";
  client.TimeseriesGetRangeToFile(
      cfg.dataset, range, cfg.symbols, schema,
      stype_in, databento::SType::InstrumentId, 0,
      cfg.fetch_output_path);
  std::cout << "Success!\n";
}

int main(int argc, char **argv) {
  try {
    Config cfg = parse_args(argc, argv);

    if (cfg.command == "describe") {
      run_describe(cfg);
    } else if (cfg.command == "gui") {
      run_gui_application(cfg);
    } else if (cfg.command == "extract-features") {
      run_extract_features(cfg);
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
