#pragma once

#include <stdint.h>

#include <chrono>
#include <functional>
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
//   numbers; any game mode), 6x77 writes to registers 232-239 and 243-247 by a GC V3 client are merged into a file per
//   character (audit S-6: <serial>.<character id>.registers.json, like the flags and records) (232/233 max, 234 min with 0 ignored, 235-239 and 245 OR, 243/244/246/247 last write wins) and
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
// - No window in a run (v35): every bridge text during a run is one B0 line (spire_ticker_line), the form of "Waiting
//   for your partner..."; the records text of a run end waits for the lobby (spire_deliver_lobby_texts).
// - Run over to the lobby (v35, regie): the first 'X' in a game marks the run over (no room clear or checkpoint after
//   it), sends the run-over text to every client and sends each client back to the lobby the way $exit leaves a quest
//   (6x73): the writer SPIRE_RUN_OVER_LOBBY_DELAY after its own 'X', a partner after its own 'X' too or at the latest
//   SPIRE_RUN_OVER_PARTNER_DELAY after the first. Never to Pioneer 2 (B-E01-41); the 98 and the lobby join run the
//   guard's restore as for a Quit.

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
constexpr const char* SPIRE_BRIDGE_ROOM_CLEAR_TEXT = "Room clear! Take a step for your reward.";
// v36: the server's room clear (r251) sends its text this long after the r251, so the board text the quest shows on
// the r251 does not push the line aside (pc 2026-10-25 09:06:48: B0 sent with r251, nothing seen)
constexpr auto SPIRE_BRIDGE_ROOM_CLEAR_TEXT_DELAY = std::chrono::milliseconds(1000);
// v17: short run messages. A client writes kind << 24 | parameters to register 248 where it used to show a window_msg;
// the bridge answers with the text (B0) to that client; the wave code (leader only) goes to every client. Kinds:
//   'W' wave n/m (n << 8 | m) · 'L' life lost (last << 8 | lives left) · 'T' time warning (minutes left)
//   'P' partner wait (1: your partner is waiting, 0: waiting for your partner) · 'X' run over (rooms << 16 | seconds)
//   v36 (quest spike/stadstart b29f310), each to the writer only: 'G' pause B (0: Continue, 1: Ready) · 'U' down
//   (1: down, 0: back on its feet) · 'H' hint: close the board and take a step · 'A' pipe approach (type << 8 | room
//   number; type 0 normal, 1 elite, 2 rest) · v37 (quest 802ebe9) 'C' the run waits for a choice of this player
//   (point << 8 | room number; points 1, 2, 6, 7, 8, and 9 with leader 1 / follower 0 in the low byte; 3-5 are retired and taken without a text or a log line)
// The kind letters differ from the first byte of every 4CC above ('SPIR', 'DONE', 'ENDB', 'ROOM'), from the record
// codes ('B', 'K', 'F', 'Y'), the timer ('S'), the draw ('Q') and the lab probe ('N').
constexpr uint8_t SPIRE_BRIDGE_MSG_WAVE = 'W';
constexpr uint8_t SPIRE_BRIDGE_MSG_LIFE = 'L';
constexpr uint8_t SPIRE_BRIDGE_MSG_TIME = 'T';
constexpr uint8_t SPIRE_BRIDGE_MSG_PARTNER = 'P';
constexpr uint8_t SPIRE_BRIDGE_MSG_RUN_OVER = 'X';
constexpr uint8_t SPIRE_BRIDGE_MSG_PAUSE_B = 'G';
constexpr uint8_t SPIRE_BRIDGE_MSG_DOWN = 'U';
constexpr uint8_t SPIRE_BRIDGE_MSG_HINT = 'H';
constexpr uint8_t SPIRE_BRIDGE_MSG_APPROACH = 'A';
constexpr uint8_t SPIRE_BRIDGE_MSG_PROMPT = 'C';
// v18: records per character, kept by the server (system/spire-bridge/<serial>.<character>.records.json) instead of
// quest counters 12-14 on the card. Codes in register 248, from each client for its own character:
//   'B' run started (runs + 1) · 'K' final score (low 24 bits; held until 'F') · 'F' run finished (bit 23 won, low 16
//   bits the seconds): best score = max, and on a win won + 1 and best time = min; v24: bits 16-17 exams
//   passed, bit 18 deathless team, and SpireMeta sets the unlock and payout flags · 'Y' Records board. 'F' and 'Y' are answered with the record text (B0) to that client.
constexpr uint8_t SPIRE_BRIDGE_REC_BEGIN = 'B';
constexpr uint8_t SPIRE_BRIDGE_REC_SCORE = 'K';
constexpr uint8_t SPIRE_BRIDGE_REC_FINISH = 'F';
constexpr uint8_t SPIRE_BRIDGE_REC_BOARD = 'Y';

// v19: the run timer on the server, instead of the quest's t_timer clock. The leader writes, at its run start,
//   'S' << 24 | warning minutes before the end << 16 | time limit in seconds (r95, after Haste)
// to register 248 (warning below 80 minutes, so the code never equals 'SPIR'). The server then ticks once a second for that game (co_spawn + async_sleep, like the ENDB phases):
// at limit - warning it sends the time warning text (B0) to every client, once; at the limit, or as soon as the game
// has fewer clients than at the 'S', it sends "Time is up." / "The second player left." (B0) and a 6x77
// register 252 = SPIRE_BRIDGE_TIMER_END << 24 | cause << 16 | elapsed seconds (cause 1 time up, 2 player left; v21) to
// every client, and stops. The quest takes those seconds as its run time. The quest's
// t_timer reads r252 and goes into its end. A run-over code ('X', v17) from any client, a new 'S' or the end of the
// game stops the timer. r252 is in the free range r251-r255 (regie), outside the stored ranges; r251 is room clear.
constexpr uint8_t SPIRE_BRIDGE_TIMER_START = 'S';
constexpr uint8_t SPIRE_BRIDGE_TIMER_END = 'E';
constexpr uint8_t SPIRE_BRIDGE_TIMER_REGISTER = 252;
constexpr uint32_t SPIRE_BRIDGE_TIMER_TIME_UP = 1;
constexpr uint32_t SPIRE_BRIDGE_TIMER_PLAYER_LEFT = 2;
// Lab only: the server config key SpireRunTimerLab {"LimitSeconds", "WarningSeconds"} replaces the 'S' values (logged).
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

// v25 (spire-bossclear): boss clear on the server for a boss without if_zone_clear. Sega's q160 polls vol_opt_is_dead
// instead of if_zone_clear, so the Vol Opt room (k11) cannot use the quest's own zone clear. When a client sets
// set_flags bit 8 (defeated, 6x76) on Vol Opt phase 2 (TBoss3VoloptP02, EnemyType VOL_OPT_2) in a game running a quest
// listed in SpireBossClearQuests (and SpireBridgeQuests), the server sends the same as for a normal room clear:
// SPIRE_BRIDGE_ROOM_CLEAR_TEXT and r251 = SPIRE_BRIDGE_ROOM_CLEARED_TAG | floor << 8 | room to every client, the replay
// note and the checkpoint, once per (game, floor, room). Quests not in the list (q920, q940-q952 with the Dragon and De
// Rol Le) keep their own boss clear; nothing changes there.
void spire_bridge_check_boss_defeated(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room, EnemyType type);

// v23: room state on the server, for room transitions and checkpoints (spoor 9).
// - Checkpoint per character: every room clear the server sees (the event rule above; a boss wave counts too, without a
//   text) whose floor and room are in SpireCheckpointRooms is stored as room k in
//   system/spire-bridge/<serial>.<character id>.checkpoint.json for every client in the game (each for its own
//   character, like the records), as a bit mask of the rooms ever cleared plus the last room cleared and its quest. A
//   quest's AvailableIf / EnabledIf reads it as SPIRE_K<k> (room k cleared, 0/1) and SPIRE_MAX (highest room cleared),
//   the way Sega's challenge stages read CC_Ep1_<n>: a room quest unlocks once the room before it is cleared, and after
//   a disconnect the next room's quest is where the run continues. SpireCheckpointArrivals adds a room on the arrival
//   of a client on a floor (6x1F / 6x21 / 6F): a rest room has no fight to see, so the next room's floor is its
//   checkpoint.
// - Replay after a floor load: for the registers in SpireReplayRegisters the server keeps, per game, the latest value
//   each client synced (6x77). When a client finishes a floor load (6x1F / 6x21, and 6F) it gets, after
//   SPIRE_REPLAY_DELAY (and once more SPIRE_REPLAY_SECOND_DELAY later), the latest value of every such register written by another client, and the last room-cleared
//   r251 of that floor. A 6x77 that reached it during its own load (respawn, go_floor) is so delivered again; a value
//   it already had is unchanged by the replay. Nothing is sent if the client changed floor again in the meantime.
// - '$spire status' shows the rooms this character cleared; '$spire continue' names the room to continue at (the one
//   after the last room cleared) and the lives it had at that clear (from its 'L' codes), so after a disconnect the
//   player picks that room's quest, which its AvailableIf has unlocked. The kit follows from the class.
constexpr auto SPIRE_REPLAY_DELAY = std::chrono::milliseconds(1500);
constexpr auto SPIRE_REPLAY_SECOND_DELAY = std::chrono::milliseconds(3500); // after the first send

// v26 follow the leader (SpireFollowLeader, regie 2026-10-24 Marck's choice A): a room starts as soon as the leader is
// in it, the partner never waits for the other. When the leader client loads a floor (6x1F / 6x21 / 6F) other than
// Pioneer 2, the server warps every other client of the game that is not on that floor there (6x94, the $warpall
// form), after DelayMs and only if the leader is still on that floor. Never to floor 0: arriving on Pioneer 2 writes
// the Lv1 overlay to the card (B-E01-41).
// v29 (regie 2026-10-24, B-E01-52): a partner that still stands in the city (floor SPIRE_FOLLOW_CITY_FLOOR) is never
// warped: it takes the Ragol warp itself, as in a Sega quest (route 1, q920/q929). The follow stays for the room
// transitions during the run. Also v29: the lives a client reported ('L' codes) are forgotten at every quest start,
// so LastLives in the checkpoint file can only come from the current run.
constexpr uint32_t SPIRE_FOLLOW_CITY_FLOOR = 0;
// Audit S-12: a client that is still loading (Client::Flag::LOADING) is never warped. Only the flag counts, not a floor
// number: Client::floor starts at 0x0F, but 0x0F is a real floor in Ep2 (Gol Dragon).

// Called at every quest start (on_quest_loaded) for each client of the game. Never throws.
void spire_checkpoint_run_started(std::shared_ptr<const Client> c);

// The two decisions above as plain functions, so 'newserv spire-follow-check' and 'spire-lives-check'
// (tests/spire-follow-city.test.sh) measure them without a client. The lives store is keyed by the Client's address
// and never dereferences it.
// The action-stream part of the v19 room clear check, for the static tests: returns why a finished event's post-wave
// actions still hold the room (a construct of an undefeated wave in the room, or a trigger of an unfinished event), or
// "" if they do not.
std::string spire_room_actions_hold(const void* action_stream, size_t action_stream_size, uint16_t room,
    const std::function<bool(uint16_t wave)>& wave_undefeated,
    const std::function<bool(uint32_t event_id)>& event_unfinished);
// Audit S-6: the meta register file of one character (character id as in spire_guard_character_id)
std::string spire_bridge_registers_filename(uint32_t serial_number, const std::string& character_id);
// The same for a character's disp data; "" if there is none (the registers then read as 0 and are not stored)
std::string spire_bridge_registers_filename_for(uint32_t serial_number, const PlayerDispDataV4* disp);
bool spire_follow_should_warp(uint32_t partner_floor, uint32_t leader_floor, bool partner_loading = false);
// Audit S-12: true if a partner the leader's floor load does not warp is skipped only because it is still loading, so
// it is remembered and follows the leader after its own floor load
bool spire_follow_partner_pending(uint32_t partner_floor, uint32_t leader_floor, bool partner_loading);
void spire_checkpoint_lives_note(const Client* key, uint32_t lives);
bool spire_checkpoint_lives_forget(const Client* key);
uint32_t spire_checkpoint_lives_for(const Client* key); // 0 = no lives reported since the quest start

// Called from the 6x77 handler for every quest register write in a game, after the bridge (forwarded or not). Never throws.
void spire_replay_note_register(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint8_t register_number, uint32_t value);

// Called when a client has loaded a floor (6x1F, 6x21) or the game (6F). Never throws.
void spire_replay_after_floor_load(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l);

// '$spire status' / '$spire continue': returns the text for the player. Throws std::runtime_error with a short message
// on bad input.
std::string spire_checkpoint_command(std::shared_ptr<Client> c, const std::string& args);

// The rooms this client's character has cleared (bit k-1 = room k), from its checkpoint file; 0 if none or unknown.
uint32_t spire_checkpoint_mask_for_client(std::shared_ptr<const Client> c);

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
// - v39: a run copy is never told apart by level or EXP (it may be stronger than the real character): under an open
//   marker only data equal to the snapshot counts as the real character, and under AwaitingSave the snapshot follows the
//   98 of a Normal or Solo game (see SpireGuardRunStart below).
// - v40: that first 98 of a Normal or Solo game under AwaitingSave alone also closes AwaitingSave (and drops the
//   overlay fingerprint): the client writes its card when it leaves the game, so the card holds the real character
//   from then on and the next login needs no push. Under RunInProgress or RestorePending nothing follows, and the 98 of
//   a Spire (Battle) game never closes AwaitingSave.

// Called from on_quest_loaded for each client, before its battle overlay is created. Does nothing unless the client is
// GC V3 with a GC license, the lobby is a Battle game whose quest is listed in SpireBridgeQuests and uses replace_char.
// Never throws.
// Returns false (v12) if the client must not get the battle overlay: the guard applies but could not store the
// snapshot. The caller then drops the client instead of creating the overlay.
bool spire_guard_begin_run(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l);

// v21: at every Battle game join (also without a quest) the guard stores the real character, like a run start; no
// snapshot means the connection is dropped (fail closed).
void spire_guard_battle_join(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l);

// v30: a Battle game without a quest. A GC client that joins a Battle game without one is watched: if the game still
// has no quest SPIRE_GUARD_NO_QUEST_SECONDS later (and nobody is picking one at the counter), the server tells it and
// sends it back to the lobby (ExitAnywhere, as $exit does). Its join snapshot (v21) becomes a pending restore at the 98
// and the lobby restore puts the real character back, so no item is lost. A client that cannot run functions only gets
// the text and keeps the guard. spire_guard_no_quest_action is the decision, kept free of state for the static tests.
constexpr uint32_t SPIRE_GUARD_NO_QUEST_SECONDS = 180;

// The guard's snapshot file of one character (serial number plus character, see spire_guard_character_id).
struct SpireGuardSnapshot {
  // Set at quest load; a 98 or the next login turns it into restore_pending (v8)
  bool run_in_progress = false;
  // Cleared only when the 61 requested right after the restore push shows the snapshot's state (v8, v32, v38)
  bool restore_pending = false;
  // v10: set instead of clearing restore_pending when the lobby restore is verified. The real character is then only in
  // the client's memory until the client saves it, so the card may still hold the run copy. v39: cleared only at a
  // login whose 61 equals the snapshot (spire_guard_same_state); any other login turns it back into restore_pending.
  // While it is set, the snapshot follows the real character's 98 from a Normal or Solo game (real progress); v40: that
  // 98 also clears it, since the client writes its card when it leaves the game.
  bool awaiting_save = false;
  // v12: what the client had in memory right before the lobby restore (from its 30): the run copy. v39: only for the
  // log; the login decision no longer uses it (it is not a complete record of the run copies a card can hold).
  bool has_overlay = false;
  uint32_t overlay_level = 0;
  uint32_t overlay_exp = 0;
  uint32_t overlay_meseta = 0;
  uint32_t overlay_num_items = 0;
  uint32_t quest_number = 0;
  uint64_t snapshot_time_usecs = 0;
  bool has_character = false;
  PlayerInventory inventory;
  PlayerDispDataV4 disp;
};

// v39 (regie, kracht-voorbij-normaal.md section 6): how the guard tells the real character from a run copy. Never by
// level or EXP: a run copy may be stronger than the real character (start level, training rooms). The criterion is
// the server's own state. While a marker is open (RunInProgress, RestorePending, AwaitingSave) the card or the server's
// character data may hold a run copy, so that data counts as the real character only if it equals the snapshot
// (level, EXP, meseta, items and material counters). The snapshot itself only takes data the server knows is real:
// the data at a run start without an open marker, and the 98 of a Normal or Solo game while only AwaitingSave is set
// (the client has had the verified real character in memory since the lobby restore).
enum class SpireGuardRunStart {
  KEPT_OPEN_RUN, // RunInProgress or RestorePending: the stored snapshot stays, nothing changes
  KEPT_UNCONFIRMED, // AwaitingSave and the data differs from the snapshot: the stored snapshot stays, marker set
  FRESH, // no open marker, or AwaitingSave with equal data: the snapshot takes the data, marker set
};
// The run-start step of spire_guard_take_snapshot on the stored snapshot, given the server's character data.
SpireGuardRunStart spire_guard_run_start_apply(SpireGuardSnapshot& snapshot, const PlayerInventory& inventory,
    const PlayerDispDataV4& disp, uint32_t quest_number, uint64_t now_usecs);
// A login 61 under AwaitingSave only (no other marker): true = confirmed (marker cleared, no push); false = the card may
// hold a run copy, RestorePending is set and the login pushes the snapshot.
bool spire_guard_awaiting_save_login_apply(
    SpireGuardSnapshot& snapshot, const PlayerInventory& inventory, const PlayerDispDataV4& disp);
// The 98 of a Normal or Solo game under AwaitingSave only: the snapshot takes the real character's data and (v40)
// AwaitingSave is closed (awaiting_save and has_overlay false): the client writes its card when it leaves the game.
// Returns false (snapshot unchanged) if another marker is open, AwaitingSave is not set or the snapshot has no
// character.
bool spire_guard_follow_real_progress(
    SpireGuardSnapshot& snapshot, const PlayerInventory& inventory, const PlayerDispDataV4& disp);
// Called from the 98 handler after the client's data was taken into the server's character data (v39). Applies
// spire_guard_follow_real_progress if spire_guard_end_run saw the client leave a Normal or Solo game under
// AwaitingSave. Never throws.
void spire_guard_after_98(std::shared_ptr<Client> c);

// v31: true if both inventories hold the same items (data1 + data2, any order, item IDs ignored) and the same meseta.
// The login confirmation of a lobby restore requires this (spire_guard_same_state, v39).
bool spire_guard_same_inventory(const PlayerInventory& a, uint32_t a_meseta, const PlayerInventory& b, uint32_t b_meseta);
// v32: level, EXP, meseta and items equal. The two decisions below are kept free of state for the static tests: a run
// start under AwaitingSave keeps the stored snapshot unless the character data equals it; the 61 after a restore push
// verifies the restore only with the snapshot's items and meseta next to its level and EXP.
bool spire_guard_same_state(const PlayerInventory& inventory, const PlayerDispDataV4& disp,
    const PlayerInventory& snapshot_inventory, const PlayerDispDataV4& snapshot_disp);
// v39: no level argument; an open AwaitingSave marker with data unequal to the snapshot keeps the snapshot
bool spire_guard_run_start_keeps_snapshot(bool awaiting_save, bool same_state);
bool spire_guard_push_verified(bool same_stats, bool same_items);
// v38: the material usage counters equal (Power, Mind, Evade, Def, Luck in items[8..12].extension_data2; HP and TP in
// hp_from_materials / tp_from_materials); part of spire_guard_same_state and of both restore checks, like the items.
bool spire_guard_same_materials(const PlayerInventory& a, const PlayerInventory& b);
std::string spire_guard_materials_str(const PlayerInventory& inv);
constexpr uint32_t SPIRE_GUARD_NO_QUEST_RECHECK_SECONDS = 30;
enum class SpireNoQuestAction { NONE, WAIT, EXIT, TELL };
SpireNoQuestAction spire_guard_no_quest_action(
    bool client_in_game, bool battle_game, bool has_quest, bool quest_selection_in_progress, bool can_run_functions);

// Called from the 98 handler before the client is removed from the game. Turns RunInProgress into RestorePending.
// Never throws.
void spire_guard_end_run(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l);

// Called from on_84 right after a client that just left a game was added to a lobby (v9). If RestorePending is set,
// runs the same restore as the login path, at once, in the lobby. On success RestorePending is cleared; on any failure
// the connection is dropped and the marker stays, so the login restore remains the safety net. Never runs at the same
// time as another restore for the same client. Never throws.
asio::awaitable<void> spire_guard_restore_in_lobby(std::shared_ptr<Client> c);
// v33: called at every 99 (the client's answer to a B1, sent only once the lobby is loaded); starts a lobby restore
// that waits for it. Cheap no-op for every other client.
void spire_guard_lobby_loaded(std::shared_ptr<Client> c);

// Called before a client joins or creates a game (v10). Returns false, after sending the client a short message, if a
// restore is running for the client or RestorePending is set; the caller must then not let the client into a game.
// Never throws.
// tell_client=false skips the message (used at login, v12).
bool spire_guard_game_join_allowed(std::shared_ptr<Client> c, bool tell_client = true);

// Called from enable_save_if_needed right before 97 is sent. Returns true if 97 may be sent; false if the caller must
// not send 97 and must drop the login. Never throws.
asio::awaitable<bool> spire_guard_restore_if_needed(std::shared_ptr<Client> c);

// Spire BBA TX guard (see SpireBBATxGuard.hh): at login, before the 97, send GC EU v1.0 clients the client function that
// null-checks the BBA driver's TX-done callback. No-op for every other version, when SpireBBATxGuardPatch is false, or
// when the client cannot run functions. Logs the client's answer (written / already present / no match).
asio::awaitable<void> spire_bba_tx_guard_if_needed(std::shared_ptr<Client> c);

// v35: a text for the client during a Spire run as one line: line breaks become spaces, trailing spaces go.
std::string spire_ticker_line(const std::string& text);
// v35: the text for a short-message code (SPIRE_BRIDGE_MSG_*), one line each; "" for any other value.
std::string spire_bridge_message_text(uint32_t value);
// v41 (B-E01-96, B-REGIE-9-1): lives are the quest's counter r172, never a Scape Doll; the Battle respawn to the city
// is always off and the quest stands a dead player with lives left up on the spot. Its 'L' code (lives left in the low
// byte) gets this log line: "Spire lives: death of serial S revived on the spot, lives N->N-1, no respawn to city".
std::string spire_lives_revive_log_line(uint32_t serial_number, uint32_t value);
// v41: a Scape Doll (03 09) never drops in a Spire quest. spire_drop_filter clears a server drop (SERVER_* drop modes)
// of a forbidden item in a quest listed in SpireBridgeQuests, logs it and returns true; false for anything else. The
// quest's rule block (ba_forbid_scape_dolls 1) covers the client drop mode and the Battle bag.
bool spire_drop_is_forbidden(const ItemData& item);
bool spire_drop_filter(std::shared_ptr<const Client> c, std::shared_ptr<const Lobby> l, ItemData& item);
// v36 (regie, pc 2026-10-25 09:26: q921 room 10 wave 2 stayed open, E-02C sat in a warp and was never hit): safety net
// for an enemy nobody can reach. In a quest listed in SpireBridgeQuests (GC V3), when a defeat leaves a wave held only by
// enemies that were never hit (no hit claim, no damage, no 6x0A update; never a boss), the server waits
// SPIRE_UNHIT_ENEMY_SECONDS. If no other enemy of the wave died and none of them was hit meanwhile, it counts them as
// defeated (set flag 8 on the server's map state only), logs "E-xxx never hit 30 s after the rest of wave n of room
// r; counted as defeated", and runs the normal wave check again (events finished, then the room clear). Enemies
// that were hit are never touched. Not after the run ended.
constexpr auto SPIRE_UNHIT_ENEMY_SECONDS = std::chrono::seconds(30);
struct SpireWaveEnemy {
  bool defeated = false;
  bool ever_hit = false;
  bool is_boss = false;
};
// The decision, free of state for the static tests: at least one enemy defeated, at least one not, and every one not
// defeated never hit and no boss.
bool spire_wave_held_only_by_unhit(const std::vector<SpireWaveEnemy>& enemies);
// Called from the 6x76 wave check when a defeat leaves the wave unfinished; recheck is that wave check (was_defeat
// false). Never throws.
using SpireWaveCheck = void (*)(std::shared_ptr<Client>, std::shared_ptr<Lobby>, uint8_t floor, uint16_t room,
    uint16_t wave_number, bool was_defeat);
void spire_bridge_watch_unhit_wave(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room,
    uint16_t wave_number, SpireWaveCheck recheck);

// v35: the lobby return after 'X' (see above). spire_run_over_exit_action is the decision, free of state for the static
// tests: nothing if the client left the game or was already sent; 6x73 while a quest runs; ExitAnywhere without a
// quest if the client runs functions; else only the log (the client leaves itself).
constexpr auto SPIRE_RUN_OVER_LOBBY_DELAY = std::chrono::seconds(6);
constexpr auto SPIRE_RUN_OVER_PARTNER_DELAY = std::chrono::seconds(20);
enum class SpireRunOverExit { NONE, EXIT_QUEST, EXIT_ANYWHERE, TELL };
SpireRunOverExit spire_run_over_exit_action(bool same_game, bool quest_in_progress, bool can_run_functions, bool already_sent);
// v35: called from on_84 once a client that left a game is in a lobby (after the guard's lobby restore). Texts the
// bridge held during the run (the records at the run end) go out SPIRE_LOBBY_TEXT_DELAY later, if the client is still
// in a lobby then. Never throws.
constexpr auto SPIRE_LOBBY_TEXT_DELAY = std::chrono::seconds(3);
void spire_deliver_lobby_texts(std::shared_ptr<Client> c);

// Shared with SpireMeta (v22): the character id of a 61 (name, class, section ID) and the crash-safe file write.
std::string spire_guard_character_id(const PlayerDispDataV4& disp);
void spire_bridge_write_durable(const std::string& filename, const std::string& data);
