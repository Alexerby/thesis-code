#include "metadata.hpp"
#include "databento/datetime.hpp"
#include <chrono>
#include <databento/enums.hpp>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace db = databento;

MetadataParser::MetadataParser(const db::Metadata &meta)
    : version_(meta.version), dataset_(meta.dataset),
      schema_(meta.schema.has_value() ? db::ToString(meta.schema.value())
                                      : "Unknown"),
      stype_in_(meta.stype_in.has_value() ? db::ToString(meta.stype_in.value())
                                          : "Unknown"),
      stype_out_(db::ToString(meta.stype_out)),
      start_ts_(meta.start.time_since_epoch().count()),
      end_ts_(meta.end.time_since_epoch().count()), record_count_(meta.limit),
      ts_out_(meta.ts_out), symbols_(meta.symbols), partial_(meta.partial),
      not_found_(meta.not_found) {}

MetadataSummary MetadataParser::GetSummary() const {
  return {version_, dataset_,      schema_, stype_in_, stype_out_, start_ts_,
          end_ts_,  record_count_, ts_out_, symbols_,  partial_,   not_found_};
}

std::string MetadataSummary::to_string() const {
  std::stringstream ss;
  auto row = [&](const std::string &label) -> std::ostream & {
    return ss << std::left << std::setw(20) << label;
  };

  ss << "--- DBN Dataset Summary ---\n";
  row("DBN Version:") << static_cast<int>(version) << "\n";
  row("Dataset:") << dataset << "\n";
  row("Schema:") << schema << "\n";

  row("Start Time:") << db::ToIso8601(
                            db::UnixNanos{std::chrono::nanoseconds{start_ts}})
                     << "\n";

  row("End Time:") << db::ToIso8601(
                          db::UnixNanos{std::chrono::nanoseconds{end_ts}})
                   << "\n";

  row("SType In/Out:") << stype_in << " -> " << stype_out << "\n";
  row("Record Limit:") << (record_count == 0 ? "None"
                                             : std::to_string(record_count))
                       << "\n";

  auto print_list = [&](const std::string &label,
                        const std::vector<std::string> &list) {
    if (list.empty() && label != "Symbols")
      return;
    row(label + " [" + std::to_string(list.size()) + "]:");
    for (const auto &sym : list)
      ss << sym << " ";
    ss << "\n";
  };

  print_list("Symbols", symbols);
  ss << "---------------------------\n";

  return ss.str();
}

void MetadataParser::ValidateSchema() const {
  if (this->schema_ != "mbo") {
    std::cerr << "FATAL: Dataset schema is '" << schema_
              << "'. Thesis logic requires 'mbo' (Market By Order).\n"
              << "Aggregated schemas (mbp/tob) will break Order ID tracking."
              << std::endl;
    std::exit(1);
  }
}
