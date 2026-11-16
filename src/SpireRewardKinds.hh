#pragma once

#include <stdint.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include <phosg/JSON.hh>

#include "AsyncUtils.hh"
#include "Client.hh"
#include "Lobby.hh"
#include "SpireDraw.hh"
#include "SpireEquipItem.hh"
#include "SpireFlagBridge.hh"
#include "SpireMag.hh"
#include "SpireMechanics.hh"
#include "SpireRewards.hh"

// What the reward kinds' own files (src/SpireReward*.cc) and the core share (the seam, docs/review-loop/goals/naad.md
// point 3). An internal header: the core is SpireRewards.cc (the grid's shared part and the distribution of an offer
// over the kinds) and SpireFlagBridge.cc (the 'Q' and the 'Z' subs 6 and 7); a kind includes this file, reads what it
// needs from what the core hands it (SpireRewardOfferInput in SpireRewards.hh; SpireRewardLive, SpireRewardPick and
// SpireRewardResult below) and registers its four functions (SpireMechanics.hh spire_mech_register_kind). A new kind
// changes nothing here.

// --- The grid's shared part (SpireRewards.cc) ---

static constexpr const char* TIER_NAMES[4] = {"Normal", "Rare", "Epic", "Legendary"};
// An item code of the grid ("000101"); throws when it is not 6 hex digits
uint32_t spire_code_of(const std::string& hex);
// A key's value, 0-max; without def a missing key throws
uint32_t spire_get_u(const phosg::JSON& json, const std::string& key, uint32_t max, uint32_t def = 0xFFFFFFFF);

// --- The bridge (SpireFlagBridge.cc) ---

// The run of a game (lobby id; a game's id is never reused while the server runs): set by the first 'J' or 'B' in the
// game, forgotten at its next quest start.
struct SpireGameRun {
  uint32_t run_id = 0;
  bool resumed = false; // the leader resumed from its savestate
  uint32_t elapsed = 0; // the leader's saved seconds (resumed only)
  size_t max_clients = 0; // the most clients the game had during the run
  std::set<std::string> savestate_files; // written ('V') or answered from ('J') in this run, also of a client gone since
  bool real_played = false; // 7r2-M1: a client's 'B' in this run was no practice run (spire_savestate_end_deletes_file)
};
SpireGameRun* spire_game_run_for(std::shared_ptr<const Lobby> l);
SpireRunMagLedger* spire_run_mag_ledger_for(std::shared_ptr<const Client> c, uint32_t run_id, bool create);
std::optional<uint32_t> spire_start_level_run_bonus(const Client* c, uint32_t lobby_id, uint32_t quest_number);
void spire_send_ticker(std::shared_ptr<Client> c, const std::string& text);
bool spire_equip_item_available_for(std::shared_ptr<Client> c);
asio::awaitable<void> spire_equip_item_send(std::shared_ptr<Client> c, std::weak_ptr<const Lobby> weak_l,
    uint32_t item_id, std::string what, uint8_t point, uint32_t slot = 0);

// What a reward pick answered with an item, until its RESULT ('Z' sub 7)
struct SpireRewardPending {
  SpireRewardOffer offer;
  SpireMagNeed need;
  uint16_t disk_mind = 0; // part B: the MIND this reward disk asks of the mag (in need.mind too)
  bool mag_due = false;
  bool server_offer = false; // false: the entry's own item (no server reward)
  uint32_t atp = 0, ata = 0, mst = 0, level = 0; // the run copy at the pick
  std::string class_name;
  std::optional<SpireMagBytes> mag_before;
};
// Per client and game: the offers of its last 'Q' by board slot (replaced at each 'Q'), and the pick in flight
struct SpireRewardOffers {
  std::weak_ptr<const Client> client;
  uint32_t lobby_id = 0;
  std::map<uint8_t, SpireRewardOffer> by_slot;
  std::optional<SpireRewardPending> pending;
  uint32_t equip_seq = 0; // counts the server weapons taken: a later one ends an earlier one's wait for its mag
};
SpireRewardOffers& spire_reward_offers_for(const std::shared_ptr<Client>& c, uint32_t lobby_id);
// The one run mag of the run copy (type 02), or nullopt
std::optional<SpireMagBytes> spire_reward_run_mag(const PlayerInventory& inventory, bool* equipped = nullptr);
std::string spire_reward_mag_text(const std::optional<SpireMagBytes>& mag);
// The 6x77s of a reward answer: r20-r31 (with an item), r41, DONE
void spire_reward_answer(std::shared_ptr<Client> c, const SpireMagBytes* item, bool mag_follows);

// One offer at the bridge (SpireRewardOfferInput::live): the client, its game and the drawn entry. A kind reads from
// here what its offer needs of the run copy; log_detail is what it adds to the journal line of the offer.
struct SpireRewardLive {
  std::shared_ptr<Client> c;
  std::shared_ptr<const Lobby> l;
  uint32_t serial_number;
  std::shared_ptr<PSOBBCharacterFile> character; // the real character
  std::shared_ptr<PSOBBCharacterFile> overlay; // the run copy
  const SpireDrawEntry& entry;
  uint32_t curse; // the drawn curse code of the slot (0 none)
  uint32_t k; // the quest's room
  uint32_t trede;
  bool elite;
  std::string log_detail;
};

// One pick ('Z' sub 6) of a server offer, as the core hands it to the kind's pick function. The core has checked the
// run mag (refusal), found the entry and the offer, and filled p with the run copy's stats; the kind answers the
// client (spire_reward_answer), or calls own_item(why) for the entry's own item, and sets offers.pending when a RESULT
// must follow.
struct SpireRewardPick {
  std::shared_ptr<Client> c;
  std::shared_ptr<const Lobby> l;
  uint32_t serial_number;
  uint32_t quest_number;
  uint8_t slot;
  uint8_t entry_id;
  uint32_t pick_curse;
  std::shared_ptr<PSOBBCharacterFile> character; // the real character
  std::shared_ptr<PSOBBCharacterFile> overlay; // the run copy
  const SpireDrawEntry* entry;
  const SpireRewardTable* table;
  const SpireRewardOffer* offer;
  SpireRewardOffers& offers;
  SpireRewardPending& p;
  SpireRunMagLedger* led; // nullptr: no run mag ledger (see refusal)
  const SpireMagLine* mag_line;
  SpireRunMagRefusal refusal;
  SpireRewardWorn worn;
  size_t main_stat;
  std::function<void(const std::string&)> own_item;
};
// Shared by the kinds whose item rule B's mag must carry (a weapon, a disk), after the kind has set p.need: whether the
// mag must follow for it (p.mag_due), the pick in flight (offers.pending) and the answer with the item
void spire_reward_pick_rule_b(SpireRewardPick& pk);

// The RESULT 1 ('Z' sub 7) of a pick that answered a server offer's item, as the core hands it to the kind's result
// function: the item exists on the client. p is the pick's pending, already taken out of offers.
struct SpireRewardResult {
  std::shared_ptr<Client> c;
  std::shared_ptr<const Lobby> l;
  uint32_t serial_number;
  uint32_t quest_number;
  SpireRewardOffers& offers;
  SpireRewardPending& p;
  SpireGameRun* run = nullptr; // set by spire_reward_result_rule_b
  SpireRunMagLedger* led = nullptr; // set by spire_reward_result_rule_b
};
// Shared by the same kinds: the ledger keeps what this item asks of every later mag (rule B); fills rs.run and rs.led
void spire_reward_result_rule_b(SpireRewardResult& rs);
