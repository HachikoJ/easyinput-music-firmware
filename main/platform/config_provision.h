#pragma once

#include <string>

#include "esp_err.h"

namespace easy_input {

// Reads a one-shot configuration envelope from the provision partition.
// The caller validates and persists the JSON before consuming the envelope.
esp_err_t read_config_provision(std::string* json);
esp_err_t consume_config_provision();

}  // namespace easy_input
