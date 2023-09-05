#include <fstream>

#include "json_loader.h"

namespace json_loader {

model::Game LoadGame(const std::filesystem::path& json_path) {
    std::ifstream ifs(json_path.string());
    std::string json_str(std::istreambuf_iterator<char>{ifs}, {});
    if (!ifs.good()) {
        throw std::runtime_error("Config file doesn't exists");
    }
    json::value json_game = json::parse(json_str);

    model::Game game = GameFromJson(json_game);

    return game;
}

model::Game GameFromJson(const json::value& json_game) {
    model::Game game;

    try {
        game.game_dog_speed_ = json_game.at("defaultDogSpeed").as_double();
    } catch(...) {
        game.game_dog_speed_ = 1.0;
    }

    try {
        game.game_bag_capacity_ = json_game.at("defaultBagCapacity").as_int64();
    } catch(...) {
        game.game_bag_capacity_ = 3;
    }

    try {
        game.dog_retirement_time = json_game.at("dogRetirementTime").as_double();
    } catch(...) {
        game.dog_retirement_time = 60.0;
    }

    game.period = json_game.at("lootGeneratorConfig").at("period").as_double();
    game.probability = json_game.at("lootGeneratorConfig").at("probability").as_double();

    for (const json::value& json_map : json_game.at("maps").as_array()) {
        game.AddMap(MapFromJson(json_map));
    }

    return game;
}

model::Map MapFromJson(const json::value& json_map) {
    model::Map map(model::Map::Id(json::value_to<std::string>(json_map.at("id"))), json::value_to<std::string>(json_map.at("name")));

    try {
        map.map_dog_speed_ = json_map.at("dogSpeed").as_double();
    } catch(...) {
        map.map_dog_speed_ = -1.0;
    }

    try {
        map.map_bag_capacity_ = json_map.at("bagCapacity").as_int64();
    } catch(...) {
        map.map_bag_capacity_ = -1;
    }

    for (const json::value& json_road : json_map.at("roads").as_array()) {
        map.AddRoad(RoadFromJson(json_road));
    }

    for (const json::value& json_building : json_map.at("buildings").as_array()) {
        map.AddBuilding(BuildingFromJson(json_building));
    }

    for (const json::value& json_office : json_map.at("offices").as_array()) {
        map.AddOffice(OfficeFromJson(json_office));
    }

    map.AddLootTypes(json_map.at("lootTypes").as_array());
    
    return map;
}

model::Road RoadFromJson(const json::value& json_road) {
    model::Point point{static_cast<model::Coord>(json_road.at("x0").as_int64()), static_cast<model::Coord>(json_road.at("y0").as_int64())};
    if (json_road.as_object().contains("x1")) {
        return {model::Road::HORIZONTAL, point, static_cast<model::Coord>(json_road.at("x1").as_int64())};
    }
    return {model::Road::VERTICAL, point, static_cast<model::Coord>(json_road.at("y1").as_int64())};
}

model::Building BuildingFromJson(const json::value& json_building) {
    model::Point point{static_cast<model::Coord>(json_building.at("x").as_int64()), static_cast<model::Coord>(json_building.at("y").as_int64())};
    model::Size size{static_cast<model::Coord>(json_building.at("w").as_int64()), static_cast<model::Coord>(json_building.at("h").as_int64())};
    return model::Building({point, size});
}

model::Office OfficeFromJson(const json::value& json_office) {
    model::Point point{static_cast<model::Coord>(json_office.at("x").as_int64()), static_cast<model::Coord>(json_office.at("y").as_int64())};
    model::Offset offset{static_cast<model::Dimension>(json_office.at("offsetX").as_int64()), static_cast<model::Dimension>(json_office.at("offsetY").as_int64())};
    return {model::Office::Id(json::value_to<std::string>(json_office.at("id"))), point, offset};
}

}  // namespace json_loader
