#pragma once

#include <filesystem>
#include <boost/json.hpp>

#include "model.h"

namespace json_loader {

namespace json = boost::json;

model::Game LoadGame(const std::filesystem::path& json_path);

model::Game GameFromJson(const json::value& json_game);

model::Map MapFromJson(const json::value& json_map);

model::Road RoadFromJson(const json::value& json_road);

model::Building BuildingFromJson(const json::value& json_building);

model::Office OfficeFromJson(const json::value& json_office);

}  // namespace json_loader
