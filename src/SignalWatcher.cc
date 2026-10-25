#include "SignalWatcher.hh"

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <phosg/Random.hh>
#include <phosg/Strings.hh>

#include "InventoryBackup.hh"
#include "ReceiveCommands.hh"
#include "SendCommands.hh"
#include "ServerState.hh"
#include "StaticGameData.hh"

SignalWatcher::SignalWatcher(std::shared_ptr<ServerState> state)
    : log("[SignalWatcher] "), state(state), signals(*this->state->io_context) {
  asio::co_spawn(*this->state->io_context, this->signal_handler_task(), asio::detached);
}

asio::awaitable<void> SignalWatcher::signal_handler_task() {
#ifndef PHOSG_WINDOWS
  this->signals.add(SIGUSR1);
  this->signals.add(SIGUSR2);
  // Inventory backups (karakter-verlies-battle): a stop (systemctl restart, apply-on-pc.sh) is a hard end for every
  // client in a game; write their Exit first, then end exactly as the default action would
  this->signals.add(SIGTERM);
  this->signals.add(SIGINT);
  for (;;) {
    int signum = co_await this->signals.async_wait(asio::use_awaitable);
    switch (signum) {
      case SIGUSR1:
        this->log.info_f("Received SIGUSR1; reloading config.json");
        try {
          this->state->data->load_config_early();
          this->state->data->load_config_late();
          this->state->disconnect_all_banned_clients();
          this->state->update_default_lobby_events_from_config();
          phosg::fwrite_fmt(stderr, "Configuration update complete\n");
        } catch (const std::exception& e) {
          phosg::fwrite_fmt(stderr, "FAILED: {}\n", e.what());
          phosg::fwrite_fmt(stderr, "Some configuration may have been reloaded. Fix the underlying issue and try again.\n");
        }
        break;
      case SIGUSR2:
        this->log.info_f("Received SIGUSR2; reloading config.json and all dependencies");
        try {
          this->state->data->load_all();
          this->state->disconnect_all_banned_clients();
          this->state->update_default_lobby_events_from_config();
          phosg::fwrite_fmt(stderr, "Configuration update complete\n");
        } catch (const std::exception& e) {
          phosg::fwrite_fmt(stderr, "FAILED: {}\n", e.what());
          phosg::fwrite_fmt(stderr, "Some configuration may have been reloaded. Fix the underlying issue and try again.\n");
        }
        break;
      case SIGTERM:
      case SIGINT: {
        size_t count = 0;
        try {
          count = inventory_backup_on_server_stop(this->state);
        } catch (const std::exception& e) {
          this->log.warning_f("Inventory backup: server stop failed: {}", e.what());
        }
        this->log.info_f("Received signal {}; exit state written for {} client(s) in a game; stopping", signum, count);
        ::signal(signum, SIG_DFL);
        ::raise(signum);
        break;
      }
      default:
        this->log.warning_f("Unknown signal received: {}", signum);
    }
  }
#endif
  co_return;
}
