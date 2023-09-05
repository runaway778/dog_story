#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <map>
#include <sstream>
#include <atomic>
#include <random>
#include <sstream>
#include <set>
#include <chrono>
#include <fstream>
#include <filesystem>

#include <boost/json.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/signals2.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <pqxx/pqxx>
#include <iostream>

#include "tagged.h"
#include "loot_generator.h"
#include "collision_detector.h"
#include "tagged_uuid.h"

namespace model {

using Dimension = int;
using Coord = Dimension;

namespace json = boost::json;
namespace sig = boost::signals2;
using milliseconds = std::chrono::milliseconds;
using namespace std::literals;
using OutputArchive = boost::archive::text_oarchive;

struct Point {
    Coord x, y;
};

struct Size {
    Dimension width, height;
};

struct Rectangle {
    Point position;
    Size size;
};

struct Offset {
    Dimension dx, dy;
};

struct Loot {
    int type;
    double x;
    double y;

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& type;
        ar& x;
        ar& y;
    }
};

class Road {
    struct HorizontalTag {
        HorizontalTag() = default;
    };

    struct VerticalTag {
        VerticalTag() = default;
    };

public:
    constexpr static HorizontalTag HORIZONTAL{};
    constexpr static VerticalTag VERTICAL{};

    Road(HorizontalTag, Point start, Coord end_x) noexcept
        : start_{start}
        , end_{end_x, start.y} {
    }

    Road(VerticalTag, Point start, Coord end_y) noexcept
        : start_{start}
        , end_{start.x, end_y} {
    }

    bool IsHorizontal() const noexcept {
        return start_.y == end_.y;
    }

    bool IsVertical() const noexcept {
        return start_.x == end_.x;
    }

    Point GetStart() const noexcept {
        return start_;
    }

    Point GetEnd() const noexcept {
        return end_;
    }

private:
    Point start_;
    Point end_;
};

class Building {
public:
    explicit Building(Rectangle bounds) noexcept
        : bounds_{bounds} {
    }

    const Rectangle& GetBounds() const noexcept {
        return bounds_;
    }

private:
    Rectangle bounds_;
};

class Office {
public:
    using Id = util::Tagged<std::string, Office>;

    Office(Id id, Point position, Offset offset) noexcept
        : id_{std::move(id)}
        , position_{position}
        , offset_{offset} {
    }

    const Id& GetId() const noexcept {
        return id_;
    }

    Point GetPosition() const noexcept {
        return position_;
    }

    Offset GetOffset() const noexcept {
        return offset_;
    }

private:
    Id id_;
    Point position_;
    Offset offset_;
};

class Map {
public:
    using Id = util::Tagged<std::string, Map>;
    using Roads = std::vector<Road>;
    using Buildings = std::vector<Building>;
    using Offices = std::vector<Office>;

    Map(Id id, std::string name) noexcept
        : id_(std::move(id))
        , name_(std::move(name)) {
    }

    const Id& GetId() const noexcept {
        return id_;
    }

    const std::string& GetName() const noexcept {
        return name_;
    }

    const Buildings& GetBuildings() const noexcept {
        return buildings_;
    }

    const Roads& GetRoads() const noexcept {
        return roads_;
    }

    const Offices& GetOffices() const noexcept {
        return offices_;
    }

    const json::array& GetLootTypes() const noexcept {
        return loot_types_;
    }

    void AddRoad(const Road& road) {
        roads_.emplace_back(road);
    }

    void AddBuilding(const Building& building) {
        buildings_.emplace_back(building);
    }

    void AddOffice(const Office& office);

    void AddLootTypes(const json::array& loot_types) {
        loot_types_ = loot_types;
    }

    double map_dog_speed_;
    int map_bag_capacity_;
private:
    using OfficeIdToIndex = std::unordered_map<Office::Id, size_t, util::TaggedHasher<Office::Id>>;

    Id id_;
    std::string name_;
    Roads roads_;
    Buildings buildings_;
    json::array loot_types_;

    OfficeIdToIndex warehouse_id_to_index_;
    Offices offices_;
};

class Dog {
public:
    using Id = util::Tagged<std::uint64_t, Dog>;
    using UUID = util::TaggedUUID<Dog>;

    Dog(std::string name) noexcept
        : id_{++next_dog_id_}
        , name_{name} {
    }

    Dog() {}

    const Id& GetId() const noexcept {
        return id_;
    }

    const std::string& GetName() const noexcept {
        return name_;
    }

    void ChangeDirection(std::string move) {
        if (move == "U") {
            dx = 0.0;
            dy = -s;
            dir = move;
        }
        if (move == "R") {
            dx = s;
            dy = 0.0;
            dir = move;
        }
        if (move == "D") {
            dx = 0.0;
            dy = s;
            dir = move;
        }
        if (move == "L") {
            dx = -s;
            dy = 0.0;
            dir = move;
        }
        if (move == "") {
            dx = 0.0;
            dy = 0.0;
        }
    }

    void move(double x_delta, double y_delta) {
        x += x_delta;
        y += y_delta;
    }

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& x;
        ar& y;
        ar& dx;
        ar& dy;
        ar& dir;
        ar& s;
        ar& cap;
        ar& bag_;
        ar& score;
        ar& next_dog_id_;
        ar& id_;
        ar& name_;
        ar& time_standing;
        ar& time_playing;
    }

    double x = 0.0;
    double y = 0.0;
    double dx = 0.0;
    double dy = 0.0;
    std::string dir = "U";
    double s;
    int cap;
    std::vector<std::pair<int, int>> bag_;
    int score = 0;
    int time_standing = 0.0;
    int time_playing = 0.0;
    bool already_stopped = false;
private:
    static std::uint64_t next_dog_id_;
    Id id_;
    std::string name_;
};

void DeletePlayer(const Dog::Id& dog_id, const Map::Id& map_id);

class GameSession {
public:
    GameSession(const Map* map, double game_dog_speed, int game_bag_capacity, double dog_retirement_time, std::string db_url)
        : map_{map}
        , dog_speed_{map->map_dog_speed_ < 0 ? game_dog_speed : map->map_dog_speed_}
        , bag_capacity_{map->map_bag_capacity_ < 0 ? game_bag_capacity : map->map_bag_capacity_}
        , dog_retirement_time_{dog_retirement_time}
        , db_url_{db_url} {
    }

    GameSession() {}

    const Map::Id& GetMapId() const {
        return map_->GetId();
    }

    const std::map<Dog::Id, std::shared_ptr<Dog>>& GetDogs() const {
        return dogs_;
    }

    const std::map<int, Loot>& GetLostObjects() const {
        return lost_objects_;
    }

    void AddDog(std::shared_ptr<Dog> dog, bool randomize_spawn_points) {
        dogs_[dog->GetId()] = dog;
        if (randomize_spawn_points) {
            auto position = GenerateRandomPosition();
            dog->x = position.first;
            dog->y = position.second;
        } else {
            dog->x = map_->GetRoads()[0].GetStart().x;
            dog->y = map_->GetRoads()[0].GetStart().y;
        }
        dog->s = dog_speed_;
        dog->cap = bag_capacity_;
    }

    void Tick(int time_delta, loot_gen::LootGenerator& loot_generator) {
        std::vector<collision_detector::Gatherer> gatherers;
        std::vector<int> gatherer_to_dog;
        for (auto& [id, dog] : dogs_) {
            gatherers.push_back({{dog->x, dog->y}, {0, 0}, DOG_WIDTH / 2});
            gatherer_to_dog.push_back(*dog->GetId());
            double minimum_x = 1e9;
            double maximum_x = -1e9;
            double minimum_y = 1e9;
            double maximum_y = -1e9;
            for (const auto& road : map_->GetRoads()) {
                double min_x = std::min(road.GetStart().x, road.GetEnd().x) - ROAD_WIDTH / 2;
                double max_x = std::max(road.GetStart().x, road.GetEnd().x) + ROAD_WIDTH / 2;
                double min_y = std::min(road.GetStart().y, road.GetEnd().y) - ROAD_WIDTH / 2;
                double max_y = std::max(road.GetStart().y, road.GetEnd().y) + ROAD_WIDTH / 2;
                if (min_x <= dog->x && dog->x <= max_x && min_y <= dog->y && dog->y <= max_y) {
                    minimum_x = std::min(minimum_x, min_x);
                    maximum_x = std::max(maximum_x, max_x);
                    minimum_y = std::min(minimum_y, min_y);
                    maximum_y = std::max(maximum_y, max_y);
                }
            }
            dog->x += dog->dx * time_delta / MILLISECONDS_IN_SECOND;
            dog->y += dog->dy * time_delta / MILLISECONDS_IN_SECOND;
            if (dog->x < minimum_x) {
                dog->time_standing = (minimum_x - dog->x) / std::abs(dog->dx) * MILLISECONDS_IN_SECOND;
                dog->already_stopped = true;
                dog->x = minimum_x;
                dog->dx = 0.0;
                dog->dy = 0.0;
            }
            if (dog->x > maximum_x) {
                dog->time_standing = (dog->x - maximum_x) / std::abs(dog->dx) * MILLISECONDS_IN_SECOND;
                dog->already_stopped = true;
                dog->x = maximum_x;
                dog->dx = 0.0;
                dog->dy = 0.0;
            }
            if (dog->y < minimum_y) {
                dog->time_standing = (minimum_y - dog->y) / std::abs(dog->dy) * MILLISECONDS_IN_SECOND;
                dog->already_stopped = true;
                dog->y = minimum_y;
                dog->dx = 0.0;
                dog->dy = 0.0;
            }
            if (dog->y > maximum_y) {
                dog->time_standing = (dog->y - maximum_y) / std::abs(dog->dy) * MILLISECONDS_IN_SECOND;
                dog->already_stopped = true;
                dog->y = maximum_y;
                dog->dx = 0.0;
                dog->dy = 0.0;
            }
            gatherers.back().end_pos = {dog->x, dog->y};
        }
        std::vector<collision_detector::Item> items;
        std::vector<int> item_to_object;
        for (const auto& [id, item] : lost_objects_) {
            items.push_back({{item.x, item.y}, ITEM_WIDTH / 2});
            item_to_object.push_back(id);
        }
        int item_count = items.size();
        for (const auto& office : map_->GetOffices()) {
            items.push_back({{static_cast<double>(office.GetPosition().x), static_cast<double>(office.GetPosition().y)}, OFFICE_WIDTH / 2});
        }
        collision_detector::VectorItemGathererProvider provider{items, gatherers};
        std::vector<collision_detector::GatheringEvent> events = collision_detector::FindGatherEvents(provider);
        for (const auto& event : events) {
            auto& dog = dogs_[Dog::Id(gatherer_to_dog[event.gatherer_id])];
            if (event.item_id < item_count) {
                if (dog->bag_.size() < dog->cap && lost_objects_.count(item_to_object[event.item_id])) {
                    dog->bag_.push_back({item_to_object[event.item_id], lost_objects_[item_to_object[event.item_id]].type});
                    lost_objects_.erase(item_to_object[event.item_id]);
                }
            } else {
                for (const auto& [id, type] : dog->bag_) {
                    dog->score += map_->GetLootTypes()[type].at("value").as_int64();
                }
                dog->bag_.clear();
            }
        }
        std::vector<Dog::Id> dogs_to_remove;
        for (auto& [id, dog] : dogs_) {
            if (dog->already_stopped) {
                dog->time_playing += time_delta;
                dog->already_stopped = false;
                continue;
            }
            if (dog->dx == 0 && dog->dy == 0) {
                if ((dog->time_standing + time_delta) / MILLISECONDS_IN_SECOND >= dog_retirement_time_) {
                    dog->time_playing += dog_retirement_time_ * MILLISECONDS_IN_SECOND - dog->time_standing;
                    pqxx::connection conn{db_url_};
                    pqxx::work work{conn};
                    work.exec_params(R"(
                        INSERT INTO retired_players (id, name, score, play_time_ms) VALUES (gen_random_uuid(), $1, $2, $3);
                    )",
                        dog->GetName(),
                        dog->score,
                        dog->time_playing);
                    work.commit();
                    dogs_to_remove.push_back(id);
                } else {
                    dog->time_standing += time_delta;
                    dog->time_playing += time_delta;
                }
            } else {
                dog->time_standing = 0.0;
                dog->time_playing += time_delta;
            }
        }
        for (const auto& id : dogs_to_remove) {
            DeletePlayer(id, map_->GetId());
            dogs_.erase(id);
        }
        int n = loot_generator.Generate(std::chrono::duration_cast<loot_gen::LootGenerator::TimeInterval>(std::chrono::duration<double>{time_delta}), lost_objects_.size(), dogs_.size());
        while (n--) {
            int type = GenerateRandomLootType();
            auto position = GenerateRandomPosition();
            lost_objects_[next_loot_id_++] = {type, position.first, position.second};
        }
    }

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& dog_speed_;
        ar& bag_capacity_;
        ar& dogs_;
        ar& lost_objects_;
        ar& next_loot_id_;
        ar& dog_retirement_time_;
        ar& db_url_;
    }

    double dog_speed_;
    int bag_capacity_;
    double dog_retirement_time_;
    std::string db_url_;
private:
    std::pair<double, double> GenerateRandomPosition() {
        static std::random_device random_device_;
        static std::mt19937_64 generator_{[] {
            std::uniform_int_distribution<std::mt19937_64::result_type> dist;
            return dist(random_device_);
        }()};
        const Road& road = map_->GetRoads()[generator_() % map_->GetRoads().size()];
        double min_x = std::min(road.GetStart().x, road.GetEnd().x) - ROAD_WIDTH / 2;
        double max_x = std::max(road.GetStart().x, road.GetEnd().x) + ROAD_WIDTH / 2;
        double min_y = std::min(road.GetStart().y, road.GetEnd().y) - ROAD_WIDTH / 2;
        double max_y = std::max(road.GetStart().y, road.GetEnd().y) + ROAD_WIDTH / 2;
        static std::default_random_engine re;
        std::uniform_real_distribution<double> unif_x(min_x,max_x);
        double x = unif_x(re);
        std::uniform_real_distribution<double> unif_y(min_y,max_y);
        double y = unif_y(re);
        return {x, y};
    }

    int GenerateRandomLootType() {
        static std::random_device random_device_;
        static std::mt19937_64 generator_{[] {
            std::uniform_int_distribution<std::mt19937_64::result_type> dist;
            return dist(random_device_);
        }()};
        return generator_() % map_->GetLootTypes().size();
    }

public:
    std::map<Dog::Id, std::shared_ptr<Dog>> dogs_;
    const Map* map_;
private:
    std::map<int, Loot> lost_objects_;

    int next_loot_id_ = 0;
    
    constexpr static int MILLISECONDS_IN_SECOND = 1000;
    constexpr static double ROAD_WIDTH = 0.8;
    constexpr static double ITEM_WIDTH = 0.0;
    constexpr static double DOG_WIDTH = 0.6;
    constexpr static double OFFICE_WIDTH = 0.5;
};

namespace detail {
    struct TokenTag {};
}  // namespace detail

class Player {
public:
    using Token = util::Tagged<std::string, detail::TokenTag>;

    Player(const std::string& name, const Token& token)
        : dog_{std::make_shared<Dog>(name)}
        , token_{token} {
    }

    Player() {}

    std::shared_ptr<model::Dog> GetDog() {
        return dog_;
    }

    const std::shared_ptr<model::Dog> GetDog() const {
        return dog_;
    }

    const Token& GetToken() const {
        return token_;
    }

    std::shared_ptr<GameSession>& GetSession() {
        return session_;
    }

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& map_id_;
        ar& dog_;
        ar& token_;
    }

    Map::Id map_id_;
private:
    std::shared_ptr<Dog> dog_;
    Token token_;
    std::shared_ptr<GameSession> session_ = nullptr;
};

class Players {
public:
    static bool ContainsToken(const Player::Token& token) {
        return token_to_player_.contains(token);
    }

    static std::shared_ptr<Player> FindByToken(const Player::Token& token) {
        return token_to_player_.at(token);
    }

    static std::shared_ptr<Player> FindByDogIdAndMapId(const Dog::Id dog_id, const Map::Id& map_id) {
        for (const auto& [token, player] : token_to_player_) {
            if (player->GetDog()->GetId() == dog_id && player->map_id_ == map_id) {
                return player;
            }
        }
        return nullptr;
    }

    static std::shared_ptr<Player> CreatePlayer(const std::string& name) {
        Player::Token token = GenerateToken();
        *token += std::string(32 - (*token).size(), '0');
        token_to_player_[token] =  std::make_shared<Player>(name, token);
        return token_to_player_[token];
        // maybe change to one line return
    }

    static void EraseByToken(const Player::Token& token) {
        token_to_player_.erase(token);
    }

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& token_to_player_;
    }

private:
    static Player::Token GenerateToken() {
        static std::random_device random_device_;
        static std::mt19937_64 generator1_{[] {
            std::uniform_int_distribution<std::mt19937_64::result_type> dist;
            return dist(random_device_);
        }()};
        static std::mt19937_64 generator2_{[] {
            std::uniform_int_distribution<std::mt19937_64::result_type> dist;
            return dist(random_device_);
        }()};
        std::ostringstream oss;
        oss << std::hex << generator1_() << generator2_();
        return Player::Token{oss.str()};
    }

public:
    static std::map<Player::Token, std::shared_ptr<Player>> token_to_player_;
};

class Application {
public:
    using TickSignal = sig::signal<void(milliseconds delta)>;

    [[nodiscard]] sig::connection DoOnTick(const TickSignal::slot_type& handler) {
        return tick_signal_.connect(handler);
    }

    void Tick(milliseconds delta) {
        tick_signal_(delta);
    }
private:
    TickSignal tick_signal_;
};

class Game {
public:
    using Maps = std::vector<Map>;

    void AddMap(const Map& map);

    const Maps& GetMaps() const noexcept {
        return maps_;
    }

    const Map* FindMap(const Map::Id& id) const noexcept {
        if (auto it = map_id_to_index_.find(id); it != map_id_to_index_.end()) {
            return &maps_.at(it->second);
        }
        return nullptr;
    }

    void JoinMap(const Map* map, std::shared_ptr<Player> player) {
        for (const auto& game_session : game_sessions_on_map_[map->GetId()]) {
            if (game_session->GetDogs().size() < MAX_DOGS_IN_SESSION) {
                return JoinSession(game_session, player);
            }
        }
        game_sessions_on_map_[map->GetId()].emplace_back(std::make_shared<GameSession>(map, game_dog_speed_, game_bag_capacity_, dog_retirement_time, db_url));
        JoinSession(game_sessions_on_map_[map->GetId()].back(), player);
    }

    void Tick(int time_delta, loot_gen::LootGenerator& loot_generator) {
        static Application app;
        static sig::scoped_connection conn = app.DoOnTick([this, sum = 0ms](milliseconds delta) mutable {
            sum += delta;
            if (sum >= milliseconds(this->save_state_period)) {
                //std::ofstream out(this->state_file);
                std::ofstream out(this->state_file + "_temp.txt");
                OutputArchive output_archive{out};
                model::Players players;
                output_archive << players;
                output_archive << this;
                std::filesystem::rename(this->state_file + "_temp.txt", this->state_file);
                sum -= milliseconds(this->save_state_period);
            }
        });
        for (auto& [map_id, game_sessions] : game_sessions_on_map_) {
            for (auto& game_session : game_sessions) {
                game_session->Tick(time_delta, loot_generator);
            }
        }
        if (contains_state_file && contains_save_state_period) {
            app.Tick(std::chrono::duration_cast<milliseconds>(std::chrono::duration<double>{time_delta}));
        }
    }

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& game_sessions_on_map_;
    }

    double game_dog_speed_;
    int game_bag_capacity_;
    bool randomize_spawn_points = false;
    double period;
    double probability;
    bool contains_state_file = false;
    std::string state_file;
    bool contains_save_state_period = false;
    int save_state_period;
    double dog_retirement_time;
    std::string db_url;
private:
    void JoinSession(std::shared_ptr<GameSession> game_session, std::shared_ptr<Player> player) {
        game_session->AddDog(player->GetDog(), randomize_spawn_points);
        player->GetSession() = game_session;
        player->map_id_ = game_session->GetMapId();
    }

    using MapIdHasher = util::TaggedHasher<Map::Id>;
    using MapIdToIndex = std::unordered_map<Map::Id, size_t, MapIdHasher>;

    std::vector<Map> maps_;
    MapIdToIndex map_id_to_index_;

public:
    std::map<Map::Id, std::vector<std::shared_ptr<GameSession>>> game_sessions_on_map_;

private:
    constexpr static int MAX_DOGS_IN_SESSION = 10;
};

}  // namespace model
