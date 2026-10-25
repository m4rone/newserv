#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <phosg/JSON.hh>
#include <resource_file/Emulators/Expression.hh>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "Account.hh"
#include "Client.hh"
#include "ClientFunctionIndex.hh"
#include "CommonItemSet.hh"
#include "DNSServer.hh"
#include "DOLFileIndex.hh"
#include "Episode3/DataIndexes.hh"
#include "Episode3/Tournament.hh"
#include "GSLArchive.hh"
#include "IPV4RangeSet.hh"
#include "ItemNameIndex.hh"
#include "ItemParameterTable.hh"
#include "ItemTranslationTable.hh"
#include "LevelTable.hh"
#include "SpireDrops.hh"
#include "Lobby.hh"
#include "MagMetadataTable.hh"
#include "Menu.hh"
#include "Quest.hh"
#include "ShopRandomSets.hh"
#include "TeamIndex.hh"
#include "TekkerAdjustmentSet.hh"
#include "WordSelectTable.hh"

struct DataIndex {
  // This structure contains everything which is essentially immutable during the server's uptime - e.g. configuration,
  // tables, item definitions, etc. which are only changed by reloading them from disk. Mutable structures, like
  // AccountIndex and TeamIndex, are on ServerState instead.

  struct PortConfiguration {
    std::string name;
    std::string addr; // Blank = listen on all interfaces (default)
    uint16_t port;
    Version version;
    ServerBehavior behavior;
  };

  struct CheatFlags {
    // This structure describes which behaviors are considered cheating (that is, require cheat mode to be enabled or the
    // user to have the CHEAT_ANYWHERE account flag). A false value here means that that particular behavior is NOT
    // cheating, so cheat mode is NOT required.
    bool create_items = true;
    bool edit_section_id = true;
    bool edit_stats = true;
    bool ep3_replace_assist = true;
    bool ep3_unset_field_character = true;
    bool infinite_hp_tp = true;
    bool fast_kills = true;
    bool insufficient_minimum_level = true;
    bool override_random_seed = true;
    bool override_section_id = true;
    bool override_variations = true;
    bool proxy_override_drops = true;
    bool reset_materials = false;
    bool warp = true;

    CheatFlags() = default;
    explicit CheatFlags(const phosg::JSON& json);
  };

  struct BBStreamFile {
    struct Entry {
      uint32_t offset;
      uint32_t size;
      uint32_t checksum; // crc32
      std::string filename;
    };
    std::vector<Entry> entries;
    std::string data;
  };

  enum class RunShellBehavior {
    DEFAULT = 0,
    ALWAYS,
    NEVER,
  };
  enum class BehaviorSwitch {
    OFF = 0,
    OFF_BY_DEFAULT,
    ON_BY_DEFAULT,
    ON,
  };

  static inline bool behavior_enabled(BehaviorSwitch b) {
    return (b == BehaviorSwitch::ON_BY_DEFAULT) || (b == BehaviorSwitch::ON);
  }
  static inline bool behavior_can_be_overridden(BehaviorSwitch b) {
    return (b == BehaviorSwitch::OFF_BY_DEFAULT) || (b == BehaviorSwitch::ON_BY_DEFAULT);
  }

  uint64_t creation_time;

  std::string config_filename;
  std::shared_ptr<const phosg::JSON> config_json;
  bool one_time_config_loaded = false;

  size_t num_worker_threads = 0;

  std::string name;
  std::unordered_map<std::string, PortConfiguration> name_to_port_config;
  std::unordered_map<uint16_t, PortConfiguration> number_to_port_config;
  std::string username;
  std::string dns_server_addr;
  uint16_t dns_server_port = 0;
  std::vector<std::string> ip_stack_addresses;
  std::vector<std::string> ppp_stack_addresses;
  std::vector<std::string> ppp_raw_addresses;
  std::vector<std::string> http_addresses;
  uint64_t client_ping_interval_usecs = 30000000;
  uint64_t client_idle_timeout_usecs = 60000000;
  uint64_t patch_client_idle_timeout_usecs = 300000000;
  bool is_debug = false;
  bool ip_stack_debug = false;
  bool allow_unregistered_users = false;
  bool allow_pc_nte = false;
  bool use_temp_accounts_for_prototypes = true;
  bool allow_same_account_concurrent_logins = true;
  std::array<uint16_t, NUM_VERSIONS> compatibility_groups = {};
  bool enable_chat_commands = true;
  char chat_command_sentinel = '\0'; // 0 = default (@ on 11/2000; $ on all other versions)
  size_t num_backup_character_slots = 16;
  std::unique_ptr<std::array<uint32_t, NUM_NON_PATCH_VERSIONS>> version_name_colors;
  uint32_t client_customization_name_color = 0x00000000;
  uint8_t allowed_drop_modes_v1_v2_normal = 0x1F;
  uint8_t allowed_drop_modes_v1_v2_battle = 0x07;
  uint8_t allowed_drop_modes_v1_v2_challenge = 0x07;
  uint8_t allowed_drop_modes_v3_normal = 0x1F;
  uint8_t allowed_drop_modes_v3_battle = 0x07;
  uint8_t allowed_drop_modes_v3_challenge = 0x07;
  uint8_t allowed_drop_modes_v4_normal = 0x1D; // CLIENT not allowed
  uint8_t allowed_drop_modes_v4_battle = 0x05;
  uint8_t allowed_drop_modes_v4_challenge = 0x05;
  ServerDropMode default_drop_mode_v1_v2_normal = ServerDropMode::CLIENT;
  ServerDropMode default_drop_mode_v1_v2_battle = ServerDropMode::CLIENT;
  ServerDropMode default_drop_mode_v1_v2_challenge = ServerDropMode::CLIENT;
  ServerDropMode default_drop_mode_v3_normal = ServerDropMode::CLIENT;
  ServerDropMode default_drop_mode_v3_battle = ServerDropMode::CLIENT;
  ServerDropMode default_drop_mode_v3_challenge = ServerDropMode::CLIENT;
  ServerDropMode default_drop_mode_v4_normal = ServerDropMode::SERVER_SHARED;
  ServerDropMode default_drop_mode_v4_battle = ServerDropMode::SERVER_SHARED;
  ServerDropMode default_drop_mode_v4_challenge = ServerDropMode::SERVER_SHARED;
  std::unordered_map<uint16_t, std::unique_ptr<ResourceDASM::Expression::Node>> quest_flag_rewrites_v1_v2;
  std::unordered_map<uint16_t, std::unique_ptr<ResourceDASM::Expression::Node>> quest_flag_rewrites_v3;
  std::unordered_map<uint16_t, std::unique_ptr<ResourceDASM::Expression::Node>> quest_flag_rewrites_v4;
  std::unordered_map<std::string, std::pair<uint8_t, uint32_t>> quest_counter_fields; // For $qfread command
  uint64_t persistent_game_idle_timeout_usecs = 0;
  std::unordered_map<uint32_t, int64_t> enable_send_function_call_quest_numbers;
  bool enable_v3_v4_protected_subcommands = false;
  // Quest numbers in which the Spire register bridge applies (SpireBridgeQuests); empty = the bridge never takes a
  // register write (see SpireFlagBridge.hh)
  std::unordered_set<uint32_t> spire_bridge_quest_numbers;
  // v15: quests the Spire guard protects; unset (the key is missing) means the guard uses spire_bridge_quest_numbers
  std::optional<std::unordered_set<uint32_t>> spire_guard_quest_numbers;
  // Regie pso-regie-9: quests outside the Spire run that read the Spire meta flags (SpireMetaQuests, e.g. the E06
  // payout q925). They get the flags delivered and their clears of the payout flags recorded, nothing else of the
  // bridge (no records, replay, checkpoint, follow or guard); empty = none
  std::unordered_set<uint32_t> spire_meta_quest_numbers;
  // v25 boss clear: quests (SpireBossClearQuests) in which a boss the quest cannot see clear with if_zone_clear (Vol Opt
  // phase 2) gets the server's room clear when it is defeated; empty = no boss clear on the server (k4/k8 unchanged)
  std::unordered_set<uint32_t> spire_boss_clear_quest_numbers;
  std::vector<uint8_t> spire_battle_end_phases;
  // Spire BBA TX guard (SpireBBATxGuardPatch, default true): send the client patch of SpireBBATxGuard.hh to GC EU v1.0
  // clients at login; false turns it off without a rebuild
  bool spire_bba_tx_guard_patch = true;
  // v19 lab: SpireRunTimerLab {"LimitSeconds": n, "WarningSeconds": m} replaces the time limit and warning the quest
  // sends with 'S', so a lab round reaches the warning and "Time is up." in minutes. Missing (the pc): the quest's values.
  std::optional<std::pair<uint32_t, uint32_t>> spire_run_timer_lab;
  // v23 checkpoint: SpireCheckpointRooms [{"Floor": f, "Room": r, "K": k}, ...] maps a cleared room (Room missing or -1:
  // any room of the floor, used for the boss floors) to the run's room index k 1-31; empty means no checkpoints.
  std::map<std::pair<uint8_t, int32_t>, uint8_t> spire_checkpoint_rooms;
  // v23: SpireCheckpointArrivals [{"Floor": f, "K": k}, ...]: a client arriving on floor f has done room k (a rest
  // room has no fight the server can see, so the floor of the room after it is its checkpoint).
  std::map<uint8_t, uint8_t> spire_checkpoint_arrivals;
  // v23 replay: SpireReplayRegisters [n, ...], the quest registers whose latest 6x77 value the server keeps per game and
  // sends again to a client after its own floor load (a packet that arrives during a load can be lost); empty means off.
  std::set<uint8_t> spire_replay_registers;
  // v26 follow the leader: SpireFollowLeader {"LeaderClient": n, "DelayMs": m}. When that client of a Spire game loads
  // a floor other than Pioneer 2, every other client not on it is warped there (6x94) after DelayMs; missing = off.
  std::optional<std::pair<uint8_t, uint32_t>> spire_follow_leader;
  // Regie pso-regie-9 (B-E01-108): SpireDrops {"DropMode": "SERVER_PRIVATE", "OnlyRunItems": true, "LevelsAhead": 5,
  // "PityDrops": 8, "BossWeaponPerPlayer": true}: in a Spire game the server makes the drops and filters them per player
  // (see SpireDrops.hh). Without the key: disabled (client drops, as before).
  SpireDropsConfig spire_drops;
  // v27 blocked characters: SpireBlockedCharacters {"Quests": [n, ...], "Characters": [{"Serial": s, "Name": n}, ...]}.
  // A GC client with that serial and character name (Name empty: every character of that serial) never sees, loads or
  // joins one of those quests. A serial belongs to the console, not the character, so a real character is blocked by
  // serial plus name and the test characters on the same console still play (regie 2026-10-24, Wakiru).
  std::unordered_set<uint32_t> spire_blocked_quest_numbers;
  std::vector<std::pair<uint32_t, std::string>> spire_blocked_characters;
  bool is_spire_blocked(uint32_t gc_serial, const std::string& name, uint32_t quest_number) const;
  // True if the character is listed in SpireBlockedCharacters, whatever the quest
  bool is_spire_protected_character(uint32_t gc_serial, const std::string& name) const;
  // Regie pso-regie-9: the stock chat commands that change a character's data (stats, level, items, quest flags, the
  // whole character, or client memory) are refused everywhere for a protected character; command_name is the
  // command's first name ("$item" for "$i"). Other commands ($fastkill, $infhp, $secid, ...) stay allowed.
  static bool spire_cheat_refused(const std::string& command_name, bool protected_character);
  // The decision behind Client::is_spire_blocked. name is null if the client has no character to read it from (fail
  // closed: every listed quest and every Challenge quest is blocked). A Challenge quest is blocked for a protected
  // character whatever its number (audit S-3).
  bool is_spire_blocked_quest(uint32_t gc_serial, const std::string* name, uint32_t quest_number, bool challenge) const;
  // v28 test serials (regie pso-regie-9): SpireTestSerials [s, ...], GC serials whose quest availability counts the Spire
  // rooms 1-10 as cleared (SPIRE_K1..SPIRE_K10, SPIRE_MAX 10), so a test console can start any room quest q941-q955
  // directly. Server-side only: the checkpoint file and the card's quest flags are not written. Empty = off.
  std::unordered_set<uint32_t> spire_test_serials;
  static constexpr uint32_t SPIRE_TEST_SERIAL_ROOMS = 0x3FF; // rooms 1-10 (bit k-1 = room k)
  // The room mask an AvailableIf / EnabledIf sees: the character's checkpoint rooms, plus rooms 1-10 for a test serial
  uint32_t spire_availability_rooms(uint32_t gc_serial, uint32_t checkpoint_rooms) const;
  bool ep3_infinite_meseta = false;
  std::vector<uint32_t> ep3_defeat_player_meseta_rewards = {400, 500, 600, 700, 800};
  std::vector<uint32_t> ep3_defeat_com_meseta_rewards = {100, 200, 300, 400, 500};
  uint32_t ep3_final_round_meseta_bonus = 300;
  bool ep3_jukebox_is_free = false;
  uint32_t ep3_behavior_flags = 0;
  bool hide_download_commands = true;
  bool censor_credentials = true;
  RunShellBehavior run_shell_behavior = RunShellBehavior::DEFAULT;
  BehaviorSwitch cheat_mode_behavior = BehaviorSwitch::OFF_BY_DEFAULT;
  bool default_switch_assist_enabled = false;
  bool use_game_creator_section_id = false;
  bool rare_notifs_enabled_for_client_drops = false;
  bool default_rare_notifs_enabled_v1_v2 = false;
  bool default_rare_notifs_enabled_v3_v4 = false;
  std::unordered_set<uint32_t> notify_game_for_item_primary_identifiers_v1_v2;
  std::unordered_set<uint32_t> notify_game_for_item_primary_identifiers_v3;
  std::unordered_set<uint32_t> notify_game_for_item_primary_identifiers_v4;
  std::unordered_set<uint32_t> notify_server_for_item_primary_identifiers_v1_v2;
  std::unordered_set<uint32_t> notify_server_for_item_primary_identifiers_v3;
  std::unordered_set<uint32_t> notify_server_for_item_primary_identifiers_v4;
  bool notify_server_for_max_level_achieved = false;
  std::vector<std::shared_ptr<const PSOBBEncryption::KeyFile>> bb_private_keys;
  std::shared_ptr<const parray<uint8_t, 0x16C>> bb_default_keyboard_config;
  std::shared_ptr<const parray<uint8_t, 0x38>> bb_default_joystick_config;
  std::shared_ptr<const ClientFunctionIndex> client_functions;
  std::shared_ptr<const PatchFileIndex> pc_patch_file_index;
  std::shared_ptr<const PatchFileIndex> bb_patch_file_index;
  std::unordered_map<uint64_t, std::shared_ptr<const MapFile>> map_file_for_source_hash;
  std::map<uint32_t, std::array<std::shared_ptr<const MapFile>, NUM_VERSIONS>> map_files_for_free_play_key;
  std::unordered_map<uint64_t, std::shared_ptr<const SuperMap>> supermap_for_source_hash_sum;
  std::unordered_map<uint32_t, std::shared_ptr<const SuperMap>> supermap_for_free_play_key;
  std::shared_ptr<const RoomLayoutIndex> room_layout_index;
  std::shared_ptr<const BBStreamFile> bb_stream_file;
  std::shared_ptr<const DOLFileIndex> dol_file_index;
  std::shared_ptr<const Episode3::CardIndex> ep3_card_index;
  std::shared_ptr<const Episode3::CardIndex> ep3_card_index_trial;
  std::shared_ptr<const Episode3::MapIndex> ep3_map_index;
  std::shared_ptr<const Episode3::COMDeckIndex> ep3_com_deck_index;
  std::shared_ptr<const G_SetEXResultValues_Ep3_6xB4x4B> ep3_default_ex_values;
  std::shared_ptr<const G_SetEXResultValues_Ep3_6xB4x4B> ep3_tournament_ex_values;
  std::shared_ptr<const G_SetEXResultValues_Ep3_6xB4x4B> ep3_tournament_final_round_ex_values;
  std::shared_ptr<const QuestCategoryIndex> quest_category_index;
  std::shared_ptr<const QuestOpcodeIndex> quest_opcodes;
  std::shared_ptr<const QuestIndex> quest_index;
  std::shared_ptr<const LevelTable> level_table_v1_v2;
  std::shared_ptr<const LevelTable> level_table_v3;
  std::shared_ptr<const LevelTable> level_table_v4;
  std::shared_ptr<const BattleParamsIndex> battle_params;
  std::shared_ptr<const GSLArchive> bb_data_gsl;
  std::unordered_map<std::string, std::shared_ptr<const CommonItemSet>> common_item_sets;
  std::unordered_map<std::string, std::shared_ptr<const RareItemSet>> rare_item_sets;
  std::shared_ptr<const ArmorShopRandomSet> armor_random_set;
  std::shared_ptr<const ToolShopRandomSet> tool_random_set;
  std::array<std::shared_ptr<const WeaponShopRandomSet>, 4> weapon_random_sets; // Keyed on difficulty
  std::shared_ptr<const TekkerAdjustmentSet> tekker_adjustment_set;
  std::array<std::shared_ptr<const ItemParameterTable>, NUM_VERSIONS> item_parameter_tables;
  std::shared_ptr<const ItemTranslationTable> item_translation_table;
  std::array<std::shared_ptr<const ItemData::StackLimits>, NUM_VERSIONS> item_stack_limits_tables;
  size_t bb_max_bank_items = 200;
  size_t bb_max_bank_meseta = 999999;
  std::shared_ptr<const MagMetadataTable> mag_metadata_table_dc_nte;
  std::shared_ptr<const MagMetadataTable> mag_metadata_table_dc_11_2000;
  std::shared_ptr<const MagMetadataTable> mag_metadata_table_v1;
  std::shared_ptr<const MagMetadataTable> mag_metadata_table_v2;
  std::shared_ptr<const MagMetadataTable> mag_metadata_table_v3;
  std::shared_ptr<const MagMetadataTable> mag_metadata_table_v4;
  std::shared_ptr<const TextIndex> text_index;
  std::array<std::shared_ptr<const ItemNameIndex>, NUM_VERSIONS> item_name_indexes;
  std::shared_ptr<const WordSelectTable> word_select_table;
  std::array<std::shared_ptr<const SetDataTableBase>, NUM_VERSIONS> set_data_tables;
  std::array<std::shared_ptr<const SetDataTableBase>, NUM_VERSIONS> set_data_tables_ep1_ult;
  std::shared_ptr<const SetDataTableBase> bb_solo_set_data_table;
  std::shared_ptr<const SetDataTableBase> bb_solo_set_data_table_ep1_ult;
  std::array<std::shared_ptr<const MapState::RareEnemyRates>, 4> rare_enemy_rates_by_difficulty;
  std::shared_ptr<const MapState::RareEnemyRates> rare_enemy_rates_challenge;
  std::array<std::array<size_t, 4>, 3> min_levels_v1_v2; // Indexed as [episode][difficulty]
  std::array<std::array<size_t, 4>, 3> min_levels_v3; // Indexed as [episode][difficulty]
  std::array<std::array<size_t, 4>, 3> min_levels_v4; // Indexed as [episode][difficulty]
  std::unordered_set<std::string> bb_required_patches;
  std::unordered_set<std::string> auto_patches;
  CheatFlags cheat_flags;

  struct QuestF960Result {
    uint32_t meseta_cost = 0;
    uint32_t base_probability = 0;
    uint32_t probability_upgrade = 0;
    std::array<std::vector<ItemData>, 7> results;

    QuestF960Result() = default;
    QuestF960Result(
        const phosg::JSON& json, std::shared_ptr<const ItemNameIndex> name_index, const ItemData::StackLimits& limits);
  };

  // Indexed as [type][difficulty][random_choice]
  std::vector<std::vector<std::vector<ItemData>>> quest_F95E_results;
  std::vector<std::pair<size_t, ItemData>> quest_F95F_results; // [(num_photon_tickets, item)]
  std::vector<QuestF960Result> quest_F960_success_results;
  QuestF960Result quest_F960_failure_results;
  float bb_global_exp_multiplier = 1.0f;
  float exp_share_multiplier = 0.5f;
  float server_global_drop_rate_multiplier = 1.0f;

  uint16_t ep3_card_auction_points = 0;
  uint16_t ep3_card_auction_min_size = 0;
  uint16_t ep3_card_auction_max_size = 0;
  struct CardAuctionPoolEntry {
    uint64_t probability;
    uint16_t card_id;
    uint16_t min_price;
  };
  std::vector<CardAuctionPoolEntry> ep3_card_auction_pool;
  std::array<std::vector<uint16_t>, 5> ep3_trap_card_ids;
  struct Ep3LobbyBannerEntry {
    uint32_t type = 1;
    uint32_t which; // See B9 documentation in CommandFormats.hh
    std::string data;
  };
  std::vector<Ep3LobbyBannerEntry> ep3_lobby_banners;

  std::shared_ptr<const IPV4RangeSet> banned_ipv4_ranges;

  phosg::JSON team_reward_defs_json;

  std::shared_ptr<const Menu> information_menu_v2;
  std::shared_ptr<const Menu> information_menu_v3;
  std::shared_ptr<const std::vector<std::string>> information_contents_v2;
  std::shared_ptr<const std::vector<std::string>> information_contents_v3;
  std::shared_ptr<const Menu> proxy_destinations_menu_dc;
  std::shared_ptr<const Menu> proxy_destinations_menu_pc;
  std::shared_ptr<const Menu> proxy_destinations_menu_gc;
  std::shared_ptr<const Menu> proxy_destinations_menu_xb;
  std::vector<std::pair<std::string, uint16_t>> proxy_destinations_dc;
  std::vector<std::pair<std::string, uint16_t>> proxy_destinations_pc;
  std::vector<std::pair<std::string, uint16_t>> proxy_destinations_gc;
  std::vector<std::pair<std::string, uint16_t>> proxy_destinations_xb;
  std::optional<std::pair<std::string, uint16_t>> proxy_destination_patch;
  std::optional<std::pair<std::string, uint16_t>> proxy_destination_bb;
  std::string welcome_message;
  std::string pc_patch_server_message;
  std::string bb_patch_server_message;

  std::array<std::vector<uint32_t>, NUM_VERSIONS> public_lobby_search_orders;
  std::vector<uint32_t> client_customization_public_lobby_search_order;
  uint8_t pre_lobby_event = 0;
  std::vector<uint8_t> per_lobby_events;
  int32_t ep3_menu_song = -1;

  std::map<std::string, uint32_t> all_addresses;
  uint32_t local_address = 0;
  uint32_t external_address = 0;

  bool proxy_allow_save_files = true;

  explicit DataIndex(const std::string& config_filename = "");

  uint32_t connect_address_for_client(std::shared_ptr<Client> c) const;
  uint16_t game_server_port_for_version(Version v) const;

  std::shared_ptr<const Menu> information_menu(Version version) const;
  std::shared_ptr<const Menu> proxy_destinations_menu(Version version) const;
  const std::vector<std::pair<std::string, uint16_t>>& proxy_destinations(Version version) const;

  std::shared_ptr<const SetDataTableBase> set_data_table(
      Version version, Episode episode, GameMode mode, Difficulty difficulty) const;

  inline std::shared_ptr<const WeaponShopRandomSet> weapon_random_set(Difficulty difficulty) const {
    return this->weapon_random_sets.at(static_cast<size_t>(difficulty));
  }
  inline std::shared_ptr<const MapState::RareEnemyRates> rare_enemy_rates(Difficulty difficulty) const {
    return this->rare_enemy_rates_by_difficulty.at(static_cast<size_t>(difficulty));
  }

  std::shared_ptr<const LevelTable> level_table(Version version) const;
  std::shared_ptr<const ItemParameterTable> item_parameter_table(Version version) const;
  std::shared_ptr<const ItemParameterTable> item_parameter_table_for_encode(Version version) const;
  std::shared_ptr<const MagMetadataTable> mag_metadata_table(Version version) const;
  std::shared_ptr<const ItemData::StackLimits> item_stack_limits(Version version) const;
  std::shared_ptr<const ItemNameIndex> item_name_index_opt(Version version) const; // Returns null if missing
  std::shared_ptr<const ItemNameIndex> item_name_index(Version version) const; // Throws if missing
  std::string describe_item(Version version, const ItemData& item, uint8_t flags = 0) const;
  ItemData parse_item_description(Version version, const std::string& description) const;

  std::shared_ptr<const CommonItemSet> common_item_set(Version logic_version, std::shared_ptr<const Quest> q) const;
  std::shared_ptr<const RareItemSet> rare_item_set(Version logic_version, std::shared_ptr<const Quest> q) const;

  const std::vector<uint32_t>& public_lobby_search_order(Version version, bool is_client_customization) const;
  inline const std::vector<uint32_t>& public_lobby_search_order(std::shared_ptr<const Client> c) const {
    return this->public_lobby_search_order(c->version(), c->check_flag(Client::Flag::IS_CLIENT_CUSTOMIZATION));
  }

  inline uint32_t name_color_for_client(Version v, bool is_client_customization) const {
    if (is_client_customization && this->client_customization_name_color) {
      return this->client_customization_name_color;
    }
    return this->version_name_colors ? this->version_name_colors->at(static_cast<size_t>(v) - NUM_PATCH_VERSIONS) : 0;
  }
  inline uint32_t name_color_for_client(std::shared_ptr<const Client> c) const {
    return this->name_color_for_client(c->version(), c->check_flag(Client::Flag::IS_CLIENT_CUSTOMIZATION));
  }

  std::shared_ptr<const std::vector<std::string>> information_contents_for_client(std::shared_ptr<const Client> c) const;

  size_t default_min_level_for_game(Version version, Episode episode, Difficulty difficulty) const;

  void set_port_configuration(const std::vector<PortConfiguration>& port_configs);

  std::shared_ptr<const std::string> load_bb_file(const std::string& patch_index_filename) const;
  std::shared_ptr<const std::string> load_map_file(Version version, const std::string& filename) const;

  std::pair<std::string, uint16_t> parse_port_spec(const phosg::JSON& json) const;
  std::vector<PortConfiguration> parse_port_configuration(const phosg::JSON& json) const;

  static constexpr uint32_t free_play_key(
      Episode episode, GameMode mode, Difficulty difficulty, uint8_t floor, uint32_t layout, uint32_t entities) {
    return (static_cast<uint32_t>(episode) << 28) |
        (static_cast<uint32_t>(mode) << 26) |
        (static_cast<uint32_t>(difficulty) << 24) |
        (static_cast<uint32_t>(floor) << 16) |
        (static_cast<uint32_t>(layout) << 8) |
        (static_cast<uint32_t>(entities) << 0);
  }
  std::shared_ptr<const SuperMap> get_free_play_supermap(
      Episode episode, GameMode mode, Difficulty difficulty, uint8_t floor, uint32_t layout, uint32_t entities);
  std::vector<std::shared_ptr<const SuperMap>> supermaps_for_variations(
      Episode episode, GameMode mode, Difficulty difficulty, const Variations& variations);

  void collect_network_addresses();
  void load_config_early();
  void load_config_late();
  void load_bb_private_keys();
  void load_bb_system_defaults();
  void load_patch_indexes();
  void load_maps();
  void load_battle_params();
  void load_level_tables();
  void load_text_index();
  std::shared_ptr<ItemNameIndex> create_item_name_index_for_version(
      std::shared_ptr<const ItemParameterTable> pmt,
      std::shared_ptr<const ItemData::StackLimits> limits,
      std::shared_ptr<const TextIndex> text_index) const;
  void load_item_name_indexes();
  void load_drop_tables();
  void load_item_definitions();
  void load_set_data_tables();
  void load_word_select_table();
  void load_ep3_cards();
  void load_ep3_maps(bool raise_on_any_failure = false);
  void load_quest_opcode_defs();
  void load_quest_index(bool raise_on_any_failure = false);
  void compile_functions(bool raise_on_any_failure = false);
  void load_dol_files();
  void generate_bb_stream_file();

  void load_all();
};
