#include "BlueZInfo.h"
#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <filesystem>


BlueZInfo::BlueZInfo(const std::string& infoPath) 
  : path_(infoPath) 
{
  std::ifstream file(path_);
  if(!file) {
    throw std::runtime_error("could not open file Path: " + path_);
  }
  
  std::stringstream buffer;
  buffer << file.rdbuf();
  contents_  = buffer.str();
}


std::optional<std::string> BlueZInfo::getLinkKey() const {
    size_t section_pos = contents_.find("[LinkKey]");
    if (section_pos == std::string::npos) return std::nullopt;

    size_t key_pos = contents_.find("Key=", section_pos);
    if (key_pos == std::string::npos) return std::nullopt;

    size_t value_start = key_pos + 4;                    // skip "Key="
    size_t end_pos = contents_.find("\n", value_start);
    size_t value_len = (end_pos == std::string::npos)
                       ? std::string::npos               // last line, no newline
                       : end_pos - value_start;

    return contents_.substr(value_start, value_len);
}


void BlueZInfo::setLinkKey(const std::string& link_key) {
    size_t section_pos = contents_.find("[LinkKey]");
    if (section_pos == std::string::npos) return;        // or throw

    size_t key_pos = contents_.find("Key=", section_pos);
    if (key_pos == std::string::npos) return;

    size_t value_start = key_pos + 4;
    size_t end_pos = contents_.find("\n", value_start);
    size_t value_len = (end_pos == std::string::npos)
                       ? std::string::npos
                       : end_pos - value_start;

    contents_.replace(value_start, value_len, link_key);  // ← the key fix
}

void BlueZInfo::save() const {
    // 1. Back up the original before touching it.
    std::filesystem::copy_file(
        path_,
        path_ + ".bak",
        std::filesystem::copy_options::overwrite_existing);

    // 2. Open the file for writing (truncates it to empty).
    std::ofstream file(path_);
    if (!file) {
        throw std::runtime_error("could not open info file for writing: " + path_);
    }

    // 3. Write the whole in-memory contents back out.
    file << contents_;
}
