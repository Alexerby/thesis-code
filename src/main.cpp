#include "databento/dbn_file_store.hpp"
#include "databento/symbol_map.hpp"
#include "market.hpp"
#include "metadata.hpp"
#include <iostream>

int main() {
  const std::string file_path = "./data/fomc.dbn.zst";

  db::DbnFileStore file_store{file_path};
  Market market;
  db::TsSymbolMap symbol_map;

  auto metadata_callback = [&symbol_map](db::Metadata metadata) {
    symbol_map = metadata.CreateSymbolMap();
    MetadataParser mdParser(metadata);
    std::cout << mdParser.GetSummary().to_string() << std::endl;
  };

  auto record_callback = [&market, &symbol_map](const db::Record &record) {
    if (auto *mbo = record.GetIf<db::MboMsg>()) {

      market.Apply(*mbo);

      if (mbo->flags.IsLast()) {
        const auto &symbol = symbol_map.At(*mbo);
        auto bbo = market.AggregatedBbo(mbo->hd.instrument_id);

        // Matches the Databento example format
        std::cout << symbol << " Aggregated BBO | "
                  << db::ToIso8601(mbo->ts_recv) << "\n"
                  << "    " << bbo.second
                  << "\n"
                  << "    " << bbo.first
                  << std::endl;
      }
    }
    return db::KeepGoing::Continue;
  };

  file_store.Replay(metadata_callback, record_callback);

  return 0;
}
