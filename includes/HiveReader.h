#pragma once

#include <hivex.h>
#include <string>
#include <vector>
#include <cstdint>

class HiveReader {
public:
  explicit HiveReader(const std::string& hive_path);
  ~HiveReader();

  HiveReader(const HiveReader&) = delete;
  HiveReader& operator=(const HiveReader&) = delete;

  std::vector<std::uint8_t> readLinkKey(const std::string& adapterMac,
                                        const std::string& deviceMac);
private:
  hive_h* hive_ = nullptr;
};
