#pragma once

#include <string>
#include <optional>

class BlueZInfo {
public:
  explicit BlueZInfo(const std::string& infoPath);

  std::optional<std::string> getLinkKey() const;
  void setLinkKey(const std::string& link_key);
  void save() const;

private:
  std::string path_;
  std::string contents_;
};
