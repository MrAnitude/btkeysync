#include "HiveReader.h"
#include <stdexcept>
#include <cstdlib>


HiveReader::HiveReader(const std::string& file_path) {
  hive_ = hivex_open(file_path.c_str(), 0);
  if (hive_ == nullptr) {
    throw std::runtime_error("could not open " + file_path);
  }
}

HiveReader::~HiveReader() {
  if (hive_) hivex_close(hive_);
}

std::vector<std::uint8_t> HiveReader::readLinkKey(const std::string& adapterMac,
                                                  const std::string& deviceMac) {

  hive_node_h node = hivex_root(hive_);
  const char* path[] = {"ControlSet001", "Services", "BTHPORT",
                        "Parameters", "Keys", adapterMac.c_str()};
  for (const char* seg : path) {
    node = hivex_node_get_child(hive_, node, seg);
    if (node == 0) {
      throw std::runtime_error(std::string("hive path segment not found: ") + seg);
    }
  }

  hive_value_h val = hivex_node_get_value(hive_, node, deviceMac.c_str());
  if (val == 0) {
    throw std::runtime_error("could not find device MAC: " + deviceMac);
  }

  hive_type type;            // what type of data i want (REG_BINARY)
  size_t len = 0;
  char* data = hivex_value_value(hive_, val, &type, &len);
  if (data == nullptr) {
    throw std::runtime_error("could not find link key value");
  }

  std::vector<std::uint8_t> key(data, data + len);
  free(data);
  return key;
}
