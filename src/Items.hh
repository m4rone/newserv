#pragma once

#include <stdint.h>

#include <memory>
#include <random>

#include "Client.hh"
#include "ItemData.hh"
#include "ItemParameterTable.hh"
#include "PSOEncryption.hh"
#include "ServerState.hh"
#include "StaticGameData.hh"

void player_use_item(std::shared_ptr<Client> c, size_t item_index, std::shared_ptr<RandomGenerator> rand_crypt);
void player_feed_mag(std::shared_ptr<Client> c, size_t mag_item_index, size_t fed_item_index);

void apply_mag_feed_result(
    ItemData& mag_item,
    const ItemData& fed_item,
    std::shared_ptr<const ItemParameterTable> item_parameter_table,
    std::shared_ptr<const MagMetadataTable> mag_metadata_table,
    uint8_t char_class,
    uint8_t section_id,
    bool version_has_rare_mags);
// The evolution part of apply_mag_feed_result (Sega's rules, hardcoded in the client): the mag's kind (data1[1]) and its
// new photon blast, decided as at a feed that reaches mag_level with the mag's current stats. The Spire run mag calls
// it with the levels a fed mag passes (10, 35, then a multiple of 5) to name the mag its stats would have evolved into.
void apply_mag_evolution(
    ItemData& mag_item,
    std::shared_ptr<const ItemParameterTable> item_parameter_table,
    std::shared_ptr<const MagMetadataTable> mag_metadata_table,
    uint8_t char_class,
    uint8_t section_id,
    bool version_has_rare_mags,
    uint8_t mag_level);
