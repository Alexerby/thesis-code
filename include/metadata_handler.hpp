#pragma once

#include <databento/dbn_file_store.hpp>
#include <databento/record.hpp>
#include <string>
#include <vector>

namespace db = databento;

struct MetadataSummary {
    uint8_t version;
    std::string dataset;
    std::string schema;
    std::string stype_in;
    std::string stype_out;
    uint64_t start_ts;
    uint64_t end_ts;
    uint64_t record_count;       // limit
    bool ts_out;
    std::vector<std::string> symbols;
    std::vector<std::string> partial;
    std::vector<std::string> not_found;

    std::string to_string() const;
};

class MetadataParser {
  public:
    explicit MetadataParser(db::DbnFileStore &store);
    MetadataSummary GetSummary() const;

  private:
    uint8_t version;
    std::string dataset;
    std::string schema;
    std::string stype_in;
    std::string stype_out;
    uint64_t start_ts;
    uint64_t end_ts;
    uint64_t record_count;       // limit
    uint64_t total_observations; // actual count if provided
    bool ts_out;
    std::vector<std::string> symbols;
    std::vector<std::string> partial;
    std::vector<std::string> not_found;
};
