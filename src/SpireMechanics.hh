#pragma once

#include <optional>
#include <string>
#include <vector>

#include <phosg/JSON.hh>

#include "SpireRewards.hh"

// SpireMechanics: the seam between the Spire core and its mechanics (pso-roguelike
// new-features/roguelike/design/mechanics/integratie.md par. 3; this repo's docs/review-loop/goals/naad.md). A
// mechanic builds in files of its own (src/Spire*.cc, taken by a glob in CMakeLists.txt) and registers here; the core
// calls what was registered and names no mechanic. Everything in this file carries the prefix spire_mech_.
//
// Every list of registrations is a static inside a function: a file registers from a static initialiser of its own,
// and the order in which the translation units start does not matter.

// --- Tests ---
//
// A test file (src/SpireTests*.cc) registers one function under a name of its own, with one line:
//   [[maybe_unused]] const bool registered = spire_mech_register_tests("mags", spire_tests_mags);
// run_static_tests() (StaticTests.cc) calls spire_mech_run_tests() once and names no test file.

using SpireMechTestFunction = void (*)();

// Registers a test file's function under its name. Always returns true (so it can initialise a static). A name
// registered twice keeps the first function and makes spire_mech_run_tests throw: no test runs twice, none is hidden.
bool spire_mech_register_tests(const char* name, SpireMechTestFunction fn);

// Runs every registered function exactly once, in the order of the names, and logs one line per test file on stderr
// before it runs. Throws std::logic_error when nothing was registered or a name was registered twice.
void spire_mech_run_tests();

// The names registered, in the order spire_mech_run_tests runs them
std::vector<std::string> spire_mech_test_names();

// --- Switches ---
//
// system/spire-schakelaars.json: one JSON object, key -> true or false (pso-roguelike integratie.md par. 7; the quest
// repo's quests/spire/data/schakelaars.json, installed by apply-on-pc.sh). The file is looked at once a second at
// most (a hook that asks at every hit costs no file question per hit), so a change is read within a second. A
// missing file or a missing key is ON, unless the asker names its key "default off". An unreadable file (no JSON, no
// object, a value that is no boolean) leaves the last good state standing and logs one line.

constexpr const char* SPIRE_SWITCHES_FILE = "system/spire-schakelaars.json";
constexpr uint64_t SPIRE_SWITCHES_LOOK_USECS = 1000000; // between two looks at the file

// Whether the mechanic behind this key is on. For a reward kind, off means: no offer, so the entry's own item. For a
// hook: not registered. A mechanic also needs the Enabled of its own table.
bool spire_mech_enabled(const std::string& key, bool default_off = false);

// Tests only: reads the switches from another file ("" = SPIRE_SWITCHES_FILE again) and forgets what was read and
// when it last looked
void spire_mech_set_switches_file(const std::string& path);
// Tests only: the clock the second between two looks is measured on, in microseconds (nullptr: the server's own)
void spire_mech_set_switches_clock(uint64_t (*now_usecs)());

// --- Reward kinds ---
//
// A reward kind (SpireRewardKind, the srv value of a draw entry) lives in a file of its own (src/SpireReward*.cc) and
// registers there with its number, its switch key and four functions:
//
//   namespace {
//   void my_parse(const phosg::JSON& json, SpireRewardTable& ret);  // its own section of the grid -> ret.sections[key]
//   std::optional<SpireRewardOffer> my_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret);
//   void my_pick(SpireRewardPick& pk);      // 'Z' sub 6 (SpireRewardKinds.hh)
//   void my_result(SpireRewardResult& rs);  // 'Z' sub 7
//   [[maybe_unused]] const bool registered = spire_mech_register_kind(
//       {SpireRewardKind::FOCUS, "force.focus", false, my_parse, my_offer, my_pick, my_result});
//   } // namespace
//
// The core (SpireRewards.cc spire_rewards_parse and spire_reward_offer; SpireFlagBridge.cc spire_reward_pick and
// spire_reward_result) asks this table and names no kind. A kind reads its input itself from what the core hands it:
// the grid's JSON at the parse, SpireRewardOfferInput (with the client at the bridge in in.live), SpireRewardPick and
// SpireRewardResult. A kind that is not registered, or whose switch is off, gives no offer: the pick answers the
// entry's own item.

struct SpireRewardPick; // SpireRewardKinds.hh
struct SpireRewardResult;

struct SpireMechKind {
  SpireRewardKind kind = SpireRewardKind::NONE;
  const char* switch_key = ""; // spire_mech_enabled; also the key of its section in SpireRewardTable::sections
  bool default_off = false; // off until its key is true in the switches file (a kind that waits for a measurement)
  // Its own section of the grid; nullptr: none. Called once per parse of the grid, after the shared part, whatever its
  // switch says. Throws on a section it cannot read (the grid is then refused as a whole, as before the seam).
  void (*parse)(const phosg::JSON& json, SpireRewardTable& ret) = nullptr;
  // The offer. ret comes with what every kind shares filled in (kind, tier, room_index, w, from_line); nullopt: none
  // from this registration. A throw is "no offer" with its reason.
  std::optional<SpireRewardOffer> (*offer)(const SpireRewardOfferInput& in, SpireRewardOffer ret) = nullptr;
  void (*pick)(SpireRewardPick& pk) = nullptr;
  void (*result)(SpireRewardResult& rs) = nullptr; // nullptr: its pick never leaves a pick in flight
};

// Registers a kind. Always returns true (so it can initialise a static). A kind registered twice keeps the first
// registration and is named by spire_mech_kind_conflicts.
bool spire_mech_register_kind(const SpireMechKind& reg);
// Registers a route that is asked BEFORE the kind itself: its offer function answers nullopt when the offer is not its
// own, and the kind itself is asked then. An offer it gives carries first_route, so its pick and result come back to
// this registration. Two points wait for one, nobody registers there yet: kind 4 (DISK) for the android route before
// the disk, kind 5 (UNIT) for "power first" before the unit.
bool spire_mech_register_kind_first(const SpireMechKind& reg);
// The registration of a kind (first: the route before it), whatever its switch says; nullptr when nobody registered
const SpireMechKind* spire_mech_kind(SpireRewardKind kind, bool first = false);
// Every registration, by kind number, a kind's first route before the kind itself
std::vector<const SpireMechKind*> spire_mech_kinds();
// "kind 4" or "kind 4 (first route)" for every registration that came second; empty when all is well
std::vector<std::string> spire_mech_kind_conflicts();
// Tests only: takes a registration out again, with the conflicts named for it
void spire_mech_forget_kind(SpireRewardKind kind, bool first = false);

// --- Hooks ---
//
// Eleven moments of the core at which a mechanic acts without a line in the core (pso-roguelike integratie.md par. 3,
// H1-H11). A mechanic registers a function from a file of its own:
//
//   namespace {
//   void my_wave_start(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
//       uint32_t quest_number, uint32_t a, uint32_t b);
//   [[maybe_unused]] const bool registered = spire_mech_register_wave_start(
//       {"superkrachten", "superkrachten", SPIRE_MECH_ORDER_POWERS, my_wave_start});
//   } // namespace
//
// and the core calls the hook's one function at its moment (spire_mech_wave_start). A registration carries a name
// (one per hook), a switch key and a fixed number: the registrations of a hook are called by that number, lower first
// (the name breaks a tie), never by the order in which the translation units start. A registration whose switch is off
// is not called (par. 7: off means not registered); an empty switch key is no switch (a fixed gate, par. 7). Without a
// registration that is called, a hook changes nothing: the core does what it did before the seam.
//
// H4-H7 stand in handlers of every game and H9's game exit comes at every 98: a registrant asks itself whether the
// game is a Spire run (spire_game_is_spire). A registrant that throws on H1-H9, or at H10's capture and resume, is
// logged and counts as not registered for that call. H10's write and read and H11 are part of reading and writing a
// file: a throw there refuses the file as a whole, as an unreadable key did before the seam.

enum class SpireMechHookId : uint8_t {
  QUEST_BIN = 1,
  REWARD_ROOM = 2,
  TYPE_WEIGHT = 3,
  PLAYER_HP = 4,
  PLAYER_DIED = 5,
  TECH_DONE = 6,
  ENEMY_KILLED = 7,
  WAVE_START = 8,
  RUN_END = 9,
  RUN_STATE = 10,
  CHAR_META = 11,
};

// The fixed place of every owner in a hook's order of calls. One ranking gives every row of par. 3's table: H1
// leveling, force, defence; H3 hunter types, ranger types, force types; H4 defence, powers; H10 mags, force, powers.
constexpr uint32_t SPIRE_MECH_ORDER_MAGS = 10;
constexpr uint32_t SPIRE_MECH_ORDER_LEVELING = 20;
constexpr uint32_t SPIRE_MECH_ORDER_HUNTER_TYPES = 30;
constexpr uint32_t SPIRE_MECH_ORDER_RANGER_TYPES = 40;
constexpr uint32_t SPIRE_MECH_ORDER_FORCE = 50; // also the force types on H3
constexpr uint32_t SPIRE_MECH_ORDER_DEFENCE = 60;
constexpr uint32_t SPIRE_MECH_ORDER_POWERS = 70;

template <typename FnT>
struct SpireMechHook {
  const char* name = ""; // the registrant; one registration per name and hook
  const char* switch_key = ""; // spire_mech_enabled; "": no switch, always called
  uint32_t order = 0; // SPIRE_MECH_ORDER_*
  FnT fn{};
  bool default_off = false; // off until its key is true in the switches file
};

class Client;
struct Lobby;
struct SpireDrawEntry; // SpireDraw.hh
struct DropReconcileResult; // ReceiveSubcommands.hh
struct SpireSavestate; // SpireFlagBridge.hh
struct SpireMetaFile; // SpireMeta.hh

// The names registered on a hook, in the order of the calls, whatever their switches say
std::vector<std::string> spire_mech_hook_names(SpireMechHookId hook);
// "H10 run_state: mags" for every registration whose name was on that hook already (the first stays); empty when all
// is well
std::vector<std::string> spire_mech_hook_conflicts();
// Tests only: takes a registration out again, with the conflict named for it
void spire_mech_forget_hook(SpireMechHookId hook, const char* name);

// H1 quest_bin: a client gets its quest .bin (ReceiveCommands.cc set_lobby_quest), after the heat rows and the start
// level. A registrant answers the .bin this client gets (compressed, as it is sent): the one it was handed, or a copy
// it changed; nullptr is "the one it was handed". Every client of a game is asked for itself.
using SpireMechQuestBinFn = std::shared_ptr<const std::string> (*)(std::shared_ptr<Client> c,
    std::shared_ptr<const Lobby> l, std::shared_ptr<const std::string> bin);
bool spire_mech_register_quest_bin(const SpireMechHook<SpireMechQuestBinFn>& reg);
std::shared_ptr<const std::string> spire_mech_quest_bin(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l,
    std::shared_ptr<const std::string> bin);

// H2 reward_room: at every offer (SpireFlagBridge.cc spire_reward_make_offer), the room index and the rung (trede) the
// offer reckons with. They come in as the quest's room k gives them (spire_reward_room_index) and the client's ladder
// (spire_reward_trede_for); a registrant changes them in place. What the registrants gave back stays inside the grid:
// a room index they changed is brought back to the nearest of the rooms the grid knows (1 to the last room of its
// RoomW or its disks), a rung they changed to the nearest of the grid's rungs (0 to the rung with every boss of
// TredeRooms beaten), with one line in the journal. Values no registrant changed stay the core's own.
struct SpireMechRewardRoom {
  std::shared_ptr<Client> c;
  std::shared_ptr<const Lobby> l;
  uint32_t serial_number;
  const SpireRewardTable& table; // the grid
  const SpireDrawEntry& entry; // the drawn entry
  uint32_t k; // the quest's room
  bool elite;
};
using SpireMechRewardRoomFn = void (*)(const SpireMechRewardRoom& at, uint32_t& room_index, uint32_t& trede);
bool spire_mech_register_reward_room(const SpireMechHook<SpireMechRewardRoomFn>& reg);
void spire_mech_reward_room(const SpireMechRewardRoom& at, uint32_t& room_index, uint32_t& trede);

// H3 type_weight: a reward kind chooses one of its candidates (a line, a technique, a unit). A registrant answers the
// weight of one candidate, 1 when it has nothing to say about it; the weights of the registrants multiply. The number
// of players and what this choice holds already are the registrant's to read from in->live (the client, its game;
// SpireRewardKinds.hh spire_reward_offers_for).
enum class SpireMechCandidate : uint8_t {
  LINE = 1, // line: the weapon line's name
  TECH = 2, // value: the technique number, level: the disk's level
  UNIT = 3, // value: the unit's item code
};
struct SpireMechTypeWeight {
  const SpireRewardOfferInput* in; // the offer being made (nullptr in a test of the hook alone)
  SpireMechCandidate what;
  const std::string* line = nullptr;
  uint32_t value = 0;
  uint32_t level = 0;
};
using SpireMechTypeWeightFn = uint32_t (*)(const SpireMechTypeWeight& candidate);
bool spire_mech_register_type_weight(const SpireMechHook<SpireMechTypeWeightFn>& reg);
// The index of the candidate chosen out of n (n > 0). Without a registration that is called this is rnd(n) % n, the one
// draw the kind made before the seam, and candidate is never asked. With one, every candidate is asked once and one
// draw rnd(sum of the weights) chooses; weights that are all 1 give the same draw and the same candidate as without.
// When every weight is 0 the draw is the plain one again.
size_t spire_mech_type_weight_pick(size_t n, const std::function<uint32_t(uint32_t)>& rnd,
    const std::function<SpireMechTypeWeight(size_t)>& candidate);

// H4 player_hp: an enemy's hit on the player itself (ReceiveSubcommands.cc on_change_hp, 6x4B a minor and 6x4C a major
// hit, by the number the client sent: a GC client's; the handler's other subcommands never reach a registrant),
// before the hit is forwarded
using SpireMechPlayerHPFn = void (*)(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t subcommand);
bool spire_mech_register_player_hp(const SpireMechHook<SpireMechPlayerHPFn>& reg);
void spire_mech_player_hp(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t subcommand);

// H5 player_died: the player itself died (ReceiveSubcommands.cc on_player_died, 6x4D), before the death is forwarded
using SpireMechPlayerDiedFn = void (*)(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l);
bool spire_mech_register_player_died(const SpireMechHook<SpireMechPlayerDiedFn>& reg);
void spire_mech_player_died(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l);

// H6 tech_done: the player's own technique is cast (ReceiveSubcommands.cc on_cast_technique_finished, 6x48), after it
// is forwarded
using SpireMechTechDoneFn = void (*)(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint32_t technique,
    uint32_t level);
bool spire_mech_register_tech_done(const SpireMechHook<SpireMechTechDoneFn>& reg);
void spire_mech_tech_done(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint32_t technique, uint32_t level);

// H7 enemy_killed: the drop request of a dying enemy (ReceiveSubcommands.cc on_entity_drop_item_request; the request
// of a box, rec.target_ene_st null, reaches no registrant), before the drop is made and whatever the game's drop mode.
// rec.should_drop is false for a request the map answered already.
using SpireMechEnemyKilledFn = void (*)(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l,
    const DropReconcileResult& rec, uint8_t floor);
bool spire_mech_register_enemy_killed(const SpireMechHook<SpireMechEnemyKilledFn>& reg);
void spire_mech_enemy_killed(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, const DropReconcileResult& rec,
    uint8_t floor);

// H8 wave_start: the quest's 'W' a/b (SpireFlagBridge.cc spire_bridge_handle_register), wave a of b; a = 1 is the
// room's start. c is the writer (the leader's room thread).
using SpireMechWaveStartFn = void (*)(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l,
    uint32_t serial_number, uint32_t quest_number, uint32_t a, uint32_t b);
bool spire_mech_register_wave_start(const SpireMechHook<SpireMechWaveStartFn>& reg);
void spire_mech_wave_start(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint32_t quest_number, uint32_t a, uint32_t b);

// H9 run_end: the three places where a client's run ends (SpireFlagBridge.cc): its return to base is OK ('O'), its
// run again is OK ('I': the earlier run is forgotten), and its 98 (spire_guard_end_run, of every game; l may be null)
enum class SpireMechRunEnd : uint8_t {
  BASE_RETURN = 1,
  RUN_AGAIN = 2,
  GAME_EXIT = 3,
};
using SpireMechRunEndFn = void (*)(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, SpireMechRunEnd why);
bool spire_mech_register_run_end(const SpireMechHook<SpireMechRunEndFn>& reg);
void spire_mech_run_end(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, SpireMechRunEnd why);

// H10 run_state: the savestate (SpireFlagBridge.cc), at its four moments; a registrant leaves a function it does not
// need nullptr. capture: a 'V' stores (spire_savestate_write), ss is the savestate being made. write: the savestate
// becomes text (spire_savestate_to_json); the registrant puts its own key in json, at the place the run mag's key had
// before the seam, so the registrations' keys follow in their order of calls. read: the text becomes a savestate
// (spire_savestate_from_json). resume: the client resumes from ss (the 'J' answered with it). What a registrant
// carries between two moments it keeps in ss.sections under its own key.
struct SpireMechRunState {
  void (*capture)(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, SpireSavestate& ss) = nullptr;
  void (*write)(const SpireSavestate& ss, phosg::JSON& json) = nullptr;
  void (*read)(const phosg::JSON& json, SpireSavestate& ret) = nullptr;
  void (*resume)(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, const SpireSavestate& ss) = nullptr;
};
bool spire_mech_register_run_state(const SpireMechHook<SpireMechRunState>& reg);
void spire_mech_run_state_capture(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, SpireSavestate& ss);
void spire_mech_run_state_write(const SpireSavestate& ss, phosg::JSON& json);
void spire_mech_run_state_read(const phosg::JSON& json, SpireSavestate& ret);
void spire_mech_run_state_resume(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, const SpireSavestate& ss);

// H11 char_meta: the meta file per character (SpireMeta.cc), read (spire_meta_load_file, after the core's own keys) and
// written (spire_meta_save, before the text is made). Every write of the file loads it first, so a registrant keeps
// what it read in ret.sections under its own key and writes it back from there: a key nobody carries is gone at the
// next write.
struct SpireMechCharMeta {
  void (*read)(const phosg::JSON& json, SpireMetaFile& ret) = nullptr;
  void (*write)(const SpireMetaFile& file, phosg::JSON& json) = nullptr;
};
bool spire_mech_register_char_meta(const SpireMechHook<SpireMechCharMeta>& reg);
void spire_mech_char_meta_read(const phosg::JSON& json, SpireMetaFile& ret);
void spire_mech_char_meta_write(const SpireMetaFile& file, phosg::JSON& json);
