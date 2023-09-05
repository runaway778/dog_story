#pragma once

#include <boost/json.hpp>

#include "model.h"

namespace json_encoder {

namespace json = boost::json;

std::string GameToString(const model::Game& game);

std::string MapToString(const model::Map& map);

json::array RoadsToJson(const model::Map::Roads& roads);

json::array BuildingsToJson(const model::Map::Buildings& buildings);

json::array OfficesToJson(const model::Map::Offices& offices);

std::string PlayerToString(const model::Player& player);

std::string GameSessionToString(const model::GameSession& game_session);

std::string GameStateToString(const model::GameSession& game_session);

json::array BagToJson(const std::vector<std::pair<int, int>>& bag);

}  // namespace json_encoder
