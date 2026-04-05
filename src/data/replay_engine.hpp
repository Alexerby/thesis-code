#pragma once

#include <databento/datetime.hpp>
#include <databento/dbn_decoder.hpp>
#include <databento/dbn_file_store.hpp>
#include <databento/enums.hpp>
#include <databento/file_stream.hpp>
#include <databento/log.hpp>
#include <databento/record.hpp>
#include <databento/symbol_map.hpp>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>

#include "data/market.hpp"

namespace db = databento;

class ReplayEngine {
 public:
  explicit ReplayEngine(const std::string &path, bool print_metadata = true)
      : file_store_{path}, print_metadata_(print_metadata) {
    // Pre-load metadata so it's available for validation before Run()
    db::InFileStream file_stream{path};
    db::DbnDecoder decoder{db::ILogReceiver::Default(), std::move(file_stream)};
    metadata_ = decoder.DecodeMetadata();
    symbol_map_ = metadata_.CreateSymbolMap();
  }

  void Run(Market &market,
           std::function<bool(const db::MboMsg &)> on_state_update) {
    // Metadata callback (1 out of 2 callbacks)
    auto metadata_cb = [this](db::Metadata metadata) {
      if (print_metadata_) {
        // Print a simple metadata summary
        std::cout << "--- DBN Dataset Summary ---\n"
                  << std::left << std::setw(20)
                  << "DBN Version:" << static_cast<int>(metadata.version)
                  << "\n"
                  << std::left << std::setw(20)
                  << "Dataset:" << metadata.dataset << "\n"
                  << std::left << std::setw(20) << "Schema:"
                  << (metadata.schema ? db::ToString(*metadata.schema)
                                      : "Unknown")
                  << "\n"
                  << std::left << std::setw(20)
                  << "Start Time:" << db::ToIso8601(metadata.start) << "\n"
                  << std::left << std::setw(20)
                  << "End Time:" << db::ToIso8601(metadata.end) << "\n"
                  << std::left << std::setw(20) << "SType In/Out:"
                  << (metadata.stype_in ? db::ToString(*metadata.stype_in)
                                        : "Unknown")
                  << " -> " << db::ToString(metadata.stype_out) << "\n"
                  << std::left << std::setw(20) << "Record Limit:"
                  << (metadata.limit == 0 ? "None"
                                          : std::to_string(metadata.limit))
                  << "\n"
                  << "---------------------------\n"
                  << std::endl;
      }

      // Validate schema
      if (metadata.schema && *metadata.schema != db::Schema::Mbo) {
        std::cerr
            << "FATAL: Dataset schema is '" << db::ToString(*metadata.schema)
            << "'. Thesis logic requires 'mbo' (Market By Order).\n"
            << "Aggregated schemas (mbp/tob) will break Order ID tracking."
            << std::endl;
        std::exit(1);
      }
    };

    // Record callback runs for every message in the DBN file (2 out of 2
    // callbacks)
    auto record_cb = [&](const db::Record &record) {
      if (auto *mbo = record.GetIf<db::MboMsg>()) {
        // Update the order book state
        market.Apply(*mbo);

        // Analysis triggers for every message to ensure telemetry/fills are
        // captured
        if (!on_state_update(*mbo)) {
          return db::KeepGoing::Stop;
        }
      }
      return db::KeepGoing::Continue;
    };

    file_store_.Replay(metadata_cb, record_cb);
  }

  // Helper to resolve instrument IDs to ticker strings
  const db::TsSymbolMap &GetSymbolMap() const { return symbol_map_; }
  const db::Metadata &GetMetadata() const { return metadata_; }

 private:
  db::DbnFileStore file_store_;
  db::TsSymbolMap symbol_map_;
  db::Metadata metadata_;
  bool print_metadata_;
};
