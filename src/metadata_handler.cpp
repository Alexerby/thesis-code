#include "metadata_handler.hpp"
#include "databento/dbn_file_store.hpp"
#include "datetime.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>

MetadataParser::MetadataParser(db::DbnFileStore &store) {
    const auto &meta = store.GetMetadata();
    
    version = meta.version;
    dataset = meta.dataset;
    
    // Use databento::ToString to convert enums to human-readable strings
    schema = meta.schema.has_value() ? db::ToString(meta.schema.value()) : "Unknown";
    stype_in = meta.stype_in.has_value() ? db::ToString(meta.stype_in.value()) : "Unknown";
    stype_out = db::ToString(meta.stype_out);
    
    // Store raw nanoseconds for the OrderBookProcessor's "Cold Start" logic
    start_ts = meta.start.time_since_epoch().count();
    end_ts = meta.end.time_since_epoch().count();
    
    record_count = meta.limit;
    ts_out = meta.ts_out;
    symbols = meta.symbols;
    partial = meta.partial;
    not_found = meta.not_found;
}

MetadataSummary MetadataParser::GetSummary() const {
    return {version, dataset, schema, stype_in, stype_out,
            start_ts, end_ts, record_count, ts_out,
            symbols, partial, not_found};
}

std::string MetadataSummary::to_string() const {
    std::stringstream ss;
    
    // Formatting helper for a clean, tabular look in your console/logs
    auto row = [&](const std::string& label) -> std::ostream& {
        return ss << std::left << std::setw(20) << label;
    };

    ss << "------------------------ DBN Dataset Summary -------------------------\n";
    row("DBN Version:")   << static_cast<int>(version) << "\n";
    row("Dataset:")       << dataset << "\n";
    row("Schema:")        << schema << "\n";
    row("SType In/Out:")  << stype_in << " -> " << stype_out << "\n";
    row("Start Time:")    << utils::epoch_to_str(start_ts) << " (UTC)\n";
    row("End Time:")      << utils::epoch_to_str(end_ts) << " (UTC)\n";
    row("Duration:")      << std::fixed << std::setprecision(2) 
                          << utils::to_minutes(end_ts - start_ts) << " minutes\n";
    row("TS Out:")        << (ts_out ? "Enabled" : "Disabled") << "\n";
    row("Record Limit:")  << (record_count == 0 ? "None" : std::to_string(record_count)) << "\n";


    // Helper for printing symbol vectors
    auto print_list = [&](const std::string& label, const std::vector<std::string>& list) {
        if (list.empty() && label != "Symbols") return;
        row(label + " [" + std::to_string(list.size()) + "]:");
        for (const auto &sym : list) ss << sym << " ";
        ss << "\n";
    };

    print_list("Symbols", symbols);
    print_list("Partial", partial);
    print_list("Not Found", not_found);
    ss << "-----------------------------------------------------------------------\n";

    return ss.str();
}
