#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

class Client;

// Security (regie pso-regie-10, Marck 2026-10-25: "kunnen wij geen security in de server inbouwen"): a table of
// subcommand field values that are known to crash the client that receives them. A subcommand from a client that
// carries such a value is refused before its handler runs: it is not forwarded and not applied. The first refusal per
// client per rule logs "Security: 6xNN from C-x refused: <field> <value> <condition> (client crash value)"; later
// refusals of the same rule by the same client are silent.
//
// Only rules with a concrete measured value are in the table. Each rule names its source.
struct CrashValueRule {
  uint8_t subcommand; // the final (GC V3 / BB) subcommand number
  const char* field_name;
  size_t field_offset; // from the start of the subcommand (the 4-byte header included)
  uint8_t field_width; // 1, 2 or 4 bytes, little-endian
  uint32_t bad_min; // the field breaks the rule when bad_min <= value <= bad_max
  uint32_t bad_max;
  // The rule applies only when the selector field (little-endian, selector_width bytes at selector_offset) has a value
  // whose bit is set in selector_values (values 0-31); selector_width 0 means always
  size_t selector_offset;
  uint8_t selector_width;
  uint32_t selector_values;
  const char* reason;
  const char* source;
};

const std::vector<CrashValueRule>& crash_value_rules();

// Returns the index of the first rule in rules that the subcommand breaks, or -1. A subcommand too short to hold the
// rule's fields breaks no rule (its handler rejects the size). value receives the field's value when a rule matches.
ssize_t crash_value_rule_violated(const std::vector<CrashValueRule>& rules, uint8_t final_subcommand, const void* data,
    size_t size, uint32_t* value);
// "Security: 6x69 from C-1 refused: npc_entity_id 13 >= 4 (client crash value)"
std::string crash_value_refusal_message(const CrashValueRule& rule, uint8_t client_id, uint32_t value);
// True the first time rule_index is passed for this mask (and sets its bit); false after that
bool crash_value_should_log(uint64_t& logged_rules, size_t rule_index);

// The check in the subcommand dispatcher: true if the subcommand must be dropped (logged once per client per rule)
bool crash_value_filter_refuses(std::shared_ptr<Client> c, uint8_t final_subcommand, const void* data, size_t size);
