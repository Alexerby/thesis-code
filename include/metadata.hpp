/**
 * @file metadata.hpp
 * @brief Utilities for parsing and summarizing Databento DBN metadata.
 */

#pragma once

#include <databento/dbn.hpp>
#include <string>
#include <vector>

namespace db = databento;

/**
 * @struct MetadataSummary
 * @brief A plain-old-data structure containing a human-readable summary of DBN
 * metadata.
 */
struct MetadataSummary {
  uint8_t version;       ///< DBN version number
  std::string dataset;   ///< Name of the dataset (e.g., XNAS.ITCH)
  std::string schema;    ///< Data schema (e.g., mbo, mbp-10)
  std::string stype_in;  ///< Input symbol type
  std::string stype_out; ///< Output symbol type
  uint64_t start_ts;     ///< Start timestamp in nanoseconds since epoch
  uint64_t end_ts;       ///< End timestamp in nanoseconds since epoch
  uint64_t record_count; ///< Maximum number of records requested
  bool ts_out;           ///< Whether timestamps are included in output
  std::vector<std::string> symbols;   ///< List of requested symbols
  std::vector<std::string> partial;   ///< Symbols with partial data
  std::vector<std::string> not_found; ///< Symbols not found in the dataset

  /**
   * @brief Generates a formatted string representation of the metadata.
   * @return A multi-line string suitable for console output.
   */
  std::string to_string() const;
};

/**
 * @class MetadataParser
 * @brief Responsible for extracting and validating information from raw
 * Databento metadata.
 */
class MetadataParser {
public:
  /**
   * @brief Constructs a parser from raw Databento metadata.
   * @param meta The metadata object provided by the Databento client/file
   * store.
   */
  explicit MetadataParser(const db::Metadata &meta);

  /**
   * @brief Returns a summary structure of the parsed metadata.
   */
  MetadataSummary GetSummary() const;

  /**
   * @brief Validates that the dataset conforms to the requirements of the
   * framework.
   * @throws std::runtime_error if the schema is not 'mbo'.
   */
  void ValidateSchema() const;

private:
  uint8_t version_;
  std::string dataset_;
  std::string schema_;
  std::string stype_in_;
  std::string stype_out_;
  uint64_t start_ts_;
  uint64_t end_ts_;
  uint64_t record_count_;
  bool ts_out_;
  std::vector<std::string> symbols_;
  std::vector<std::string> partial_;
  std::vector<std::string> not_found_;
};
