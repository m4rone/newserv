#pragma once

#include <memory>
#include <string>

#include "Client.hh"
#include "ServerState.hh"

std::shared_ptr<Lobby> create_game_generic(
    std::shared_ptr<ServerState> s,
    std::shared_ptr<Client> c,
    const std::string& name,
    const std::string& password = "",
    Episode episode = Episode::EP1,
    GameMode mode = GameMode::NORMAL,
    Difficulty difficulty = Difficulty::NORMAL,
    bool allow_v1 = false,
    std::shared_ptr<Lobby> watched_lobby = nullptr,
    std::shared_ptr<Episode3::BattleRecordPlayer> battle_player = nullptr);
void set_lobby_quest(std::shared_ptr<Lobby> l, std::shared_ptr<const Quest> q, bool substitute_v3_for_ep3 = false);
// What the quest board asks before it loads a quest in a game (the 10 command for the quest menu): the quest is
// available to every client in the game (Lobby::quest_include_condition). Spire's "Start a new run" asks the same
// before it starts the run quest (SpireFlagBridge.cc spire_base_new_run and spire_base_game_start). The text is what
// the asking client reads when it is not: at the board in a message box, from the Spire bridge as one ticker line.
bool quest_unlocked_for_all_players(std::shared_ptr<const Lobby> l, std::shared_ptr<const Quest> q);
constexpr const char* QUEST_NOT_UNLOCKED_TEXT = "This quest has not\nbeen unlocked for\nall players in this\ngame.";
asio::awaitable<void> start_login_server_procedure(std::shared_ptr<Client> c);

asio::awaitable<void> start_proxy_session(
    std::shared_ptr<Client> c, const std::string& host, uint16_t port, bool use_persistent_config);
asio::awaitable<void> end_proxy_session(std::shared_ptr<Client> c, const std::string& error_message = "");

asio::awaitable<void> on_connect(std::shared_ptr<Client> c);
asio::awaitable<void> on_disconnect(std::shared_ptr<Client> c);

asio::awaitable<void> on_command(std::shared_ptr<Client> c, std::unique_ptr<Channel::Message> msg);
asio::awaitable<void> on_command_with_header(std::shared_ptr<Client> c, const std::string& data);
