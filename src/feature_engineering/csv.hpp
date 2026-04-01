#pragma once

#include <filesystem> // exists, create_directories
#include <fstream>
#include <string>

namespace fs = std::filesystem;

class CSVWriter {
public:
  CSVWriter() = default;

  bool Open(const std::string &filename, const std::string &dir = "features",
            bool append = false) {
    // Ensure the directory exists
    try {
      if (!fs::exists(dir)) {
        fs::create_directories(dir);
      }
    } catch (const fs::filesystem_error &e) {
      return false;
    }

    std::string full_path = dir + "/" + filename;

    // Set the 1MB buffer before opening
    m_file.rdbuf()->pubsetbuf(m_buffer, sizeof(m_buffer));

    auto mode = std::ios::out | std::ios::binary;
    if (append) {
      mode |= std::ios::app;
    } else {
      mode |= std::ios::trunc;
    }

    m_file.open(full_path, mode);

    return m_file.is_open();
  }

  bool Exists(const std::string &filename,
              const std::string &dir = "features") {
    return fs::exists(dir + "/" + filename);
  }

  template <typename T> void Write(const T &data, bool is_last = false) {
    m_file << data << (is_last ? "\n" : ",");
  }

  void Flush() { m_file.flush(); }

private:
  std::ofstream m_file;
  char m_buffer[1024 * 1024];
};
