#include "SpireFlagBridge.hh"

#include <algorithm>
#include <phosg/JSON.hh>

#include "Loggers.hh"
#include "SpireMag.hh"
#include "SpireMechanics.hh"

// The run mag in the savestate (the seam, docs/review-loop/goals/naad.md point 4): the key "RunMag" with the run mag's
// ledger, written and read at the seam's hook H10 run_state (SpireMechanics.hh). Both blocks stood in
// SpireFlagBridge.cc (spire_savestate_to_json and spire_savestate_from_json) and are moved here as they were; the
// hook is called where they stood, so the key is put in the same dict at the same moment and the savestate's text is
// the one of before.
//
// The registration carries no switch: the run mag itself has none in the core, and a savestate written without its
// ledger gives no run mag back at the resume (spire_savestate_items keeps only the ledger's mag).

namespace {

void spire_mag_state_write(const SpireSavestate& ss, phosg::JSON& json) {
  if (ss.run_mag) { // F6: the run mag ledger, keyed by run id and step, never by item id
    const auto& led = *ss.run_mag;
    phosg::JSON bosses = phosg::JSON::list();
    for (uint8_t k : led.boss_rooms) {
      bosses.emplace_back(k);
    }
    auto led_json = phosg::JSON::dict({
        {"RunId", led.run_id},
        {"IssuedStep", led.issued_step},
        {"StartStep", led.start_step},
        {"BossRooms", std::move(bosses)},
        {"Lean", led.lean},
    });
    if (led.last_mirrored) {
      led_json.emplace("LastMirrored", spire_run_mag_bytes_hex(*led.last_mirrored));
    }
    // fase 1 part A, rule B: what the reward weapon asks of the mag, and mag food still owed
    if (led.need.pow || led.need.dex || led.need.mind) {
      led_json.emplace("Need", phosg::JSON::list({led.need.pow, led.need.dex, led.need.mind}));
    }
    if (led.disk_mind) { // part B: the MIND the reward disks of this run ask
      led_json.emplace("DiskMind", led.disk_mind);
    }
    if (led.boost_due) {
      led_json.emplace("BoostDue", true);
    }
    if (led.feed_levels) {
      led_json.emplace("FeedLevels", led.feed_levels);
    }
    json.emplace("RunMag", std::move(led_json));
  }
}

void spire_mag_state_read(const phosg::JSON& json, SpireSavestate& ret) {
  if (json.contains("RunMag")) {
    const auto& led_json = json.at("RunMag");
    SpireRunMagLedger led;
    led.run_id = led_json.get_int("RunId");
    led.issued_step = led_json.get_int("IssuedStep");
    led.start_step = led_json.get_int("StartStep");
    for (const auto& k : led_json.get_list("BossRooms")) {
      led.boss_rooms.emplace(k->as_int());
    }
    int64_t lean = led_json.get_int("Lean", 0);
    led.lean = spire_run_mag_saved_lean(lean);
    if (led.lean != lean) {
      player_data_log.warning_f("Spire savestate: run mag ledger of run {:08X} has lean {}; read as 0 (no choice)",
          led.run_id, lean);
    }
    if (led_json.contains("LastMirrored")) {
      led.last_mirrored = spire_run_mag_bytes_from_hex(led_json.get_string("LastMirrored"));
    }
    if (led_json.contains("Need")) {
      const auto& need = led_json.get_list("Need");
      if (need.size() == 3) {
        // an edited file: 0-200, never a wrapped number (as "Lean", F10S critic N6)
        led.need = SpireMagNeed{static_cast<uint16_t>(std::clamp<int64_t>(need[0]->as_int(), 0, 200)),
            static_cast<uint16_t>(std::clamp<int64_t>(need[1]->as_int(), 0, 200)),
            static_cast<uint16_t>(std::clamp<int64_t>(need[2]->as_int(), 0, 200))};
      }
    }
    led.boost_due = led_json.get_bool("BoostDue", false);
    led.feed_levels = std::clamp<int64_t>(led_json.get_int("FeedLevels", 0), 0, 200);
    led.disk_mind = std::clamp<int64_t>(led_json.get_int("DiskMind", 0), 0, 200);
    ret.run_mag = led;
  }
}

[[maybe_unused]] const bool registered = spire_mech_register_run_state(
    {"mags", "", SPIRE_MECH_ORDER_MAGS, {nullptr, spire_mag_state_write, spire_mag_state_read, nullptr}});

} // namespace
