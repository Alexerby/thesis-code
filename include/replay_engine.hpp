#pragma once

#include <functional>
#include <iostream>
#include <string>

#include <databento/dbn_file_store.hpp>
#include <databento/record.hpp>
#include <databento/symbol_map.hpp>

#include "market.hpp"
#include "metadata.hpp"

namespace db = databento;

class ReplayEngine {
public:
  explicit ReplayEngine(const std::string &path) : file_store_{path} {}

  void Run(Market &market,
           std::function<void(const db::MboMsg &)> on_state_update) {

    // Metadata callback runs once at the start of the file
    auto metadata_cb = [this](db::Metadata metadata) {
      // Create and print the metadata summary
      MetadataParser md_parser(metadata);
      this->metadata_summary_ = md_parser.GetSummary();
      std::cout << this->metadata_summary_.to_string() << std::endl;

      // Store the symbol map for the engine's use if needed
      this->symbol_map_ = metadata.CreateSymbolMap();
    };

    // Record callback runs for every message in the DBN file
    auto record_cb = [&](const db::Record &record) {
      if (auto *mbo = record.GetIf<db::MboMsg>()) {

        // Validation check for ToB/MBP (runs once per file)

        // Update the order book state
        market.Apply(*mbo);

        // Analysis triggers for every message to ensure telemetry/fills are captured
        on_state_update(*mbo);
      }
      return db::KeepGoing::Continue;
    };

    file_store_.Replay(metadata_cb, record_cb);
  }

  // Helper to resolve instrument IDs to ticker strings
  const db::TsSymbolMap &GetSymbolMap() const { return symbol_map_; }
  const MetadataSummary &GetMetadata() const { return metadata_summary_; }

private:
  db::DbnFileStore file_store_;
  db::TsSymbolMap symbol_map_;
  MetadataSummary metadata_summary_;
  bool validated_mbo_type_{false};
};
