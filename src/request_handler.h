#pragma once

#include <filesystem>
#include <iostream>
#include <variant>
#include <optional>
#include <string>

#include <boost/asio/dispatch.hpp>
#include <boost/json.hpp>

#include "http_server.h"
#include "model.h"
#include "json_encoder.h"
#include "loot_generator.h"

namespace http_handler {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace fs = std::filesystem;
namespace json = boost::json;
using namespace std::literals;

using StringRequest = http::request<http::string_body>;
using StringResponse = http::response<http::string_body>;
using FileResponse = http::response<http::file_body>;

struct ContentType {
    ContentType() = delete;
    constexpr static std::string_view TEXT_HTML = "text/html"sv;
    constexpr static std::string_view TEXT_PLAIN = "text/plain"sv;
    constexpr static std::string_view JSON = "application/json"sv;
    constexpr static std::string_view CSS = "text/css"sv;
    constexpr static std::string_view JAVASCRIPT = "text/javascript"sv;
    constexpr static std::string_view XML = "application/xml"sv;
    constexpr static std::string_view PNG = "image/png"sv;
    constexpr static std::string_view JPG = "image/jpg"sv;
    constexpr static std::string_view GIF = "image/gif"sv;
    constexpr static std::string_view BMP = "image/bmp"sv;
    constexpr static std::string_view ICO = "image/vnd.microsoft.icon"sv;
    constexpr static std::string_view TIFF = "image/tif"sv;
    constexpr static std::string_view SVG_XML = "image/svg+xml"sv;
    constexpr static std::string_view MP3 = "audio/mpeg"sv;
    constexpr static std::string_view OCTET_STREAM = "application/octet-stream"sv;
};

struct ApiPath {
    ApiPath() = delete;
    constexpr static std::string_view MAPS = "/api/v1/maps"sv;
    constexpr static std::string_view JOIN = "/api/v1/game/join"sv;
    constexpr static std::string_view PLAYERS = "/api/v1/game/players"sv;
    constexpr static std::string_view STATE = "/api/v1/game/state"sv;
    constexpr static std::string_view ACTION = "/api/v1/game/player/action"sv;
    constexpr static std::string_view TICK = "/api/v1/game/tick"sv;
    constexpr static std::string_view RECORDS = "/api/v1/game/records"sv;
};

struct Response {
    Response() = delete;
    constexpr static std::string_view INVALID_CONTENT_TYPE = R"({"code": "invalidArgument", "message": "Invalid content type"})"sv;
    constexpr static std::string_view INVALID_NAME = R"({"code": "invalidArgument", "message": "Invalid name"})"sv;
    constexpr static std::string_view JOIN_GAME_REQUEST_PARSE_ERROR = R"({"code": "invalidArgument", "message": "Join game request parse error"})"sv;
    constexpr static std::string_view ACTION_REQUEST_PARSE_ERROR = R"({"code": "invalidArgument", "message": "Failed to parse action"})"sv;
    constexpr static std::string_view TICK_REQUEST_PARSE_ERROR = R"({"code": "invalidArgument", "message": "Failed to parse tick request JSON"})"sv;
    constexpr static std::string_view BAD_REQUEST = R"({"code": "badRequest", "message": "Bad request"})"sv;
    constexpr static std::string_view AUTHORIZATION_HEADER_MISSING = R"({"code": "invalidToken", "message": "Authorization header is missing"})"sv;
    constexpr static std::string_view PLAYER_TOKEN_NOT_FOUND = R"({"code": "unknownToken", "message": "Player token has not been found"})"sv;
    constexpr static std::string_view MAP_NOT_FOUND = R"({"code": "mapNotFound", "message": "Map not found"})"sv;
    constexpr static std::string_view INVALID_METHOD = R"({"code": "invalidMethod", "message": "Invalid method"})"sv;
};

std::string UrlDecode(std::string_view url);

bool IsSubPath(const fs::path& path, const fs::path& base);

std::string GetFileExtension(const fs::path& path);

std::string_view GetContentType(std::string ext);

template <typename Response, typename Body>
Response MakeResponse(http::status status, Body&& body, unsigned http_version,
                      bool keep_alive, std::string_view content_type) {
    Response response(status, http_version);
    response.set(http::field::content_type, content_type);
    response.keep_alive(keep_alive);
    response.body() = std::move(body);
    response.prepare_payload();
    return response;                        
}

class RequestHandler {
public:
    using Strand = net::strand<net::io_context::executor_type>;

    explicit RequestHandler(model::Game& game, fs::path base_path, Strand api_strand, bool is_ticking, loot_gen::LootGenerator& loot_generator, std::string db_url)
        : game_{game}
        , base_path_{fs::weakly_canonical(base_path)} 
        , api_strand_{api_strand}
        , is_ticking_{is_ticking}
        , loot_generator_{loot_generator}
        , db_url_{db_url} {
    }

    RequestHandler(const RequestHandler&) = delete;
    RequestHandler& operator=(const RequestHandler&) = delete;

    template <typename Body, typename Allocator, typename Send>
    void operator()(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        if (req.target().rfind("/api/"sv, 0) == 0) {
            return net::dispatch(api_strand_,
                    [this, send, req = std::forward<decltype(req)>(req)] {
                        try {
                            return send(this->HandleApiRequest(req));
                        } catch(...) {
                            return send(this->ReportServerError(req));
                        }
                    });
        }
        return std::visit(
                [&send](auto&& result) {
                    send(std::forward<decltype(result)>(result));
                },
                HandleFileRequest(req));
    }

private:
    using FileRequestResult = std::variant<FileResponse, StringResponse>;

    StringResponse HandleApiRequest(const StringRequest& req) {
        const auto json_response = [&req](http::status status, std::string_view text) {
            return MakeResponse<StringResponse>(status, text, req.version(), req.keep_alive(), ContentType::JSON);
        };
        const auto api_response = [&json_response, &req](http::status status, std::string_view text) {
            auto res = json_response(status, text);
            res.set(http::field::cache_control, "no-cache");
            return res;
        };
        const auto try_extract_token = [](const StringRequest& req) -> std::optional<std::string> {
            try {
                std::string authorization_token = std::string(req["Authorization"]).substr(7);
                if (req["Authorization"].substr(0, 6) != "Bearer"sv || req["Authorization"].size() != "Bearer "s.size() + 32) {
                    return std::nullopt;
                }
                return authorization_token;
            } catch(...) {
                return std::nullopt;
            }
        };
        const auto try_get_player_by_token = [](const std::string& token) -> std::optional<std::shared_ptr<model::Player>> {
            if (model::Players::ContainsToken(model::Player::Token{token})) {
                return model::Players::FindByToken(model::Player::Token{token});
            }
            return std::nullopt;
        };
        const auto assert_content_type_is_json = [](const StringRequest& req) {
            try {
                return req["Content-Type"] == ContentType::JSON;
            } catch(...) {
                return false;
            }
        };
        std::string target = std::string(req.target());
        if (target.back() == '/') {
            target.pop_back();
        }
        if (target == ApiPath::MAPS) {
            if (req.method_string() == "GET"sv || req.method_string() == "HEAD"sv) {
                return api_response(http::status::ok, json_encoder::GameToString(game_));
            }
            auto res = api_response(http::status::method_not_allowed, Response::INVALID_METHOD);
            res.set(http::field::allow, "GET, HEAD");
            return res;
        }
        if (target.rfind(ApiPath::MAPS, 0) == 0) {
            if (req.method_string() == "GET"sv || req.method_string() == "HEAD"sv) {
                std::string id = std::string(target).substr(ApiPath::MAPS.size() + "/"s.size());
                const model::Map* map = game_.FindMap(model::Map::Id(id));
                if (!map) {
                    return api_response(http::status::not_found, Response::MAP_NOT_FOUND);
                }
                return api_response(http::status::ok, json_encoder::MapToString(*map));
            }
            auto res = api_response(http::status::method_not_allowed, Response::INVALID_METHOD);
            res.set(http::field::allow, "GET, HEAD");
            return res;
        }
        if (target == ApiPath::JOIN) {
            if (req.method_string() == "POST"sv) {
                if (!assert_content_type_is_json(req)) {
                    return api_response(http::status::bad_request, Response::INVALID_CONTENT_TYPE);
                }
                try {
                    json::value req_body = json::parse(req.body());
                    std::string user_name = json::value_to<std::string>(req_body.at("userName"));
                    std::string map_id = json::value_to<std::string>(req_body.at("mapId"));
                    if (user_name.empty()) {
                        return api_response(http::status::bad_request, Response::INVALID_NAME);
                    }
                    const model::Map* map = game_.FindMap(model::Map::Id(map_id));
                    if (!map) {
                        return api_response(http::status::not_found, Response::MAP_NOT_FOUND);
                    }
                    auto player = model::Players::CreatePlayer(user_name);
                    game_.JoinMap(map, player);
                    return api_response(http::status::ok, json_encoder::PlayerToString(*player));
                } catch(...) {
                    return api_response(http::status::bad_request, Response::JOIN_GAME_REQUEST_PARSE_ERROR);
                }
            }
            auto res = api_response(http::status::method_not_allowed, Response::INVALID_METHOD);
            res.set(http::field::allow, "POST");
            return res;
        }
        if (target == ApiPath::PLAYERS) {
            if (req.method_string() == "GET"sv || req.method_string() == "HEAD"sv) {
                if (auto token = try_extract_token(req)) {
                    if (auto player = try_get_player_by_token(*token)) {
                        return api_response(http::status::ok, json_encoder::GameSessionToString(*(*player)->GetSession()));
                    }
                    return api_response(http::status::unauthorized, Response::PLAYER_TOKEN_NOT_FOUND);
                }
                return api_response(http::status::unauthorized, Response::AUTHORIZATION_HEADER_MISSING);
            }
            auto res = api_response(http::status::method_not_allowed, Response::INVALID_METHOD);
            res.set(http::field::allow, "GET, HEAD");
            return res;
        }
        if (target == ApiPath::STATE) {
            if (req.method_string() == "GET"sv || req.method_string() == "HEAD"sv) {
                if (auto token = try_extract_token(req)) {
                    if (auto player = try_get_player_by_token(*token)) {
                        return api_response(http::status::ok, json_encoder::GameStateToString(*(*player)->GetSession()));
                    }
                    return api_response(http::status::unauthorized, Response::PLAYER_TOKEN_NOT_FOUND);
                }
                return api_response(http::status::unauthorized, Response::AUTHORIZATION_HEADER_MISSING);
            }
            auto res = api_response(http::status::method_not_allowed, Response::INVALID_METHOD);
            res.set(http::field::allow, "GET, HEAD");
            return res;
        }
        if (target == ApiPath::ACTION) {
            if (req.method_string() == "POST"sv) {
                if (!assert_content_type_is_json(req)) {
                    return api_response(http::status::bad_request, Response::INVALID_CONTENT_TYPE);
                }
                if (auto token = try_extract_token(req)) {
                    if (auto player = try_get_player_by_token(*token)) {
                        try {
                            json::value req_body = json::parse(req.body());
                            std::string move = json::value_to<std::string>(req_body.at("move"));
                            if (move != "U" && move != "R" && move != "D" && move != "L" && move != "") {
                                return api_response(http::status::bad_request, Response::ACTION_REQUEST_PARSE_ERROR);
                            }
                            (*player)->GetDog()->ChangeDirection(move);
                            return api_response(http::status::ok, "{}");
                        } catch(...) {
                            return api_response(http::status::bad_request, Response::ACTION_REQUEST_PARSE_ERROR);
                        }
                    }
                    return api_response(http::status::unauthorized, Response::PLAYER_TOKEN_NOT_FOUND);
                }
                return api_response(http::status::unauthorized, Response::AUTHORIZATION_HEADER_MISSING);
            }
            auto res = api_response(http::status::method_not_allowed, Response::INVALID_METHOD);
            res.set(http::field::allow, "POST");
            return res;
        }
        if (!is_ticking_ && target == ApiPath::TICK) {
            if (req.method_string() == "POST"sv) {
                if (!assert_content_type_is_json(req)) {
                    return api_response(http::status::bad_request, Response::INVALID_CONTENT_TYPE);
                }
                try {
                    json::value req_body = json::parse(req.body());
                    int time_delta = req_body.at("timeDelta").as_int64();
                    game_.Tick(time_delta, loot_generator_);
                    return api_response(http::status::ok, "{}");
                } catch(...) {
                    return api_response(http::status::bad_request, Response::TICK_REQUEST_PARSE_ERROR);
                }
            }
            auto res = api_response(http::status::method_not_allowed, Response::INVALID_METHOD);
            res.set(http::field::allow, "POST");
            return res;
        }
        if (target.rfind(ApiPath::RECORDS, 0) == 0) {
            if (req.method_string() == "GET"sv || req.method_string() == "HEAD"sv) {
                int start = 0;
                int maxItems = 100;
                if (target != ApiPath::RECORDS) {
                    std::string query = target.substr(target.find('?') + 1);
                    if (query.find('&') != std::string::npos) {
                        if (query.substr(0, 5) == "start") {
                            query = query.substr(6);
                            int i = 0;
                            while ('0' <= query[i] && query[i] <= '9') {
                                ++i;
                            }
                            std::string start_str = query.substr(0, i);
                            query = query.substr(i);
                            query = query.substr(10);
                            std::string maxItems_str = query;
                            start = std::stoi(start_str);
                            maxItems = std::stoi(maxItems_str);

                        } else if (query.substr(0, 8) == "maxItems") {
                            query = query.substr(9);
                            int i = 0;
                            while ('0' <= query[i] && query[i] <= '9') {
                                ++i;
                            }
                            std::string maxItems_str = query.substr(0, i);
                            query = query.substr(i);
                            query = query.substr(7);
                            std::string start_str = query;
                            start = std::stoi(start_str);
                            maxItems = std::stoi(maxItems_str);
                        } else {
                            if (query.substr(0, 5) == "start") {
                                query = query.substr(6);
                                int i = 0;
                                while ('0' <= query[i] && query[i] <= '9') {
                                    ++i;
                                }
                                std::string start_str = query.substr(0, i);
                                start = std::stoi(start_str);
                            } else if (query.substr(0, 8) == "maxItems") {
                                query = query.substr(9);
                                int i = 0;
                                while ('0' <= query[i] && query[i] <= '9') {
                                    ++i;
                                }
                                std::string maxItems_str = query.substr(0, i);
                                maxItems = std::stoi(maxItems_str);
                            }
                        }
                    }
                }
                if (maxItems > 100) {
                    return api_response(http::status::bad_request, Response::BAD_REQUEST);
                }
                pqxx::connection conn{db_url_};
                pqxx::work work{conn};
                json::array res;
                for (auto& [name, score, play_time_ms] : work.query<std::string, int, int>("SELECT name, score, play_time_ms FROM retired_players ORDER BY score DESC, play_time_ms, name LIMIT '" + std::to_string(maxItems) + "' OFFSET '" + std::to_string(start) + "';")) {
                    res.push_back({
                        {"name", name},
                        {"score", score},
                        {"playTime", play_time_ms / 1000.0}
                    });
                }
                std::ostringstream oss;
                oss << res;
                return api_response(http::status::ok, oss.str());
            }
            auto res = api_response(http::status::method_not_allowed, Response::INVALID_METHOD);
            res.set(http::field::allow, "GET, HEAD");
            return res;
        }
        return api_response(http::status::bad_request, Response::BAD_REQUEST);
    }

    FileRequestResult HandleFileRequest(const StringRequest& req) {
        const auto text_response = [&req](http::status status, std::string_view text) {
            return MakeResponse<StringResponse>(status, text, req.version(), req.keep_alive(), ContentType::TEXT_PLAIN);
        };
        const auto file_response = [&req](http::status status, http::file_body::value_type&& file, std::string_view content_type = ContentType::TEXT_HTML) {
            return MakeResponse<FileResponse>(status, std::move(file), req.version(), req.keep_alive(), content_type);
        };
        fs::path rel_path{UrlDecode(req.target().substr(1))};
        if (rel_path == "") {
            rel_path = "index.html";
        }
        fs::path abs_path = fs::weakly_canonical(base_path_ / rel_path);
        if (IsSubPath(abs_path, base_path_)) {
            http::file_body::value_type file;
            if (boost::system::error_code ec; file.open(abs_path.string().c_str(), beast::file_mode::read, ec), ec) {
                return text_response(http::status::not_found, "Not found"sv);
            }
            return file_response(http::status::ok, std::move(file), GetContentType(GetFileExtension(abs_path)));
        }
        return text_response(http::status::bad_request, "Bad Request"sv);
    }

    StringResponse ReportServerError(const StringRequest& req) const {
        const auto text_response = [&req](http::status status, std::string_view text) {
            return MakeResponse<StringResponse>(status, text, req.version(), req.keep_alive(), ContentType::TEXT_PLAIN);
        };
        http_server::ReportError({}, "server error");
        return text_response(http::status::bad_request, "Bad Request"sv);
    }
        
    model::Game& game_;
    fs::path base_path_;
    Strand api_strand_;
    bool is_ticking_;
    loot_gen::LootGenerator& loot_generator_;
    std::string db_url_;
};

}  // namespace http_handler
