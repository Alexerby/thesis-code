#include <databento/dbn_file_store.hpp>
#include <databento/symbol_map.hpp>
#include <iostream>

#include "databento/datetime.hpp"
#include "market.hpp"
#include "metadata.hpp"
#include "visualizer.hpp"

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

  // Define the levels your thesis cares about
  std::vector<std::size_t> depths = {1, 2, 3, 4, 5};

  // Create the visualizer
  Visualizer viz(depths);

  auto record_callback = [&](const db::Record &record) {
    if (auto *mbo = record.GetIf<db::MboMsg>()) {
      market.Apply(*mbo);

      if (mbo->flags.IsLast()) {
        MarketState state;
        state.symbol = symbol_map.At(*mbo);
        state.timestamp = db::ToIso8601(mbo->ts_recv);
        state.bbo = market.AggregatedBbo(mbo->hd.instrument_id);

        for (auto d : depths) {

          // Absolute imbalance (state)
          double imb = market.AggregatedDeepImbalance(mbo->hd.instrument_id, d);
          state.imbalance_levels.push_back({"L" + std::to_string(d), imb});

          // Change in imabalance (velocity)
          double diff = market.AggregatedImbalanceVelocity(mbo->hd.instrument_id, d);
          state.imbalance_levels.push_back({"L" + std::to_string(d) + " Diff ", diff});
        }

        // Pass the completed state to the visualizer
        viz.Render(state);
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
