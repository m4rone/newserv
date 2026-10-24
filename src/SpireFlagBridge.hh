#pragma once

#include <stdint.h>

#include <chrono>
#include <memory>

#include "Client.hh"
#include "Lobby.hh"
#include "StaticGameData.hh"

// Spire flag bridge (GC V3 only).
//
// Battle quests with replace_char enabled run on a temporary overlay character on the client, so any quest flag the
// quest script sets during the battle is lost once the battle ends. Measured on the lab: a 6x75 sent by the server
// sets the flag in the running game, but the GC client does not save it to the memory card as character state. So the
// server is the only source of truth for the bridged range:
// - Every quest flag write in the bridged range by a GC V3 client, in any game (Battle or not) that runs a quest listed
//   in SpireBridgeQuests (v5; a missing or empty list records nothing), is recorded per GC license serial number in
//   system/spire-bridge/<serial>.json. Both set and clear are recorded; the last write wins.
// - Every time the same client finishes loading into any game (Battle or not), all stored flags are sent to it as
//   6x75 commands (set or clear), so quest scripts in that game read the server's state. Stored entries are never
//   erased by delivery.
// - Spire meta registers (v4): in the quests listed in the server config key SpireBridgeQuests (a JSON list of quest
//   numbers; any game mode), 6x77 writes to registers 232-239 and 243-247 by a GC V3 client are merged into the same
//   per-serial file (232/233 max, 234 min with 0 ignored, 235-239 and 245 OR, 243/244/246/247 last write wins) and
//   are not forwarded, so a co-op partner never takes them over. A write of 'SPIR' to register 248 is answered to that
//   client only: one 6x77 per stored register (0 if none), then 248 = 'DONE'. Registers 240-242 are left alone (Sega's
//   q101 and Schtserv commands use them). If SpireBridgeQuests is missing or empty, the register bridge takes nothing:
//   every register write is handled exactly as without the bridge. The flag part above records only in listed quests
//   (v5) but delivers stored flags on every join, so E06 keeps reading its payout flag in normal games.
// - Battle end (v10b): a write of 'ENDB' to register 248 sends the phases in SpireBattleEndPhases as 6x7D (what = 0,
//   set battle phase) to every client in the Battle game, 2 seconds apart. Off unless the key lists phases.
// - Room clear (v16, lab probe): a write of 'ROOM' to register 248 sends the server text "Room clear!" (B0) to every
//   client in the game, so the quest shows no window_msg of its own at the room clear.
// - Short messages (v17): a write of a message code (SPIRE_BRIDGE_MSG_*) to register 248 sends the matching text (B0)
//   to the writing client: wave n/m, life lost, time warning, partner wait, run over.
// - Records (v18): best score, best time, runs and wins per character on the server (SPIRE_BRIDGE_REC_*).
// - Run timer (v19): the time limit, its warning and "player left" on the server (SPIRE_BRIDGE_TIMER_*).
// - Reward draw (v20): the pause A draw on the server (SPIRE_BRIDGE_DRAW_*, SpireDraw.hh).

// The range of quest flags that is bridged (inclusive on both ends). Flags outside this range are never recorded.
constexpr uint16_t SPIRE_BRIDGE_FIRST_FLAG = 0x0380;
constexpr uint16_t SPIRE_BRIDGE_LAST_FLAG = 0x03AF;

// The meta registers that are stored (inclusive; 240-242 in between are not), and the request register and its values.
// The quest numbers in which the register bridge applies come from the config (DataIndex::spire_bridge_quest_numbers).
constexpr uint8_t SPIRE_BRIDGE_FIRST_REGISTER = 232;
constexpr uint8_t SPIRE_BRIDGE_LAST_REGISTER = 247;
constexpr uint8_t SPIRE_BRIDGE_REQUEST_REGISTER = 248;
constexpr uint32_t SPIRE_BRIDGE_REQUEST_VALUE = 0x53504952; // 'SPIR'
constexpr uint32_t SPIRE_BRIDGE_DONE_VALUE = 0x444F4E45; // 'DONE'
// v10b: written to register 248 by the quest at the end of a run; see spire_bridge_end_battle. The phases come from the
// server config key SpireBattleEndPhases (a JSON list of 6x7D phase numbers, e.g. [3, 4]; missing or empty: off).
constexpr uint32_t SPIRE_BRIDGE_BATTLE_END_VALUE = 0x454E4442; // 'ENDB'
constexpr auto SPIRE_BRIDGE_BATTLE_PHASE_INTERVAL = std::chrono::seconds(2);
// v16: written to register 248 by the quest's leader at a room clear; answered with SPIRE_BRIDGE_ROOM_CLEAR_TEXT as a
// text message to every client in the game.
constexpr uint32_t SPIRE_BRIDGE_ROOM_CLEAR_VALUE = 0x524F4F4D; // 'ROOM'
constexpr const char* SPIRE_BRIDGE_ROOM_CLEAR_TEXT = "Room clear!";
// v17: short run messages. A client writes kind << 24 | parameters to register 248 where it used to show a window_msg;
// the bridge answers with the text (B0) to that client; the wave code (leader only) goes to every client. Kinds:
//   'W' wave n/m (n << 8 | m) · 'L' life lost (last << 8 | lives left) · 'T' time warning (minutes left)
//   'P' partner wait (1: your partner is waiting, 0: waiting for your partner) · 'X' run over (rooms << 16 | seconds)
// The kind letters differ from the first byte of every 4CC above ('SPIR', 'DONE', 'ENDB', 'ROOM').
constexpr uint8_t SPIRE_BRIDGE_MSG_WAVE = 'W';
constexpr uint8_t SPIRE_BRIDGE_MSG_LIFE = 'L';
constexpr uint8_t SPIRE_BRIDGE_MSG_TIME = 'T';
constexpr uint8_t SPIRE_BRIDGE_MSG_PARTNER = 'P';
constexpr uint8_t SPIRE_BRIDGE_MSG_RUN_OVER = 'X';
// v18: records per character, kept by the server (system/spire-bridge/<serial>.<character>.records.json) instead of
// quest counters 12-14 on the card. Codes in register 248, from each client for its own character:
//   'B' run started (runs + 1) · 'K' final score (low 24 bits; held until 'F') · 'F' run finished (bit 23 won, low 16
//   bits the seconds): best score = max, and on a win won + 1 and best time = min (a new best time also sets payout flag
//   SPIRE_BRIDGE_RECORD_FLAG) · 'Y' Records board. 'F' and 'Y' are answered with the record text (B0) to that client.
constexpr uint8_t SPIRE_BRIDGE_REC_BEGIN = 'B';
constexpr uint8_t SPIRE_BRIDGE_REC_SCORE = 'K';
constexpr uint8_t SPIRE_BRIDGE_REC_FINISH = 'F';
constexpr uint8_t SPIRE_BRIDGE_REC_BOARD = 'Y';
constexpr uint16_t SPIRE_BRIDGE_RECORD_FLAG = 0x03A2; // E06 payout: a new best time (score.py FLAG_PAY_RECORD)

// v19: the run timer on the server, instead of the quest's t_timer clock. The leader writes, at its run start,
//   'S' << 24 | warning minutes before the end << 16 | time limit in seconds (r95, after Haste)
// to register 248 (warning below 80 minutes, so the code never equals 'SPIR'). The server then ticks once a second for that game (co_spawn + async_sleep, like the ENDB phases):
// at limit - warning it sends the time warning text (B0) to every client, once; at the limit, or as soon as the game
// has fewer clients than at the 'S', it sends "Time is up." / "The second player left." (B0) and a 6x77
// register 252 = SPIRE_BRIDGE_TIMER_END << 24 | cause (1 time up, 2 player left) to every client, and stops. The quest's
// t_timer reads r252 and goes into its end. A run-over code ('X', v17) from any client, a new 'S' or the end of the
// game stops the timer. r252 is in the free range r251-r255 (regie), outside the stored ranges; r251 is room clear.
constexpr uint8_t SPIRE_BRIDGE_TIMER_START = 'S';
constexpr uint8_t SPIRE_BRIDGE_TIMER_END = 'E';
constexpr uint8_t SPIRE_BRIDGE_TIMER_REGISTER = 252;
constexpr uint32_t SPIRE_BRIDGE_TIMER_TIME_UP = 1;
constexpr uint32_t SPIRE_BRIDGE_TIMER_PLAYER_LEFT = 2;
constexpr auto SPIRE_BRIDGE_TIMER_TICK = std::chrono::seconds(1);

// v19: room clear detected by the server. When a wave is finished and its room is clear by the event rule (see
// spire_bridge_check_room_clear; a wave that never started, such as the elite wave on the normal path, does not hold
// the room, and a boss wave sends nothing), the server sends SPIRE_BRIDGE_ROOM_CLEAR_TEXT and sets register SPIRE_BRIDGE_ROOM_CLEARED_REGISTER to
// SPIRE_BRIDGE_ROOM_CLEARED_TAG | floor << 8 | room (6x77) on every client in the game, once per room and game. The quest
// waits for that register instead of polling its own kill counters.
constexpr uint8_t SPIRE_BRIDGE_ROOM_CLEARED_REGISTER = 251;
constexpr uint32_t SPIRE_BRIDGE_ROOM_CLEARED_TAG = 0x434C0000; // 'CL'

// v20: the reward draw on the server. The quest writes its SPIRE_DRAW_NUM_INPUTS input registers, in order, to
// register SPIRE_BRIDGE_DRAW_DATA_REGISTER (each a 6x77; the bridge takes them and keeps them per client, never forwards
// them), then 'Q' << 24 | count to register 248. The server loads the draw table system/spire-draw/q<quest>.json (written
// by the quest build), draws, and answers that client like SPIR: 6x77 r181 = the drawn menu, then r248 = 'DONE'. With a
// missing table or a wrong count only the log says why and no answer is sent: the quest's own draw takes over after
// its wait. The data register is in the free range r251-r255 (regie); r251 room clear, r252 run end.
constexpr uint8_t SPIRE_BRIDGE_DRAW_REQUEST = 'Q';
constexpr uint8_t SPIRE_BRIDGE_DRAW_DATA_REGISTER = 253;
constexpr uint8_t SPIRE_BRIDGE_DRAW_RESULT_REGISTER = 181;
constexpr const char* SPIRE_BRIDGE_DRAW_DIRECTORY = "system/spire-draw";

// Directory (relative to the server's working directory) where the per-serial JSON files are stored.
constexpr const char* SPIRE_BRIDGE_DIRECTORY = "system/spire-bridge";

// Called from the 6x75 handler for every valid quest flag write. Does nothing unless the client is GC V3, the lobby is
// a game running a quest listed in SpireBridgeQuests, the client has a GC license, and the flag is in the bridged
// range. Never throws.
void spire_bridge_record_flag(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, Difficulty difficulty, uint16_t flag_num, bool is_set);

// Called when a client has finished loading into a game (6F). Does nothing unless the client is GC V3, the lobby is a
// game, and there are stored entries for the client's GC license serial. Never throws.
void spire_bridge_deliver_flags(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l);

// Called from the 6x77 handler for every valid quest register write. Returns true if the bridge took the write (the
// caller must then not forward it), false if the caller should handle it as before. Takes nothing unless the client
// is GC V3 with a GC license, the lobby is a game running a quest listed in SpireBridgeQuests (a missing or empty list
// means never), and the register is 232-239, 243-247 or 248. Never throws.
bool spire_bridge_handle_register(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint16_t register_number, uint32_t value);

// Called from the 6x76 set-flags path once per wave, the first time all its enemies are defeated, after its events are
// set to finished (v19, spire-server-all). Does nothing unless the client is GC V3 and the lobby is a game running a
// quest listed in SpireBridgeQuests, or the wave has a boss. The room counts as clear when no wave event in it is started
// but unfinished, and no finished event in it triggers an unfinished event of the room or constructs a wave of the room
// that is not defeated. Once per (game, floor, room). Never throws.
void spire_bridge_check_room_clear(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room, uint16_t wave_number);

// Spire guard (v6-v15; GC V3 only).
//
// Quit Game (or a disconnect, crash or power loss) in the middle of a replace_char Battle quest leaves the overlay
// character (Lv1) on the memory card, and the client logs in with it next time. The guard keeps a per-serial snapshot
// of the real character next to the bridge file, in system/spire-bridge/<serial>.snapshot.json, with two markers:
// RunInProgress (a run is running) and RestorePending (the run's game has ended, by a 98 or not, and the restore is not
// verified yet). Measured on the Wii (v8): even a normal end of a run saves the overlay character to the card, so every
// Spire game end needs a verified restore.
// - When a quest listed in SpireBridgeQuests is loaded in a Battle game with replace_char, RunInProgress is set and the
//   real character (inventory and disp from the server's character data, which comes from the 61 at login or a 98 from
//   a non-Spire game; never the overlay) is stored. If either marker is already set, the stored snapshot is kept, so a
//   61 or 98 from or after a Spire game can never become the snapshot.
// - A 98 with RunInProgress set turns it into RestorePending; a 98 never clears either marker. The snapshot stays until
//   the next run replaces it.
// - When save is enabled for a client with either marker set, it becomes RestorePending and the 97 is held back: the
//   client's full character is requested (GetExtendedPlayerInfo), its inventory and disp are replaced by the snapshot,
//   the result is sent with SetExtendedPlayerInfo, and a 61 is requested. Only that 61, showing the snapshot's level
//   and EXP, clears RestorePending (the inventory count is logged); only then does the caller send 97 with flag 1. If
//   the client cannot run functions, does not answer within 15 seconds, or the 61 does not match, no 97 is sent, the
//   login is dropped and both files stay, so the next login tries again. 97 is never sent with flag 0 (that deletes
//   the character's items and challenge records). To give up on a pending restore, delete the snapshot file.
// - v9: the same restore also runs as soon as a client with RestorePending leaves a game and is back in a lobby (84),
//   so the real character is back without a new login. A failure there drops the connection; the login restore stays
//   the safety net.
// - v10: a verified lobby restore does not clear the marker: the real character is then only in the client's memory
//   until the client saves, so a Wii switched off right after it would still have the overlay on the card. The marker
//   becomes AwaitingSave instead, and is cleared at the next login whose 61 already shows the snapshot's level and EXP,
//   without a push; any other login pushes again. While a restore runs or RestorePending is set, game joins and game
//   creation are refused with a short message, so a push never lands in a game.
// - v11: snapshot and markers belong to the serial number plus the character (name, class and section ID from the 61;
//   GC sends no character slot), in system/spire-bridge/<serial>.<character id>.snapshot.json, so switching
//   characters on one card never restores one character onto another. The restore is refused (no push, marker kept)
//   if the login 61 or the full character fetched before the push shows another character than the snapshot. A
//   v6-v10 <serial>.snapshot.json is moved to the new name at the first use by the same character.
// - v12 (critic review): the lobby restore stores the overlay it replaced; at the next login only a 61 that still
//   shows that overlay, or has less EXP than the snapshot, gets a push, so real progress saved since is never undone.
//   A run that cannot store its snapshot gets no overlay (the client is dropped). Files are written with fsync. Game
//   joins are also refused while RunInProgress is set, and a login straight into a game honours the same check.
// - v13: a game join refused in a lobby while RestorePending is set and no restore runs starts the lobby restore.
// - v14: a login 61 with the snapshot's level and EXP always confirms the lobby restore (no push), and no overlay
//   fingerprint is kept when the client already had the real character in memory at the lobby restore.
// - v15: the guard protects the quests in the config key SpireGuardQuests; if that key is missing, the quests in
//   SpireBridgeQuests. So a probe quest with replace_char can be guarded without the bridge storing its flags and
//   registers under the player's serial.

// Called from on_quest_loaded for each client, before its battle overlay is created. Does nothing unless the client is
// GC V3 with a GC license, the lobby is a Battle game whose quest is listed in SpireBridgeQuests and uses replace_char.
// Never throws.
// Returns false (v12) if the client must not get the battle overlay: the guard applies but could not store the
// snapshot. The caller then drops the client instead of creating the overlay.
bool spire_guard_begin_run(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l);

// Called from the 98 handler before the client is removed from the game. Turns RunInProgress into RestorePending.
// Never throws.
void spire_guard_end_run(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l);

// Called from on_84 right after a client that just left a game was added to a lobby (v9). If RestorePending is set,
// runs the same restore as the login path, at once, in the lobby. On success RestorePending is cleared; on any failure
// the connection is dropped and the marker stays, so the login restore remains the safety net. Never runs at the same
// time as another restore for the same client. Never throws.
asio::awaitable<void> spire_guard_restore_in_lobby(std::shared_ptr<Client> c);

// Called before a client joins or creates a game (v10). Returns false, after sending the client a short message, if a
// restore is running for the client or RestorePending is set; the caller must then not let the client into a game.
// Never throws.
// tell_client=false skips the message (used at login, v12).
bool spire_guard_game_join_allowed(std::shared_ptr<Client> c, bool tell_client = true);

// Called from enable_save_if_needed right before 97 is sent. Returns true if 97 may be sent; false if the caller must
// not send 97 and must drop the login. Never throws.
asio::awaitable<bool> spire_guard_restore_if_needed(std::shared_ptr<Client> c);
