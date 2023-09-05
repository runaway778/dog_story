#include "sdk.h"
//
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/program_options.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <pqxx/pqxx>
#include <iostream>
#include <thread>
#include <string_view>
#include <fstream>
#include <iostream>
#include <optional>
#include <vector>

#include "json_loader.h"
#include "request_handler.h"
#include "logger.h"
#include "ticker.h"
#include "loot_generator.h"

using namespace std::literals;
namespace net = boost::asio;
namespace sys = boost::system;
using InputArchive = boost::archive::text_iarchive;
using OutputArchive = boost::archive::text_oarchive;

namespace {

// Запускает функцию fn на n потоках, включая текущий
template <typename Fn>
void RunThreads(unsigned num_threads, const Fn& fn) {
    num_threads = std::max(1u, num_threads);
    std::vector<std::jthread> workers;
    workers.reserve(num_threads - 1);
    // Запускаем n-1 рабочих потоков, выполняющих функцию fn
    while (--num_threads) {
        workers.emplace_back(fn);
    }
    fn();
}

void MyFormatter(logging::record_view const& rec, logging::formatting_ostream& strm) {
    strm << json::value{
        {"timestamp", to_iso_extended_string(*rec[timestamp])},
        {"data", *rec[additional_data]},
        {"message", *rec[expr::smessage]}
    };;
}

struct Args {
    int tick_period = 0;
    std::string config_file;
    std::string www_root;
    std::string state_file;
    int save_state_period;
    bool randomize_spawn_points = false;
    bool contains_state_file = false;
    bool contains_save_state_period = false;
};

[[nodiscard]] std::optional<Args> ParseCommandLine(int argc, const char* const argv[]) {
    namespace po = boost::program_options;

    po::options_description desc{"Allowed options:"};

    Args args;
    desc.add_options()
        ("help,h", "produce help message")
        ("tick-period,t", po::value(&args.tick_period)->value_name("milliseconds"s), "set tick period")
        ("config-file,c", po::value(&args.config_file)->value_name("file"s), "set config file path")
        ("www-root,w", po::value(&args.www_root)->value_name("dir"s), "set static files root")
        ("randomize-spawn-points", "spawn dogs at random positions")
        ("state-file", po::value(&args.state_file)->value_name("file"s), "set state file")
        ("save-state-period", po::value(&args.save_state_period)->value_name("milliseconds"s), "set save state period");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.contains("help"s)) {
        std::cout << desc;
        return std::nullopt;
    }

    if (!vm.contains("config-file"s)) {
        throw std::runtime_error("Config file is not specified"s);
    }

    if (!vm.contains("www-root"s)) {
        throw std::runtime_error("Static files root is not specified"s);
    }
    
    if (vm.contains("randomize-spawn-points")) {
        args.randomize_spawn_points = true;
    }
    
    if (vm.contains("state-file")) {
        args.contains_state_file = true;
    }

    if (vm.contains("save-state-period")) {
        args.contains_save_state_period = true;
    }

    return args;
}

constexpr const char DB_URL_ENV_NAME[]{"GAME_DB_URL"};

std::string GetUrlFromEnv() {
    std::string db_url;
    if (const auto* url = std::getenv(DB_URL_ENV_NAME)) {
        db_url = url;
    } else {
        throw std::runtime_error(DB_URL_ENV_NAME + " environment variable not found"s);
    }
    return db_url;
}


}  // namespace

int main(int argc, const char* argv[]) {
    logging::add_common_attributes();
    logging::add_console_log( 
        std::cout,
        keywords::format = &MyFormatter,
        keywords::auto_flush = true
    ); 
    try {
        if (auto args = ParseCommandLine(argc, argv)) {

            // 1. Загружаем карту из файла и построить модель игры
            model::Game game = json_loader::LoadGame(args->config_file);

            if (args->randomize_spawn_points) {
                game.randomize_spawn_points = true;
            }

            if (args->contains_state_file) {
                game.contains_state_file = true;
                game.state_file = args->state_file;
            }

            if (args->contains_save_state_period) {
                game.contains_save_state_period = true;
                game.save_state_period = args->save_state_period;
            }

            if (args->contains_state_file) {
                std::ifstream in(args->state_file);
                if (in.good()) {
                    InputArchive input_archive{in};
                    model::Players players;
                    input_archive >> players;
                    input_archive >> game;

                    for (auto& [map_id, game_sessions] : game.game_sessions_on_map_) {
                        for (auto& game_session : game_sessions) {
                            game_session->map_ = game.FindMap(map_id);
                            for (auto& [dog_id, dog] : game_session->dogs_) {
                                std::shared_ptr<model::Player> player = players.FindByDogIdAndMapId(dog_id, map_id);
                                player->GetSession() = game_session;
                                dog = player->GetDog();
                            }
                        }
                    }
                }
            }

            game.db_url = GetUrlFromEnv();

            // 2. Инициализируем io_context
            const unsigned num_threads = std::thread::hardware_concurrency();
            net::io_context ioc(num_threads);

            // 3. Добавляем асинхронный обработчик сигналов SIGINT и SIGTERM
            net::signal_set signals(ioc, SIGINT, SIGTERM);
            signals.async_wait([&ioc](const sys::error_code& ec, [[maybe_unused]] int signal_number) {
                if (!ec) {
                    ioc.stop();
                }
            });

            auto api_strand = net::make_strand(ioc);

            loot_gen::LootGenerator loot_generator{std::chrono::duration_cast<loot_gen::LootGenerator::TimeInterval>(std::chrono::duration<double>{game.period}), game.probability};

            pqxx::connection conn{game.db_url};

            pqxx::work work{conn};
            work.exec(R"(
                CREATE TABLE IF NOT EXISTS retired_players (
                    id UUID PRIMARY KEY,
                    name varchar(100) NOT NULL,
                    score INT,
                    play_time_ms INT
                );
            )");
            work.exec(R"(
                CREATE INDEX IF NOT EXISTS retired_players_idx ON retired_players (score DESC, play_time_ms, name);
            )");
            work.commit();

            // 4. Создаём обработчик HTTP-запросов и связываем его с моделью игры
            http_handler::RequestHandler handler{game, args->www_root, api_strand, static_cast<bool>(args->tick_period), loot_generator, game.db_url};

            if (static_cast<bool>(args->tick_period)) {
                auto ticker = std::make_shared<Ticker>(api_strand, std::chrono::milliseconds(args->tick_period),
                    [&game, &loot_generator](std::chrono::milliseconds delta) { game.Tick(delta.count(), loot_generator); }
                );
                ticker->Start();
            }
            
            // 5. Запустить обработчик HTTP-запросов, делегируя их обработчику запросов
            const auto address = net::ip::make_address("0.0.0.0");
            constexpr net::ip::port_type port = 8080;
            http_server::ServeHttp(ioc, {address, port}, [&handler](auto&& req, auto&& send) {
                handler(std::forward<decltype(req)>(req), std::forward<decltype(send)>(send));
            });

            // Эта надпись сообщает тестам о том, что сервер запущен и готов обрабатывать запросы
            //std::cout << "Server has started..."sv << std::endl;
            BOOST_LOG_TRIVIAL(info) << logging::add_value(additional_data, 
                                    json::value{
                                        {"address", address.to_string()},
                                        {"port", port}
                                    })
                                    << "server started"sv;

            // 6. Запускаем обработку асинхронных операций
            RunThreads(std::max(1u, num_threads), [&ioc] {
                ioc.run();
            });

            if (args->contains_state_file) {
                //std::ofstream out(args->state_file);
                std::ofstream out(args->state_file + "_temp.txt");
                OutputArchive output_archive{out};
                model::Players players;
                output_archive << players;
                output_archive << game;
                std::filesystem::rename(args->state_file + "_temp.txt", args->state_file);
            }

        }
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << logging::add_value(additional_data, 
                        json::value{
                            {"code", "EXIT_FAILURE"},
                            {"exception", ex.what()}
                        })
                        << "server exited"sv;
        return EXIT_FAILURE;
    }
    BOOST_LOG_TRIVIAL(info) << logging::add_value(additional_data, 
                    json::value{
                        {"code", 0}
                    })
                    << "server exited"sv;
}
