#include <databento/datetime.hpp>
#include <databento/dbn_file_store.hpp>
#include <databento/symbol_map.hpp>
#include <iostream>
#include <vector>

#include "market.hpp"
#include "replay_engine.hpp"
#include "telemetry.hpp"
#include "visualizer.hpp"

int main() {
  try {
    Market market;
    MarketTelemetry tel;
    ReplayEngine engine("./data/multi_instrument.dbn.zst");

    std::vector<std::size_t> depths = {1, 2, 3, 4, 5};
    Visualizer viz(depths);
    viz.SetFocus("AAPL");

    auto analysis = [&](const db::MboMsg &mbo) {
      tel.RecordAction(static_cast<char>(mbo.action));
      MarketState state = market.CaptureState(
          mbo.hd.instrument_id, depths, engine.GetSymbolMap().At(mbo),
          db::ToIso8601(mbo.ts_recv), mbo.ts_recv.time_since_epoch().count());

      viz.RecordAction(state, engine.GetMetadata());
    };

    engine.Run(market, analysis);

    tel.PrintSummary();

  } catch (const std::exception &e) {
    std::cerr << "Fatal Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
