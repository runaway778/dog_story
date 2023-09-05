#include "json_encoder.h"

namespace json_encoder {

std::string GameToString(const model::Game& game) {
    json::array json_maps;
    for (const auto& map : game.GetMaps()) {
        json_maps.push_back({
            {"id", *map.GetId()},
            {"name", map.GetName()}
        });
    }

    std::ostringstream res;
    res << json_maps;
    return res.str();
}

std::string MapToString(const model::Map& map) {
    json::array json_roads = RoadsToJson(map.GetRoads());

    json::array json_buildings = BuildingsToJson(map.GetBuildings());

    json::array json_offices = OfficesToJson(map.GetOffices());

    json::object json_map {
        {"id", *map.GetId()},
        {"name", map.GetName()},
        {"roads", json_roads},
        {"buildings", json_buildings},
        {"offices", json_offices},
        {"lootTypes", map.GetLootTypes()}
    };

    std::ostringstream res;
    res << json_map;
    return res.str();
}

json::array RoadsToJson(const model::Map::Roads& roads) {
    json::array json_roads;
    for (const auto& road : roads) {
        json_roads.push_back({
            {"x0", road.GetStart().x},
            {"y0", road.GetStart().y}
        });
        if (road.IsHorizontal()) {
            json_roads.back().as_object()["x1"] = road.GetEnd().x;
        } else {
            json_roads.back().as_object()["y1"] = road.GetEnd().y;
        }
    }
    return json_roads;
}

json::array BuildingsToJson(const model::Map::Buildings& buildings) {
    json::array json_buildings;
    for (const auto& building : buildings) {
        json_buildings.push_back({
            {"x", building.GetBounds().position.x},
            {"y", building.GetBounds().position.y},
            {"w", building.GetBounds().size.width},
            {"h", building.GetBounds().size.height}
        });
    }
    return json_buildings;
}

json::array OfficesToJson(const model::Map::Offices& offices) {
    json::array json_offices;
    for (const auto& office : offices) {
        json_offices.push_back({
            {"id", *office.GetId()},
            {"x", office.GetPosition().x},
            {"y", office.GetPosition().y},
            {"offsetX", office.GetOffset().dx},
            {"offsetY", office.GetOffset().dy}
        });
    }
    return json_offices;
}

std::string PlayerToString(const model::Player& player) {
    json::value json_player {
        {"authToken", *player.GetToken()},
        {"playerId", *player.GetDog()->GetId()}
    };
    std::ostringstream res;
    res << json_player;
    return res.str();
}

std::string GameSessionToString(const model::GameSession& game_session) {
    json::object json_game_session;
    for (const auto& [id, dog] : game_session.GetDogs()) {
        json_game_session[std::to_string(*id)] = json::object({{"name", dog->GetName()}});
    }
    std::ostringstream res;
    res << json_game_session;
    return res.str();
}

std::string GameStateToString(const model::GameSession& game_session) {
    json::object json_players;
    for (const auto& [id, dog] : game_session.GetDogs()) {
        json_players[std::to_string(*id)] = json::object({
            {"pos", json::array({dog->x, dog->y})},
            {"speed", json::array({dog->dx, dog->dy})},
            {"dir", dog->dir},
            {"bag", BagToJson(dog->bag_)},
            {"score", dog->score}
        });
    }
    json::object json_lost_objects;
    for (const auto& [id, item] : game_session.GetLostObjects()) {
        json_lost_objects[std::to_string(id)] = json::object({
            {"type", item.type},
            {"pos", json::array({item.x, item.y})}
        });
    }
    std::ostringstream res;
    res << json::object({
        {"players", json_players},
        {"lostObjects", json_lost_objects}
    });
    return res.str();
}

json::array BagToJson(const std::vector<std::pair<int, int>>& bag) {
    json::array json_bag;
    for (const auto& item : bag) {
        json_bag.push_back({
            {"id", item.first},
            {"type", item.second}
        });
    }
    return json_bag;
}

}  // namespace json_encoder
