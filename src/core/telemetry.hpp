#pragma once
#include <cstdint>
#include <iostream>
#include <map>
#include <string>

class MarketTelemetry {
public:
  void RecordAction(char action) {
    action_counts_[action]++;
    total_processed_++;
  }

  void PrintSummary() const {
    std::cout << "\n" << std::string(30, '=') << "\n";
    std::cout << "  DATA INTEGRITY REPORT\n";
    std::cout << std::string(30, '=') << "\n";
    std::cout << "Total Records: " << total_processed_ << "\n\n";

    for (auto const &[action, count] : action_counts_) {
      std::cout << GetLabel(action) << ": " << count << "\n";
    }
    std::cout << std::string(30, '=') << std::endl;
  }

private:
  uint64_t total_processed_ = 0;
  std::map<char, uint64_t> action_counts_;

  std::string GetLabel(char a) const {
    switch (a) {
    case 'A':
      return "Add    (A)";
    case 'C':
      return "Cancel (C)";
    case 'M':
      return "Modify (M)";
    case 'T':
      return "Trade  (T)";
    case 'F':
      return "Fill   (F)";
    case 'R':
      return "Clear  (R)";
    case 0:
      return "None   (0)";
    default:
      return "Other  (" + std::to_string(static_cast<int>(a)) + ")";
    }
  }
};
