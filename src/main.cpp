#include "databento/dbn_file_store.hpp"
#include "metadata_handler.hpp"
#include "orderbook_processor.hpp"
#include "string"

int main() {
    const std::string file_path = "./data/fomc.dbn.zst";
    db::DbnFileStore store{file_path};

    // Metadata
    MetadataParser mdParser(store);
    auto summary = mdParser.GetSummary();
    std::cout << summary.to_string() << std::endl;

    // Orderbook processing
    OrderBookProcessor obp;
    auto enriched_orders = obp.ProcessRecords(store);
}
