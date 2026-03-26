#include "metadata.hpp"
#include <databento/enums.hpp>
#include <iomanip>
#include <sstream>

MetadataParser::MetadataParser(const db::Metadata &meta) {
  version = meta.version;
  dataset = meta.dataset;

  schema = meta.schema.has_value() 
      ? db::ToString(meta.schema.value()) 
      : "Unknown";

  stype_in = meta.stype_in.has_value() 
      ? db::ToString(meta.stype_in.value())
      : "Unknown";

  stype_out = db::ToString(meta.stype_out);
  start_ts = meta.start.time_since_epoch().count();
  end_ts = meta.end.time_since_epoch().count();
  record_count = meta.limit;
  ts_out = meta.ts_out;
  symbols = meta.symbols;
  partial = meta.partial;
  not_found = meta.not_found;
}

MetadataSummary MetadataParser::GetSummary() const {
  return {version, dataset,      schema, stype_in, stype_out, start_ts,
          end_ts,  record_count, ts_out, symbols,  partial,   not_found};
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
