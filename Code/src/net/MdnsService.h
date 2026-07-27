#pragma once

#include <string>

namespace MdnsService {

// Announces <hostname>.local with an _http._tcp service on port 80.
void begin(const std::string& hostname);

}  // namespace MdnsService
