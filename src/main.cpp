#include <chrono> // For sleep
#include <databento/dbn_file_store.hpp>
#include <databento/symbol_map.hpp>
#include <iomanip>
#include <iostream>
#include <thread> // For sleep

#include "constants.hpp"
#include "market.hpp"
#include "metadata.hpp"
// TODO: Define different callbacks for investigating the data,
// so we can pass these anonymous functions into our replay.

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

        // Clear screen
        std::cout << constants::CLEAR;
        std::cout << constants::YEL << ">> " << symbol << " | ";
        std::cout << db::ToIso8601(mbo->ts_recv) << constants::RESET << "\n";

        const std::vector<std::size_t> depths = {1, 3, 5, 10, 30, 50};

        for (auto d : depths) {
          double imb = market.AggregatedDeepImbalance(mbo->hd.instrument_id, d);
          int bar_width = 20;
          int pos = static_cast<int>(imb * bar_width);

          std::string label = "L" + std::to_string(d) + " Imbalance:";
          std::cout << std::left << std::setw(15) << label << " ["
                    << constants::GRN << std::string(pos, '|')
                    << constants::RESET << std::string(bar_width - pos, '.')
                    << "] " << std::fixed << std::setprecision(4) << imb
                    << "\n";
        }

        std::cout << "\n";
        if (!bbo.second.IsEmpty())
          std::cout << constants::RED << "  ASK  " << constants::RESET
                    << bbo.second << "\n";
        if (!bbo.first.IsEmpty())
          std::cout << constants::GRN << "  BID  " << constants::RESET
                    << bbo.first << "\n";

        std::cout << std::string(60, '-') << std::endl;

        // Pause the "Tape": 50ms delay
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
