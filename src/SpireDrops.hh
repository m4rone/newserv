#pragma once

#include <stdint.h>

#include <map>
#include <memory>
#include <phosg/JSON.hh>
#include <string>

#include "EnemyType.hh"
#include "ItemData.hh"
#include "ItemParameterTable.hh"
#include "LevelTable.hh"
#include "PlayerSubordinates.hh"

class Client;
class Lobby;

// Spire drops (regie pso-regie-9, B-E01-108; rule book items 25-30 of pso-roguelike
// new-features/roguelike/output/spelsysteem-randgevallen-2026-10-25.md). In a Spire game (spire_game_is_spire) the server
// makes the drops (SpireDrops.DropMode, default SERVER_PRIVATE: every player his own drops, items 29 and 34) and passes
// every item it made for a player through spire_drops_filter_item before it is sent:
// - item 30 (OnlyRunItems): only weapons, frames, barriers, mates, fluids, Sol/Moon/Star Atomizers and meseta drop; any
//   other item (materials, Telepipes, mags, Scape Dolls, units, tech disks, grinders, ...) is removed;
// - items 25-26: a weapon the player's class cannot equip, or whose ATP/ATA/MST requirement is above the stats he has
//   LevelsAhead levels from now (Sega's level table; 0 = only what he can equip now), becomes a weapon he can equip now;
// - item 27 (PityDrops): after that many kept drops in a row without a weapon he can equip now, the next drop is one;
// - item 28 (BossWeaponPerPlayer): a boss's drop is exactly one weapon each player can equip now (Sega's own drop when it
//   already is one, else a picked one), also when Sega's table gave nothing.
// A picked weapon is the best common (not rare) weapon of a random weapon group 01-0C (Saber ... Wand) whose
// requirements the player meets now, without grind, special or percentages (see spire_drops_pick_weapon).
// Route B F8 (plan-route-b-2026-10-26 section 6): the boss weapon of room k gets grind min(MaxGrind, BossGrindBase +
// BossGrindPerK * k) and Hit min(50, BossHitBase + BossHitPerK * k), also when it is Sega's own drop; from room PickFromK
// on, a picked weapon (items 25-27) gets grind min(MaxGrind, 1 + k) and Hit min(25, 5 + 2k). The Hit sits in the
// attribute slot of type 5 (spire_drops_boost_weapon); no special is added (special byte 0 on a picked weapon). Room k is
// the k SpireCheckpointRooms gives the player's floor (spire_drops_floor_k). Missing keys: no grind and no Hit (as
// before F8).
// Without the SpireDrops key nothing changes (the client makes the drops, the quest's rule block still forbids Scape
// Dolls). In the client drop mode (or when the client never sends 6x60/6xA2) the server never sees a drop, so none of
// this applies and the run plays with Sega's client drops: it degrades to the old behaviour, never to a lost item.
struct SpireDropsConfig {
  bool enabled = false; // the SpireDrops key is present
  ServerDropMode drop_mode = ServerDropMode::SERVER_PRIVATE; // DropMode
  bool only_run_items = true; // OnlyRunItems (item 30)
  uint32_t levels_ahead = 5; // LevelsAhead (item 26: "hooguit een gebied vooruit"; 0 = only usable now)
  uint32_t pity_drops = 8; // PityDrops (item 27; 0 = off)
  bool boss_weapon_per_player = true; // BossWeaponPerPlayer (item 28)
  uint32_t boss_grind_base = 0; // BossGrindBase (F8; boss weapon grind = base + per_k * k, 0 and 0 = none)
  uint32_t boss_grind_per_k = 0; // BossGrindPerK
  uint32_t boss_hit_base = 0; // BossHitBase (F8; boss weapon Hit = base + per_k * k, 0 and 0 = none)
  uint32_t boss_hit_per_k = 0; // BossHitPerK
  uint32_t pick_from_k = 0; // PickFromK (F8; picked weapons get grind and Hit from this room on; 0 = never)

  static SpireDropsConfig from_json(const phosg::JSON& json);
  std::string str() const;
};

// Bits of ItemParameterTable::Weapon::usability_flags that this class must find set
uint8_t spire_weapon_usability_bits(uint8_t char_class);
// Item 30: true for the kinds a run drops (weapon, frame, barrier, mate, fluid, Sol/Moon/Star Atomizer, meseta)
bool spire_drops_is_run_item(const ItemData& item);

struct SpireDropPlayer {
  uint8_t char_class = 0;
  CharacterStats now; // the player's stats now
  CharacterStats ahead; // his stats levels_ahead levels later (Sega's level table)
};
// The stats of a character of this class at level (0-based) + levels_ahead, from his stats now (level capped at 199)
CharacterStats spire_drops_stats_ahead(
    const LevelTable& level_table, uint8_t char_class, uint32_t level, const CharacterStats& now, uint32_t levels_ahead);

enum class SpireWeaponFit {
  NOT_A_WEAPON = 0,
  NOW, // class can equip it, requirements met now
  AHEAD, // class can equip it, requirements met only levels_ahead levels later
  NO, // class cannot equip it, or the requirements are above even the stats ahead
};
SpireWeaponFit spire_drops_weapon_fit(const ItemParameterTable& pmt, const ItemData& item, const SpireDropPlayer& p);
// The weapon to give: see above. random picks the group. If no common weapon fits the player now (Sega's level-1
// rangers), the common weapons of his class he lacks the least for. Empty only if his class can equip none.
ItemData spire_drops_pick_weapon(const ItemParameterTable& pmt, const SpireDropPlayer& p, uint32_t random);

// B-BEL-4 (armour): a frame or barrier this class cannot wear (Sega's usability bits, as for weapons) becomes one of
// the same kind it can wear: the strongest common one (DFP + EVP of Sega's table, the later one on a tie) that is not
// stronger than the dropped one, else the base Frame or Barrier (index 0). Its slots stay, its DFP and EVP bonus are
// capped at the new item's ranges. Returns true if the item changed. Any other item, one the class can wear, or one
// not in Sega's table: unchanged. Pity does not see this (the item stays a non-weapon run item).
bool spire_drops_fit_armor(const ItemParameterTable& pmt, ItemData& item, uint8_t char_class);

// F8: the grind and Hit a weapon gets (0 = none)
struct SpireWeaponBoost {
  uint8_t grind = 0;
  uint8_t hit = 0;
};
constexpr uint8_t SPIRE_WEAPON_ATTRIBUTE_HIT = 5; // attribute type of Hit in data1[6], [8], [10]
constexpr uint32_t SPIRE_DROPS_BOSS_HIT_MAX = 50;
constexpr uint32_t SPIRE_DROPS_PICK_HIT_MAX = 25;
constexpr uint32_t SPIRE_DROPS_PICK_GRIND_BASE = 1, SPIRE_DROPS_PICK_GRIND_PER_K = 1;
constexpr uint32_t SPIRE_DROPS_PICK_HIT_BASE = 5, SPIRE_DROPS_PICK_HIT_PER_K = 2;
// The boss weapon of room k: grind BossGrindBase + BossGrindPerK * k (capped at 255; MaxGrind is applied by
// spire_drops_boost_weapon), Hit min(50, BossHitBase + BossHitPerK * k). Nothing for k 0 (room unknown).
SpireWeaponBoost spire_drops_boss_boost(const SpireDropsConfig& cfg, uint32_t k);
// A picked weapon in room k: grind 1 + k, Hit min(25, 5 + 2k) from PickFromK on; nothing before it, nothing with
// PickFromK 0 or k 0.
SpireWeaponBoost spire_drops_pick_boost(const SpireDropsConfig& cfg, uint32_t k);
// Gives a weapon at least this grind (capped at its MaxGrind in Sega's table) and at least this Hit, in its attribute
// slot of type 5, else in its first empty slot (never a second slot of one type; no empty slot: no Hit). Never lowers
// a grind or Hit it has, never touches its special byte. Returns true if the item changed. Not a weapon, or not in
// Sega's table: unchanged.
bool spire_drops_boost_weapon(const ItemParameterTable& pmt, ItemData& item, SpireWeaponBoost boost);
// The room k of a floor in a game of this episode: the highest K SpireCheckpointRooms gives any room of that floor
// (a boss floor's Room -1 entry; a floor with two listed rooms, Forest 1, its later one), or 0 if none.
uint8_t spire_drops_floor_k(const std::map<uint8_t, std::map<std::pair<uint8_t, int32_t>, uint8_t>>& rooms_by_episode,
    Episode episode, uint8_t floor);

// Route B F8 (critic M2): the warning line for a boss drop whose room k is 0 (its floor is not in SpireCheckpointRooms,
// e.g. the key is missing from the live config), so a boss weapon without grind or Hit is visible in the log; "" for
// any other drop or a boss drop with a room.
std::string spire_drops_boss_k_warning(bool is_boss, uint8_t k, Episode episode, uint8_t floor);

// The decision for one item made for one player, free of state for the static tests. Changes item (cleared or
// replaced) and pity (kept drops in a row without a weapon usable now). k is the room (F8 grind and Hit; 0 = unknown, no
// grind or Hit). Returns why the item changed, or nullptr if it was kept as it is.
const char* spire_drops_filter_item(const SpireDropsConfig& cfg, const ItemParameterTable& pmt, ItemData& item,
    const SpireDropPlayer& p, bool is_boss, uint32_t& pity, uint32_t random, uint32_t k = 0);

// Item 28 applies to the drop of this enemy: a boss (EnemyType BOSS flag), except the Vol Opt parts before its end.
// Vol Opt's monitors, pillars, amp, core and phase 1 carry the BOSS flag too, but only VOL_OPT_2 falling ends the fight
// (k11, plan-mines-volopt-2026-10-25 section 5), so only VOL_OPT_2 gives each player his boss weapon. Dragon, De Rol Le
// and every other boss are unchanged. Dark Falz (k15): DARK_FALZ_1 carries the flag but is not the end, so it gives no
// boss drop; the final phase (DARK_FALZ_2 on Normal, DARK_FALZ_3 above, the type newserv's flag route drops for) does.
// Darvants have no BOSS flag. Only the head of an enemy set gives the boss drop (child_index 0, the drop target after
// alias resolution): Gol Dragon's set expands to the head plus 5 children of type GOL_DRAGON, and Barba Ray's ends on
// a second BARBA_RAY child; each child has its own drop flag, so a drop request for one would otherwise be a second
// boss drop. Dark Falz and Olga Flow children alias their head, which keeps child_index 0 as the target.
bool spire_drops_is_boss_drop(EnemyType type, uint16_t child_index = 0);

// B-BEL-4 (meseta): the rules the server's item creator uses in a Spire game with SpireDrops: the quest's rule block
// with meseta allowed. The quest forbids meseta drops (ba_set_meseta_drop_mode 1, which also goes to the client and
// stays), but the server makes the drops and the Spire filter keeps meseta, so the server gives Sega's meseta drops of
// enemies and boxes again. Every other rule stays the quest's. Returns quest_rules itself when it forbids no meseta
// (or is null).
std::shared_ptr<const BattleRules> spire_drops_item_rules(std::shared_ptr<const BattleRules> quest_rules);

// Server glue. At quest load: in a Spire game with SpireDrops, set the lobby's drop mode and forget every client's pity
// counter, and give the lobby's item creator spire_drops_item_rules (again at every filtered drop, because the
// lobby makes a new item creator with the quest's rules when its leader changes). Per item made for client c (the receiving player; the requester in SERVER_SHARED): apply the filter, log a
// change. Both no-ops outside a Spire game or without SpireDrops; the filter never throws (a missing table or
// character keeps the item as it is). floor: the floor the item drops on (the drop request's), which gives the room
// k; never the receiver's floor (critic M1: a SERVER_PRIVATE box drop reaches players on other floors, and a client's
// floor is stale between a follow-warp and its own floor report).
void spire_drops_quest_loaded(std::shared_ptr<Lobby> l, std::shared_ptr<const Client> leader_c);
bool spire_drops_filter(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, ItemData& item, bool is_boss, uint8_t floor);
