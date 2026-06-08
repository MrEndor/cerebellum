#pragma once
#include <string_view>

#include "common/config.hpp"

namespace cere::common {

Config LoadConfig(std::string_view path);

}
