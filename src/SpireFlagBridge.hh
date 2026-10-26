#pragma once

#include <stdint.h>

#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <vector>

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
//   in SpireBridgeQuests (v5; a missing or empty list records nothing), is recorded per character (SpireMeta, v22; audit S-6) in
//   system/spire-bridge/<serial>.<character id>.meta.json; the v5-v21 per-card file <serial>.json is never read. In such
//   a game the write is not forwarded to the other clients. Both set and clear are recorded; the last write wins.
// - The stored flags are sent to the same client as 6x75 commands (set or clear) only in a game that runs a Spire quest
//   (spire_game_is_spire: SpireBridgeQuests or SpireGuardQuests; regie pso-regie-9) or a quest listed in
//   SpireMetaQuests (the E06 payout q925, which also has its clears of 0x3A0-0x3A2 recorded): when such a quest loads
//   (set_lobby_quest, before the quest files and the AC barrier) and when the client joins a game that already runs
//   one. A normal game (Normal, Battle or Solo without a Spire quest) gets nothing. Stored entries are never erased by
//   delivery.
// - Spire meta registers (v4): in the quests listed in the server config key SpireBridgeQuests (a JSON list of quest
//   numbers; any game mode), 6x77 writes to registers 232-239 and 243-247 by a GC V3 client are merged into a file per
//   character (audit S-6: <serial>.<character id>.registers.json, like the flags and records) (232/233 max, 234 min with 0 ignored, 235-239 and 245 OR, 243/244/246/247 last write wins) and
//   are not forwarded, so a co-op partner never takes them over. A write of 'SPIR' to register 248 is answered to that
//   client only: one 6x77 per stored register (0 if none), then 248 = 'DONE'. Registers 240-242 are left alone (Sega's
//   q101 and Schtserv commands use them). If SpireBridgeQuests is missing or empty, the register bridge takes nothing:
//   every register write is handled exactly as without the bridge. The flag part above records and delivers only in listed
//   quests (v5, regie pso-regie-9): a quest that reads the flags in a normal game (E06 payout q925) goes in SpireMetaQuests.
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

// v44 savestate (plan-checkpoints-savestates 2026-10-25 as changed by critic-savestates; quest B-E01-178 to -183): the
// state of a run at the start of a room, per character, so a run continues after a logout or a crash.
// - Run per game: the first 'J' or 'B' in a game sets the game's run: the run of the leader's savestate when the leader
//   resumes from it (SpireFollowLeader.LeaderClient, else lobby client 0), else a new random run id. A quest start
//   forgets it. 'B' (records) carries the quest's start room: 'B' << 24 | resumed << 16 | K0 << 8 | start room (low 16
//   bits 0: a quest from before v44, counted as before).
// - Write: at every room start after the first room (k > K0) each client writes SPIRE_SAVESTATE_NUM_VALUES values to
//   SPIRE_BRIDGE_DRAW_DATA_REGISTER (in the SpireSavestateValue order) and then 'V' << 24 | count to register 248. The
//   server takes the items of the client's run copy (the Battle overlay; never the real character: without an overlay
//   nothing is written) without its mags (type 2), and replaces <serial>.<character id>.savestate.json with them, the
//   values, the game's run id, the episode, the quest and the client's run start room and practice bit. Nothing is
//   written after the run ended in the game or before the client's 'B'.
// - Never consumed: a savestate stays until the next one replaces it or the run truly ends. Every run end in the game
//   (the first 'X', 'F' or run timer end) deletes the savestate of each client in the game whose run id is the game's,
//   unless the end is "player left" (run timer) or the game has fewer clients than its run had: a crash of one player
//   must not cost the other one his savestate.
// - Resume: a room quest that starts after K0 writes 'J' << 24 | variant << 8 | k at its kit point and waits for 'DONE'.
//   The server always answers that client: register SPIRE_RESUME_ANSWER_REGISTER = SPIRE_RESUME_ANSWER_TAG (fresh) or
//   SPIRE_RESUME_ANSWER_TAG | SPIRE_RESUME_ANSWER_RESUME | item count, the values in SPIRE_RESUME_HEADER (value index -> register) before it, then
//   r248 = 'DONE'. It resumes only from a savestate of this episode, room and variant, with the game's run id (the
//   leader's), and never after the run ended. Then 'J' << 24 | 1 << 16 | i asks for item i: data1 in
//   SPIRE_RESUME_ITEM_FIRST_REGISTER.. (12 registers), SPIRE_RESUME_ITEM_ACK_REGISTER = i + 1 (| SPIRE_RESUME_ITEM_GLASS
//   for the run's Glass item), then 'DONE'; ack 0 if there is no item i. The items come from the copy loaded at the
//   start answer. The quest creates them with item_create2 into its own run copy.
// - The run timer 'S' of a resumed game starts the saved seconds back, and the draw 'Q' is seeded per (run id, room,
//   n-th draw of the client in the room, inputs).
constexpr uint8_t SPIRE_BRIDGE_SAVESTATE_WRITE = 'V';
constexpr uint8_t SPIRE_BRIDGE_RESUME = 'J';
enum SpireSavestateValue : size_t {
  SPIRE_SS_K = 0, // the room starting (r150)
  SPIRE_SS_VARIANT, // r131 & 3: 0 normal, 1 elite, 2 rest
  SPIRE_SS_RELICS_LO, // r170
  SPIRE_SS_RELICS_HI, // r171
  SPIRE_SS_PACTS, // r204
  SPIRE_SS_LIVES, // r172
  SPIRE_SS_ELAPSED, // seconds since the run start
  SPIRE_SS_DEATHS, // r179
  SPIRE_SS_SHOP_LIVES, // r144
  SPIRE_SS_MESETA, // get_slot_meseta
  SPIRE_SS_STATE, // r182 rewards state
  SPIRE_SS_BOSS1, // r177
  SPIRE_SS_BOSS2, // r178
  SPIRE_SS_TAGS, // r180 tag counters
  SPIRE_SS_PICK0, // r183-r186 pick counters
  SPIRE_SS_PICK1,
  SPIRE_SS_PICK2,
  SPIRE_SS_PICK3,
  SPIRE_SS_GLASS, // r138 item id of the Glass run item, 0 = none (stored as an item index)
  SPIRE_SAVESTATE_NUM_VALUES,
};
constexpr uint8_t SPIRE_RESUME_ANSWER_REGISTER = 41;
// The answer always carries 'J' in its top byte, so the quest tells it from the 0 it set before asking (another
// request's 'DONE' on r248 can come first)
constexpr uint32_t SPIRE_RESUME_ANSWER_TAG = static_cast<uint32_t>(SPIRE_BRIDGE_RESUME) << 24;
constexpr uint32_t SPIRE_RESUME_ANSWER_RESUME = 0x100;
constexpr uint8_t SPIRE_RESUME_ITEM_FIRST_REGISTER = 20; // r20-r31
constexpr uint8_t SPIRE_RESUME_ITEM_ACK_REGISTER = 44;
constexpr uint32_t SPIRE_RESUME_ITEM_GLASS = 0x100;
// The values the start answer sends, as (value index, register); k, variant and the Glass id are not sent
constexpr std::pair<size_t, uint8_t> SPIRE_RESUME_HEADER[] = {
    {SPIRE_SS_ELAPSED, 48}, {SPIRE_SS_MESETA, 49}, {SPIRE_SS_LIVES, 50}, {SPIRE_SS_RELICS_LO, 51},
    {SPIRE_SS_RELICS_HI, 52}, {SPIRE_SS_PACTS, 53}, {SPIRE_SS_DEATHS, 54}, {SPIRE_SS_SHOP_LIVES, 55},
    {SPIRE_SS_STATE, 56}, {SPIRE_SS_BOSS1, 57}, {SPIRE_SS_BOSS2, 58}, {SPIRE_SS_TAGS, 32}, {SPIRE_SS_PICK0, 33},
    {SPIRE_SS_PICK1, 34}, {SPIRE_SS_PICK2, 35}, {SPIRE_SS_PICK3, 36}};

struct SpireSavestate {
  bool exists = false;
  uint32_t run_id = 0;
  uint8_t episode = 0;
  uint32_t quest_number = 0;
  uint8_t start_room = 0;
  bool practice = false;
  std::vector<uint32_t> values; // SPIRE_SAVESTATE_NUM_VALUES, SpireSavestateValue order
  std::vector<ItemData> items;
  int32_t glass_index = -1; // index in items of the Glass run item, -1 = none
  uint64_t time_usecs = 0;
};
// The pure parts, for the static tests:
// The items a savestate takes from a run copy: every item but empty slots and mags, in inventory order; *glass_index is
// the position of the item whose id is glass_item_id (0: none) among them, or -1.
std::vector<ItemData> spire_savestate_items(const PlayerInventory& inventory, uint32_t glass_item_id, int32_t* glass_index);
// Whether a 'V' may write: the client has a run copy (overlay), began its run ('B') and the run has not ended; the count
// matches.
bool spire_savestate_write_allowed(bool run_ended, bool has_overlay, bool began_run, size_t num_values, uint32_t count);
// Whether a run end deletes the game's savestates: every true end (dead, time up, Give up, won), also when a client of
// the run is gone by then; only the run timer's "player left" (the disconnect itself, no true end) keeps them.
bool spire_savestate_end_deletes(bool player_left_end);
// The savestate files a true run end looks at: the files the run's clients wrote or were answered from (also of a
// client gone by then) and the files of the clients still in the game; each once, never "".
std::vector<std::string> spire_savestate_end_files(const std::set<std::string>& run_files,
    const std::vector<std::string>& present_files);
// Whether a run end deletes this savestate: only one of the game's run.
bool spire_savestate_is_of_run(const SpireSavestate& ss, uint32_t run_id);
// Whether a savestate fits a room quest's start: same episode, room and variant.
bool spire_savestate_matches(const SpireSavestate& ss, uint8_t episode, uint8_t k, uint8_t variant);
// The resume answer for one client: resume only if the game's run is a resume, the client's own savestate fits the
// quest and has the game's run id, and the run has not ended.
bool spire_resume_client(bool run_ended, bool game_resumed, uint32_t game_run_id, bool own_matches, uint32_t own_run_id);
// The ack of an item request: index + 1 (| SPIRE_RESUME_ITEM_GLASS for the Glass item), 0 = no item sent (the run
// ended, this client was not answered with a resume, or no such item).
uint32_t spire_resume_item_ack(bool run_ended, bool answered_resume, uint32_t index, size_t num_items, int32_t glass_index);
std::string spire_savestate_to_json(const SpireSavestate& ss, uint32_t serial_number);
SpireSavestate spire_savestate_from_json(const std::string& data); // throws on a malformed file
// 'B': whether the run counts as a practice run (start room not K0, or resumed from a practice savestate), and whether
// it counts as a new run in the records (neither practice nor resumed). start_k 0 = a 'B' from before v44.
bool spire_run_is_practice(uint8_t start_k, uint8_t first_k, bool resumed, bool savestate_practice);
bool spire_run_counts_as_new(uint8_t start_k, bool resumed, bool practice);
// A practice run stores no quest flag of the bridged range (relics seen are unlocks too)
bool spire_practice_blocks_flag(bool practice, uint16_t flag_num);
// The one ticker line at 'B': "Run resumed at room k: ..." (with the savestate), "Practice run: ...", or "" for a run
// from its first room
std::string spire_run_start_ticker(bool resumed, bool practice, const SpireSavestate* ss);
// The second ticker line of a resumed run (regie 2026-10-26): a mag stays out of the savestate (spire_savestate_items),
// and the run copy also holds the real mag, so the server cannot tell a mag found in the run from it; the line comes
// on every resume, "" otherwise. It follows the start line after SPIRE_RESUME_MAG_TEXT_DELAY, so it does not push that
// line aside (as the room clear text, v36).
constexpr const char* SPIRE_RESUME_MAG_TEXT = "A mag found in this run does not come back.";
constexpr auto SPIRE_RESUME_MAG_TEXT_DELAY = std::chrono::milliseconds(4000);
std::string spire_run_resume_mag_ticker(bool resumed);
// The seed of the draw 'Q' of a run: run id, room, n-th draw of the client in that room and the input values.
uint64_t spire_draw_seed(uint32_t run_id, uint32_t k, uint32_t nth, const std::vector<uint32_t>& values);
// '$spire continue' with a savestate (SPIRE_SS_* values); for the static tests
std::string spire_savestate_continue_text(const SpireSavestate& ss);

// Called from the 6x75 handler for every valid quest flag write. Does nothing unless the client is GC V3, the lobby is
// a game running a quest listed in SpireBridgeQuests (or a payout-flag clear in a SpireMetaQuests quest), the client
// has a GC license, and the flag is in the bridged range. Never throws.
void spire_bridge_record_flag(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, Difficulty difficulty, uint16_t flag_num, bool is_set);

// Called when a client has finished loading into a game (6F). Does nothing unless the client is GC V3, the lobby is a
// game, and there are stored entries for the client's GC license serial. Never throws.
void spire_bridge_deliver_flags(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l);

// Audit S-6: false for a quest flag write in 0x380-0x3AF in a game where the meta flags apply (a Spire quest or a
// SpireMetaQuests quest): the 6x75 handler then neither forwards it nor keeps it in the game's flag state, so a partner
// never takes over the writer's unlocks or payout. True otherwise (and on any error). Never throws.
bool spire_bridge_flag_is_shared(std::shared_ptr<const Client> c, std::shared_ptr<const Lobby> l, uint16_t flag_num);

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
// The 6x76 route above is for VOL_OPT_2 only (free of state for the tests). Dark Falz (k15, plan-ruins-darkfalz section
// 7) does not use it: its 0x1FD Darvants are children of the Falz root, a Darvant's 6x76 is redirected to the root, so
// every Darvant would clear the room early. Its clear is the final-phase quest flag the client sets when Falz is dead,
// the flag newserv's own final boss drop keys on: 0x35 on Normal (no third phase, DARK_FALZ_2) and 0x37 on Hard and up
// (DARK_FALZ_3). spire_bridge_dark_falz_final_phase gives that phase or NONE; spire_bridge_check_dark_falz_flag sends
// the same room clear, replay note and checkpoint as Vol Opt, once per (game, floor, room), for the room of the Falz
// set entry (on_set_quest_flag calls it for a set flag on area 0x0E).
bool spire_bridge_is_server_clear_boss(EnemyType type);
EnemyType spire_bridge_dark_falz_final_phase(Difficulty difficulty, uint16_t flag_num);
void spire_bridge_check_dark_falz_flag(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, Difficulty difficulty, uint16_t flag_num);

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
// The room index k a clear of this room on this floor stores (SpireCheckpointRooms; Room -1 is any room of the floor, the
// Ep1 boss floors 11 Dragon, 12 De Rol Le, 13 Vol Opt and 14 Dark Falz; Ep2 floor 14 Barba Ray), or 0 if the room is not listed. Free of state for the tests.
uint8_t spire_checkpoint_room_k(
    const std::map<std::pair<uint8_t, int32_t>, uint8_t>& rooms, uint8_t floor, uint16_t room);
// The same for a game of this episode: the rooms of SpireCheckpointRooms with that Episode (missing: 1), 0 if none.
uint8_t spire_checkpoint_room_k(const std::map<uint8_t, std::map<std::pair<uint8_t, int32_t>, uint8_t>>& rooms_by_episode,
    Episode episode, uint8_t floor, uint16_t room);
// The room index k an arrival on this floor in a game of this episode stores (SpireCheckpointArrivals), or 0.
uint8_t spire_checkpoint_arrival_k(
    const std::map<uint8_t, std::map<uint8_t, uint8_t>>& arrivals_by_episode, Episode episode, uint8_t floor);
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
// - v43: after a verified lobby restore the lobby is refreshed like a normal lobby join: the restored client gets only
//   a 67 (no 69 for its own slot; undone in v45), the other players get the 69 first and the 68
//   SPIRE_GUARD_LOBBY_REFRESH_GAP later, never in the same frame. The $loadchar refresh (69 to all, then 67 and 68 at once) left the restored
//   player invisible on the partner's screen (2026-10-25 20:46:49, BidX after a run in q921 with M4ia in the lobby).
// - v45: the restored client itself gets the 69 for its own slot again, directly before its 67, as $loadchar sends it
//   (spire_guard_refresh_self_commands). The v43 67 without that 69 was followed by a client crash in the lobby
//   18 s later (2026-10-26 11:44:33, BidX alone in the lobby after q921); the $loadchar order (69, C5, 67 for the
//   restored client) ran 11 times on the pc on 2026-10-25 without a crash, 6 of them alone in the lobby. The other
//   players keep the v43 order (69, the gap, 68).
// - v46: undoes v45 and v43's 67 for the restored client: it gets no 69 and no 67 at all, since the function call (B2)
//   already set its character in memory. v45's 69 and 67 left Kamako frozen and invisible in the lobby, no 6x3E/3F
//   for 5 min (2026-10-26 12:39:05, lab, alone in the lobby). Only the other players get the refresh (69, gap, 68).

// Called from on_quest_loaded for each client, before its battle overlay is created. Does nothing unless the client is
// GC V3 with a GC license, the lobby is a Battle game whose quest is listed in SpireBridgeQuests and uses replace_char.
// Never throws.
// Returns false (v12) if the client must not get the battle overlay: the guard applies but could not store the
// snapshot. The caller then drops the client instead of creating the overlay.
bool spire_guard_begin_run(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l);

// v21 (narrowed by regie pso-regie-9, 2026-10-25): a Battle game join takes no snapshot and sets no marker any more; it
// only starts the no-quest watcher (v30). Measured on the pc 13:05-13:08: a q0 Battle join set RunInProgress for BidX
// (normal play, disconnect without a 98, next login pushed back an older snapshot: progress lost) and for Wakiru.
void spire_guard_battle_join(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l);

// Regie pso-regie-9: what a run start does. MARKER only for a Battle game whose quest uses replace_char and is guarded
// (SpireGuardQuests); spire_guard_begin_run is called from on_quest_loaded right before create_battle_overlay, so the
// marker is on disk before the overlay exists. A protected character (SpireBlockedCharacters) gets no marker and no
// overlay (REFUSE: the caller drops it). Everything else, a Battle game without a quest included: NONE.
enum class SpireGuardStart { NONE, MARKER, REFUSE };
SpireGuardStart spire_guard_start_action(
    bool battle_game, bool has_quest, bool replace_char, bool guarded_quest, bool protected_character);

// Regie pso-regie-9: whether a snapshot's markers make the guard act (login restore, lobby restore, game join refusal).
// Never for a protected character: an old marker file of one is logged and ignored, never restored onto it.
bool spire_guard_marker_open(bool protected_character, bool run_in_progress, bool restore_pending, bool awaiting_save);

// v30: a Battle game without a quest. A GC client that joins a Battle game without one is watched: if the game still
// has no quest SPIRE_GUARD_NO_QUEST_SECONDS later (and nobody is picking one at the counter), the server tells it and
// sends it back to the lobby (ExitAnywhere, as $exit does). Since regie pso-regie-9 the join takes no snapshot, so
// nothing is restored afterwards. A client that cannot run functions only gets the text. spire_guard_no_quest_action
// is the decision, kept free of state for the static tests.
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
// v43: the lobby refresh after a lobby restore. The 69 goes to every other client in the lobby (the restored client's
// own 69 follows after the gap, v45); after the gap the 68 goes only to those that got the 69 and are still in that
// lobby (a client that joined in between already has the restored character in its 67). Ids are Client::id.
constexpr auto SPIRE_GUARD_LOBBY_REFRESH_GAP = std::chrono::milliseconds(300);
std::vector<uint64_t> spire_guard_refresh_leave_targets(const std::vector<uint64_t>& in_lobby, uint64_t self_id);
std::vector<uint64_t> spire_guard_refresh_join_targets(
    const std::vector<uint64_t>& told_leave, const std::vector<uint64_t>& in_lobby_now, uint64_t self_id);
// v46: the commands the restored client gets in the refresh: none (v45 sent 0x69 and 0x67 and froze the client).
std::vector<uint8_t> spire_guard_refresh_self_commands();

// Called before a client joins or creates a game (v10). Returns false, after sending the client a short message, if a
// restore is running for the client or RestorePending is set; the caller must then not let the client into a game.
// Never throws.
// tell_client=false skips the message (used at login, v12).
bool spire_guard_game_join_allowed(std::shared_ptr<Client> c, bool tell_client = true);

// Called from enable_save_if_needed right before 97 is sent. Returns true if 97 may be sent; false if the caller must
// not send 97 and must drop the login. Never throws.
asio::awaitable<bool> spire_guard_restore_if_needed(std::shared_ptr<Client> c);

// "$kitlobby [n]" (regie, 2026-10-25): in the lobby, the player's own character is brought to level n (default 200)
// with the level table's EXP and base stats for its class at that level (PSOBBCharacterFile::recompute_stats, as
// $edit level does on BB), and the class's $kit materials (DataIndex::spire_kit_material_targets) are put in its material
// counters (never lower than they are, within the shared cap: 150 for an android, 250 otherwise; HP and TP at most 125). The character
// is pushed to the client with the guard's restore push (full character 30, SetExtendedPlayerInfo, verifying 61).
constexpr uint32_t SPIRE_KIT_LOBBY_DEFAULT_LEVEL = 200;
// $kitlobby works only in the lobby, where cheat mode is never on (CheatModeBehavior OnByDefault applies to games),
// so it is gated on spire_kit_lobby_refused and the guard marker alone, never on cheat mode (regie pso-regie-10)
constexpr bool SPIRE_KIT_LOBBY_NEEDS_CHEAT_MODE = false;
// Why $kitlobby is refused, or null if it may run. Free of state for the static tests.
const char* spire_kit_lobby_refused(bool protected_character, bool is_game, bool spire_run, bool restore_pending);
// Sets level (1-based, clamped to 1..200), EXP, stats and the material counters on ch. Free of state for the static tests.
void spire_kit_lobby_apply(PSOBBCharacterFile& ch, std::shared_ptr<const LevelTable> level_table, uint32_t level);
// Runs the push; returns the text for the player. Never throws.
asio::awaitable<std::string> spire_kit_lobby_push(std::shared_ptr<Client> c, uint32_t level);

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
// v42 (regie, pc 2026-10-25 12:32: q921 room 7 = k2 elite, "wave 3 done, room not clear: W-009 (event 117) is
// running"; its only enemy, the elite Hildebear E-04D, never showed: no hit, no kill, no flag from either client while
// the event had started at 12:29:48). Second net, for a started wave event without a living enemy: when the room
// check finds the room not clear and some started, unfinished event of the room has no enemy that is alive and was hit
// (every one defeated or never hit, no boss; an event without enemies counts too), the server looks again every
// SPIRE_UNHIT_ENEMY_SECONDS. An event without a living enemy at two looks in a row that is all that still holds the
// room counts as finished (flag 0018 on the event, flag 8 on its enemies; the server's map state only), logs "event n
// of room r on floor ff running without a living enemy; counted as finished", and the room check runs again. One
// watch per room; it stops at the room clear, at the run end, or when every started event has a living enemy again.
// v43 (pc 2026-10-25 20:43): the watch also starts when the room is held only by a trigger_event of an event that has
// not started yet (the check ran before the clients' 6x67); it then needs one look more.
bool spire_event_without_living_enemy(const std::vector<SpireWaveEnemy>& enemies);
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
// Shared with InventoryBackup (regie pso-regie-10): the snapshot hex fields (decode throws on a length mismatch)
std::string spire_guard_hex_encode(const void* data, size_t size);
void spire_guard_hex_decode(const std::string& hex, void* data, size_t size);

// v42 no player damage in a Spire run (regie pso-regie-9, 2026-10-25: players never damage each other in a Spire run;
// the Wii hit the Dolphin player in Battle although the quest sends pl_pkoff). In a Spire game (spire_game_is_spire: a
// quest listed in SpireGuardQuests or SpireBridgeQuests) the server removes every player target (entity id below
// 0x1000) from 6x46 (attack strikes) and 6x49 (photon blast) before forwarding, and from 6x47 (technique) unless the
// technique is a support technique (spire_pvp_tech_is_support: Deband, Shifta, Ryuker, Resta, Anti, Reverser), so a
// Resta or Shifta on the partner still lands. Enemy and object targets are kept. The count and size fields are
// rewritten; a command left without targets is still forwarded (count 0 is valid, a swing or tech that hits nothing),
// so the other clients still see the animation. The first strip per attacker per game logs "Spire PvP: stripped ...".
bool spire_game_is_spire(std::shared_ptr<const Client> c, std::shared_ptr<const Lobby> l);
bool spire_pvp_tech_is_support(uint8_t technique_number);
// Each returns the number of player targets removed from targets (0: header and targets unchanged).
size_t spire_pvp_strip_player_targets(G_AttackFinished_Header_6x46& header, std::vector<TargetEntry>& targets);
size_t spire_pvp_strip_player_targets(G_CastTechnique_Header_6x47& header, std::vector<TargetEntry>& targets);
size_t spire_pvp_strip_player_targets(G_ExecutePhotonBlast_Header_6x49& header, std::vector<TargetEntry>& targets);
void spire_pvp_log_strip(std::shared_ptr<const Client> c, std::shared_ptr<Lobby> l, uint8_t subcommand, size_t count);
