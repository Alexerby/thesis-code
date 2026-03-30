/**
 * @file downloader.cpp
 * @brief Utility for downloading historical market data from Databento.
 * 
 * This module uses the Databento Historical API to fetch MBO data for a specified
 * range of symbols and time, performing cost analysis and safety checks before 
 * execution to avoid unexpected billing.
 */

#include <databento/enums.hpp>
#include <databento/historical.hpp>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

// --- Configuration Constants ---
const std::string DATASET = "XNAS.ITCH";
const std::vector<std::string> SYMBOLS = {"AAPL", "MSFT", "AMZN", "NVDA", "GOOGL"};
const std::string START_TIME = "2026-03-18T00:00:00Z";
const std::string END_TIME = "2026-03-19T00:00:00Z";
const std::string OUTPUT_PATH = "./data/multi_instrument.dbn.zst";

/**
 * @brief Hard limit to abort automatically if estimated cost exceeds this value.
 */
const double MAX_COST_USD = 5.00;

// ANSI Colors for console output
const std::string RED = "\033[1;31m";
const std::string GRN = "\033[1;32m";
const std::string YEL = "\033[1;33m";
const std::string CYN = "\033[1;36m";
const std::string BOLD = "\033[1m";
const std::string RESET = "\033[0m";
// -------------------------------

int main(int argc, char *argv[]) {
  std::string api_key;
  
  // API Key resolution (Arg > Env)
  if (argc >= 2) {
    api_key = argv[1];
  } else {
    const char *env_key = std::getenv("DATABENTO_API_KEY");
    if (env_key) {
      api_key = env_key;
    } else {
      std::cerr << RED << "Error: No API key provided." << RESET << std::endl;
      std::cerr << "Usage: " << argv[0] << " <API_KEY>" << std::endl;
      return 1;
    }
  }

  try {
    // Initialize Historical Client
    databento::Historical client =
        databento::Historical::Builder()
            .SetKey(api_key)
            .SetGateway(databento::HistoricalGateway::Bo1)
            .Build();

    std::cout << BOLD << CYN << "\n--- Databento Downloader ---" << RESET
              << std::endl;
    std::cout << "Dataset: " << DATASET << std::endl;
    std::cout << "Symbols: ";
    for (size_t i = 0; i < SYMBOLS.size(); ++i) {
      std::cout << SYMBOLS[i] << (i == SYMBOLS.size() - 1 ? "" : ", ");
    }
    std::cout << "\nRange:   " << START_TIME << " to " << END_TIME << std::endl;

    // Pre-fetch Cost and Size Analysis
    std::cout << "\nChecking cost and billable size..." << std::endl;

    databento::DateTimeRange<std::string> range{START_TIME, END_TIME};

    double estimated_cost =
        client.MetadataGetCost(DATASET, range, SYMBOLS, databento::Schema::Mbo,
                               databento::SType::RawSymbol,
                               0 // limit
        );

    uint64_t billable_size = client.MetadataGetBillableSize(
        DATASET, range, SYMBOLS, databento::Schema::Mbo,
        databento::SType::RawSymbol,
        0 // limit
    );

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n" << BOLD << "Estimated Cost:  " << GRN << "$" << estimated_cost << " USD" << RESET << std::endl;
    std::cout << BOLD << "Billable Size:   " << (billable_size / (1024.0 * 1024.0)) << " MB (uncompressed)" << RESET << std::endl;

    // Safety Boundary Check
    if (estimated_cost > MAX_COST_USD) {
      std::cerr << "\n"
                << RED << BOLD << "[ABORTED] Estimated cost ($"
                << estimated_cost << ") exceeds your maximum limit of $"
                << MAX_COST_USD << "." << RESET << std::endl;
      return 2;
    }

    // User Confirmation
    std::cout << "\n"
              << RED << BOLD
              << "WARN: This operation will incur real-world costs on your "
                 "Databento account."
              << RESET << std::endl;
    std::cout << "Do you want to proceed with the download? [y/N]: ";
    std::string response;
    std::getline(std::cin, response);

    if (response != "y" && response != "Y") {
      std::cout << "Download aborted by user." << std::endl;
      return 0;
    }

    // Ensure data directory exists
    fs::path out_path(OUTPUT_PATH);
    if (out_path.has_parent_path()) {
        std::error_code ec;
        if (!fs::create_directories(out_path.parent_path(), ec) && ec) {
            std::cerr << RED << "Error creating directory: " << ec.message() << RESET << std::endl;
            return 3;
        }
    }

    // Fetch data and stream to file
    std::cout << "\nDownloading and saving to " << OUTPUT_PATH << "..."
              << std::endl;

    client.TimeseriesGetRangeToFile(
        DATASET, range, SYMBOLS, databento::Schema::Mbo,
        databento::SType::RawSymbol, databento::SType::InstrumentId,
        0, // limit
        OUTPUT_PATH);

    std::cout << "\n"
              << GRN << BOLD << "Success! Multi-instrument data saved to "
              << OUTPUT_PATH << RESET << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "\n"
              << RED << BOLD << "Fatal Error: " << e.what() << RESET
              << std::endl;
    return 1;
  }

  return 0;
}
