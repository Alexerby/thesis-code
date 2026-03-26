#include <databento/dbn_file_store.hpp>
#include <databento/symbol_map.hpp>
#include <iomanip>
#include <iostream>

#include "market.hpp"
#include "metadata.hpp"

int main() {
  const std::string file_path = "./data/fomc.dbn.zst";

  db::DbnFileStore file_store{file_path};
  Market market;
  db::TsSymbolMap symbol_map;

  auto metadata_callback = [&symbol_map](db::Metadata metadata) {
    symbol_map = metadata.CreateSymbolMap();
    MetadataParser md_parser(metadata);
    std::cout << md_parser.GetSummary().to_string() << std::endl;
  };

  auto record_callback = [&](const db::Record &record) {
    if (auto *mbo = record.GetIf<db::MboMsg>()) {
      market.Apply(*mbo);

      if (mbo->flags.IsLast()) {
        const auto &symbol = symbol_map.At(*mbo);
        auto bbo = market.AggregatedBbo(mbo->hd.instrument_id);
        auto imbalance = market.AggregatedImbalance(mbo->hd.instrument_id);

        const std::string RED = "\033[1;31m";
        const std::string GRN = "\033[1;32m";
        const std::string YEL = "\033[1;33m";
        const std::string RESET = "\033[0m";

        std::cout << YEL << ">> " << symbol << " | "
                  << db::ToIso8601(mbo->ts_recv) << RESET << "\n";

        int bar_width = 20;
        int pos = static_cast<int>(imbalance * bar_width);
        std::cout << "Imbalance: [" << GRN << std::string(pos, '|') << RESET
                  << std::string(bar_width - pos, '.') << "] " << std::fixed
                  << std::setprecision(4) << imbalance << "\n";

        if (!bbo.second.IsEmpty()) {
          std::cout << RED << "  ASK  " << RESET << bbo.second << "\n";
        }
        if (!bbo.first.IsEmpty()) {
          std::cout << GRN << "  BID  " << RESET << bbo.first << "\n";
        }

        std::cout << std::string(60, '-') << std::endl;
      }
    }
    return db::KeepGoing::Continue;
  };

  try {
    file_store.Replay(metadata_callback, record_callback);
  } catch (const std::exception &e) {
    std::cerr << "Execution error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
