#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>
#include <phosg/UnitTest.hh>

#include <stdint.h>

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "SpireFlagBridge.hh"
#include "SpireMag.hh"
#include "SpireMechanics.hh"
#include "SpireRewards.hh"

// The tests of the run mag (SpireMag.*, rule B, the ledger) and of reward kind 3 (mag food).
// Moved literally out of run_static_tests() in StaticTests.cc (the seam, docs/review-loop/goals/naad.md point 1): the
// text of every test below is what stood there, the indentation and this function around it aside. The owner of this
// file is the mags mechanic (pso-roguelike new-features/roguelike/design/mechanics/integratie.md par. 5).

namespace {

void spire_tests_mags() {
  phosg::log_info_f("-- Spire run mag: step, bytes, merge, refusals, idempotence (F6, route B)");
  {
    auto f6_case = [](const char* name, const std::function<void()>& fn) {
      phosg::log_info_f("   F6 ({})", name);
      fn();
    };
    using Bytes = SpireMagBytes;
    auto word = [](const Bytes& b, size_t which) -> uint16_t { return b[4 + which * 2] | (b[5 + which * 2] << 8); };
    auto table = spire_mag_parse(phosg::load_file(SPIRE_RUN_MAG_FILE));
    const auto& hu = table.classes.at("HU");

    f6_case("B-BEL-4: Run II grows at every Episode 2 boss, Run I unchanged (shipped spire-mag.json)", [&]() {
      // Run I keeps its five steps on the same rooms: step 4 after Dark Falz is still the mag of Run I
      const std::map<std::string, std::pair<uint8_t, uint8_t>> step4_ids = {
          {"HU", {0x06, 0x06}}, {"RA", {0x26, 0x26}}, {"FO", {0x1C, 0x24}}}; // Kama, Madhu, Naga (Shell: Bana)
      for (const char* cls : {"HU", "RA", "FO"}) {
        const auto& full = table.classes.at(cls);
        for (uint8_t lean : {SPIRE_RUN_MAG_LEAN_FANG, SPIRE_RUN_MAG_LEAN_SHELL}) {
          const bool is_shell = (lean == SPIRE_RUN_MAG_LEAN_SHELL);
          const auto col = spire_mag_column(full, lean).line;
          expect(col.steps.size() > 4);
          expect_eq(is_shell ? step4_ids.at(cls).second : step4_ids.at(cls).first, col.steps[4].id);
          expect_eq(70u, spire_mag_level_of(spire_mag_row_bytes(col.steps[4])));
          // Run I from room 1: the kit at k1, then one step per Episode 1 boss (after k4 step 1 ... after k15 step 4)
          SpireRunMagLedger run;
          run.run_id = 0xB41;
          auto kit = spire_run_mag_handle(run, table, full, SPIRE_RUN_MAG_ASK, 1, {}, {}, 0, lean);
          expect(kit.decision.result);
          expect_eq(0u, kit.decision.expected_step);
          expect_eq(is_shell, kit.shell);
          expect(spire_run_mag_result(run, true));
          Bytes held = kit.decision.bytes;
          uint32_t want = 0;
          auto run_bosses = [&](SpireRunMagLedger& led, Bytes& mag, uint32_t& step, std::initializer_list<uint8_t> ks) {
            for (uint8_t k : ks) {
              auto e = spire_run_mag_handle(led, table, full, SPIRE_RUN_MAG_EXAM, k, {mag}, {}, 0, 0);
              expect(e.decision.result);
              expect_eq(++step, e.decision.expected_step);
              expect(e.decision.bytes == spire_mag_step_bytes(col, step, 0, mag));
              expect(e.decision.bytes[1] != mag[1]); // a visible evolve
              expect(spire_mag_level_of(e.decision.bytes) > spire_mag_level_of(mag));
              expect(spire_run_mag_result(led, true));
              mag = e.decision.bytes;
            }
          };
          run_bosses(run, held, want, {4, 8, 11, 15});
          expect_eq(4u, want);
          Bytes run1_mag = held;
          // Run II from k16 (four bosses before it): step 4 at its first ASK, then one step per Episode 2 boss
          SpireRunMagLedger run2;
          run2.run_id = 0xB42;
          auto start = spire_run_mag_handle(run2, table, full, SPIRE_RUN_MAG_ASK, 16, {}, {}, 0, lean);
          expect_eq(4u, run2.start_step);
          expect(start.decision.result);
          expect_eq(4u, start.decision.expected_step);
          expect(start.decision.bytes == spire_mag_step_bytes(col, 4, 0, std::nullopt));
          expect(spire_run_mag_result(run2, true));
          held = start.decision.bytes;
          uint32_t step2 = 4;
          run_bosses(run2, held, step2, {18, 21, 27, 30}); // Barba Ray, Gol Dragon, Gal Gryphon, Olga Flow: steps 5-8
          expect_eq(8u, step2);
          expect_eq(8, run2.issued_step);
          // after Olga Flow the last step is the cap: the same EXAM again issues nothing
          auto again = spire_run_mag_handle(run2, table, full, SPIRE_RUN_MAG_EXAM, 30, {held}, {}, 0, 0);
          expect(!again.decision.result);
          expect_eq(8u, again.decision.expected_step);
          // a single room k19 (five bosses before it) starts at step 5
          SpireRunMagLedger single;
          single.run_id = 0xB43;
          auto s19 = spire_run_mag_handle(single, table, full, SPIRE_RUN_MAG_ASK, 19, {}, {}, 0, lean);
          expect_eq(5u, single.start_step);
          expect(s19.decision.result);
          expect_eq(5u, s19.decision.expected_step);
          expect(s19.decision.bytes == spire_mag_row_bytes(col.steps[5]));
          // Run I on into Episode 2 grows the same way: after k30 step 8
          run_bosses(run, run1_mag, want, {18, 21, 27, 30});
          expect_eq(8u, want);
        }
      }
    });

    f6_case("every row of the shipped spire-mag.json", [&]() {
      auto names = phosg::JSON::parse(phosg::load_file("system/tables/names-v3.json"));
      expect(table.enabled);
      expect_eq(3u, table.classes.size());
      for (const char* cls : {"HU", "RA", "FO"}) {
        const auto& full = table.classes.at(cls);
        // F10: the Shell column is shipped for every class, as long as Steps, and keeps the same rules (randgevallen 4-5)
        expect_eq(std::string(""), full.shell_note);
        expect_eq(full.steps.size(), full.shell_steps.size());
        const auto shell = spire_mag_column(full, SPIRE_RUN_MAG_LEAN_SHELL);
        expect(shell.shell && !shell.fell_back);
        // fix round (F10S critic N3): step 0 equal, and from step 1 DEF swapped with the main stat (POW for HU/RA,
        // MIND for FO), the two other stats equal; the Id's are checked above and below (names-v3, a new one per step)
        expect(spire_mag_row_bytes(full.shell_steps[0]) == spire_mag_row_bytes(full.steps[0]));
        bool fo = (std::string(cls) == "FO");
        for (size_t s = 1; s < full.steps.size(); s++) {
          const auto& f = full.steps[s];
          const auto& h = full.shell_steps[s];
          expect_eq(fo ? f.mind : f.pow, h.def);
          expect_eq(f.def, fo ? h.mind : h.pow);
          expect_eq(fo ? f.pow : f.mind, fo ? h.pow : h.mind);
          expect_eq(f.dex, h.dex);
        }
        for (const SpireMagLine* line_p : {&full, &shell.line}) {
          const auto& line = *line_p;
          expect_eq(table.boss_rooms.size() + 1, line.steps.size()); // B-BEL-4: the kit and one step per boss
          std::vector<SpireMagRow> rows = line.steps;
          for (const auto& f : line.floors) {
            rows.emplace_back(f.row);
          }
          for (const auto& row : rows) {
            auto b = spire_mag_row_bytes(row);
            expect_eq(0x02, b[0]);
            expect_eq(row.id, b[1]);
            uint32_t sum = 0;
            for (size_t z = 0; z < 4; z++) {
              expect_eq(0, word(b, z) & 1); // every stat even
              sum += word(b, z) / 100;
            }
            expect_eq(sum, b[2]); // LL == the sum of the four levels
            expect(b[2] <= 200);
            expect(names.contains(std::format("02{:02X}00", b[1]))); // a type-02 id of V3
          }
          for (size_t s = 1; s < line.steps.size(); s++) {
            auto prev = spire_mag_row_bytes(line.steps[s - 1]);
            auto cur = spire_mag_row_bytes(line.steps[s]);
            for (size_t z = 0; z < 4; z++) {
              expect(word(cur, z) >= word(prev, z)); // monotone per stat
            }
            expect(cur[1] != prev[1]); // a visible evolve at every step
            expect(cur[2] > prev[2]);
          }
          // every step under every start floor and an already fed mag keeps the same rules
          for (uint32_t bonus : {0u, 5u, 10u, 14u}) {
            Bytes prev{};
            for (size_t s = 0; s < line.steps.size(); s++) {
              auto b = spire_mag_step_bytes(line, s, bonus, std::nullopt);
              uint32_t sum = 0;
              for (size_t z = 0; z < 4; z++) {
                expect_eq(0, word(b, z) & 1);
                sum += word(b, z) / 100;
                expect(!s || (word(b, z) >= word(prev, z)));
              }
              expect_eq(sum, b[2]);
              expect(b[2] <= 200);
              prev = b;
            }
          }
        }
      }
      expect_eq(std::string("HU"), spire_mag_archetype(0));
      expect_eq(std::string("RA"), spire_mag_archetype(11));
      expect_eq(std::string("FO"), spire_mag_archetype(10));
      expect_eq(std::string(""), spire_mag_archetype(12));
      expect_eq(std::set<uint8_t>({4, 8, 11, 15, 18, 21, 27, 30}), table.boss_rooms); // spel-af item 5
    });

    f6_case("HU start floor L22", [&]() {
      Bytes l22 = {0x02, 0x01, 0x16, 0x00, 0x4C, 0x04, 0xE8, 0x03, 0x00, 0x00, 0x64, 0x00};
      expect(l22 == spire_mag_step_bytes(hu, 0, 14, std::nullopt)); // a Lv15 start (+14) begins at Varuna L22
      Bytes l13 = {0x02, 0x01, 0x0D, 0x00, 0x20, 0x03, 0xF4, 0x01, 0x00, 0x00, 0x00, 0x00};
      expect(l13 == spire_mag_step_bytes(hu, 0, 5, std::nullopt)); // Lv6 start: Varuna L13 (8/5/0/0)
      expect(l13 == spire_mag_step_bytes(hu, 0, 10, std::nullopt)); // Lv11 start: the same floor
      Bytes l5 = {0x02, 0x00, 0x05, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
      expect(l5 == spire_mag_step_bytes(hu, 0, 0, std::nullopt)); // no bought levels: Mag L5
      // after the Dragon under the L22 floor: merged per stat (11/18/0/1), the step's kind (Varuna) and LL 30
      auto s1 = spire_mag_step_bytes(hu, 1, 14, std::nullopt);
      expect_eq(0x01, s1[1]);
      expect_eq(30, s1[2]);
      expect_eq(1100, word(s1, 0));
      expect_eq(1800, word(s1, 1));
      expect_eq(100, word(s1, 3));
    });

    f6_case("merge: a fed POW 23.50 against the step's 18.00", [&]() {
      Bytes fed = {0x02, 0x00, 0x1E, 0x00, 0xBC, 0x02, 0x2E, 0x09, 0x00, 0x00, 0x00, 0x00}; // DEF 7.00, POW 23.50
      auto b = spire_mag_step_bytes(hu, 1, 0, fed);
      expect_eq(2350, word(b, 1)); // POW stays 23.50
      expect_eq(700, word(b, 0));
      expect_eq(30, b[2]); // LL recomputed: 7 + 23
      expect_eq(0x01, b[1]); // the step's kind (Varuna)
      expect_eq(0x00, b[3]);
      // a mirrored mag that would push the merge past 200 keeps its own stats
      Bytes big = {0x02, 0x05, 0xC8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x4E, 0x00, 0x00}; // DEX 200.00
      auto c = spire_mag_step_bytes(hu, 4, 0, big);
      expect_eq(200, c[2]);
      expect_eq(20000, word(c, 2));
    });

    f6_case("idempotence: EXAM twice, ASK after a resume", [&]() {
      SpireRunMagLedger led;
      led.run_id = 0x1234;
      led.start_step = spire_mag_start_step(table, 1);
      expect_eq(0u, led.start_step);
      auto d = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 1, {}, 0); // kit
      expect(d.result);
      expect_eq(0u, d.expected_step);
      expect_eq(0, led.issued_step); // counted at the answer (B-REGIE-14-10)
      // the same ASK again (a reloaded floor) never issues the step twice: the known degrade of the protocol
      auto again = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 1, {}, 0);
      expect(!again.result);
      expect(spire_run_mag_result(led, true));
      expect_eq(0, led.issued_step);
      expect(!spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 2, {}, 0).result); // the next room: nothing owed
      expect(spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0).result); // the Dragon
      expect(spire_run_mag_result(led, true));
      expect_eq(1, led.issued_step);
      // EXAM k8 twice issues one step
      auto e8 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 8, {}, 0);
      expect(e8.result);
      expect_eq(2u, e8.expected_step);
      expect(spire_run_mag_result(led, true));
      auto e8b = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 8, {}, 0);
      expect(!e8b.result);
      expect_eq(2u, e8b.expected_step);
      expect_eq(2, led.issued_step);
      expect(!spire_run_mag_result(led, true)); // nothing offered: a stray RESULT changes nothing
      expect_eq(2, led.issued_step);
      // the server's own boss clear of k8 counted in this game changes nothing either
      expect(!spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 9, {4, 8}, 0).result);
      // ASK after a resume: the ledger from the savestate holds 2 bosses and issued_step 2
      SpireRunMagLedger resumed;
      resumed.run_id = 0x1234;
      resumed.boss_rooms = {4, 8};
      resumed.issued_step = 2;
      resumed.last_mirrored = e8.bytes;
      auto r = spire_run_mag_decide(resumed, hu, SPIRE_RUN_MAG_ASK, 9, {}, 0);
      expect(!r.result);
      expect_eq(2u, r.expected_step);
      // B-BEL-4: a boss after Dark Falz grows the mag (Barba Ray: step 5); the last step, after Olga Flow, is the cap
      SpireRunMagLedger full;
      full.boss_rooms = {4, 8, 11, 15};
      full.issued_step = 4;
      auto e18 = spire_run_mag_decide(full, hu, SPIRE_RUN_MAG_EXAM, 18, {}, 0);
      expect(e18.result);
      expect_eq(5u, e18.expected_step);
      full.boss_rooms = {4, 8, 11, 15, 18, 21, 27, 30};
      full.issued_step = 8;
      expect(!spire_run_mag_decide(full, hu, SPIRE_RUN_MAG_EXAM, 30, {}, 0).result);
      expect_eq(8u, spire_run_mag_decide(full, hu, SPIRE_RUN_MAG_EXAM, 30, {}, 0).expected_step);
      expect_eq(4u, spire_mag_expected_step(5, 0, 5, true));
      // a practice run from k9 starts after two bosses
      expect_eq(2u, spire_mag_start_step(table, 9));
      expect_eq(0u, spire_mag_start_step(table, 4));
      // spel-af item 5: a single room k25 (Central Control Area) has six bosses before it, k28 (Seabed) seven
      expect_eq(6u, spire_mag_start_step(table, 25));
      expect_eq(7u, spire_mag_start_step(table, 28));
    });

    f6_case("pack full: the step stays owed", [&]() {
      SpireRunMagLedger led;
      led.issued_step = 0;
      led.last_mirrored = spire_mag_step_bytes(hu, 0, 0, std::nullopt);
      auto e = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0);
      expect(e.result);
      expect(spire_run_mag_result(led, false)); // item_create2 gave FFFFFFFF
      expect_eq(0, led.issued_step);
      expect(led.last_mirrored == spire_mag_step_bytes(hu, 0, 0, std::nullopt));
      auto a = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 5, {}, 0); // the next room start
      expect(a.result);
      expect(a.bytes == e.bytes);
      expect(led.boss_rooms.count(4));
    });

    // B-REGIE-14-10: a step counts as issued when the server answers it with result 1; a RESULT 0 gives it back, a
    // RESULT 1 only confirms, and a RESULT that never arrives never yields a second issue of the same step.
    f6_case("counting: answer, the RESULT lost, then ASK", [&]() {
      SpireRunMagLedger led;
      led.run_id = 0x2222;
      auto kit = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 1, {}, 0);
      expect(kit.result);
      expect_eq(0, led.issued_step); // counted at the answer
      // a floor load killed the thread after the swap, before the RESULT: the client holds the kit mag
      auto after = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 2, {}, 0);
      expect(!after.result);
      expect_eq(0u, after.expected_step);
      expect_eq(0, led.issued_step);
      // the mag of the lost RESULT shows in the overlay: the ledger takes it as created, so the savestate keeps it
      expect(spire_run_mag_follow_mirror(led, {kit.bytes}));
      expect(led.last_mirrored == kit.bytes);
      expect(!spire_run_mag_result(led, true)); // a RESULT after that has nothing to confirm
      expect_eq(0, led.issued_step);
      // the Dragon's EXAM, its RESULT lost, then the same EXAM again (a reloaded floor): one issue of step 1
      auto e4 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0);
      expect(e4.result);
      expect_eq(1, led.issued_step);
      expect(!spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0).result);
      expect(!spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 5, {}, 0).result);
      expect_eq(1, led.issued_step);
    });

    f6_case("counting: answer, RESULT 0, then ASK gives the same step", [&]() {
      SpireRunMagLedger led;
      led.run_id = 0x3333;
      led.issued_step = 0;
      led.last_mirrored = spire_mag_step_bytes(hu, 0, 0, std::nullopt);
      auto e4 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0);
      expect(e4.result);
      expect_eq(1, led.issued_step); // counted at the answer
      expect(spire_run_mag_result(led, false)); // pack full: the step is given back
      expect_eq(0, led.issued_step);
      expect(led.last_mirrored == spire_mag_step_bytes(hu, 0, 0, std::nullopt)); // the client kept the old mag
      auto a5 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 5, {}, 0);
      expect(a5.result);
      expect_eq(1u, a5.expected_step);
      expect(a5.bytes == e4.bytes);
      expect_eq(1, led.issued_step);
    });

    f6_case("counting: answer, RESULT 1, then the next boss's EXAM gives the next step", [&]() {
      SpireRunMagLedger led;
      led.run_id = 0x4444;
      led.issued_step = 0;
      led.last_mirrored = spire_mag_step_bytes(hu, 0, 0, std::nullopt);
      auto e4 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0);
      expect(e4.result);
      expect_eq(1, led.issued_step); // counted at the answer
      expect(spire_run_mag_result(led, true)); // only confirms
      expect_eq(1, led.issued_step);
      expect(led.last_mirrored == e4.bytes);
      expect(!spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 5, {}, 0).result);
      auto e8 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 8, {}, 0);
      expect(e8.result);
      expect_eq(2u, e8.expected_step);
      expect_eq(2, led.issued_step);
      expect(!(e8.bytes == e4.bytes));
    });

    f6_case("counting: a resume with the issued step persisted issues nothing again", [&]() {
      SpireRunMagLedger led;
      led.run_id = 0x5555;
      led.issued_step = 1;
      led.boss_rooms = {4};
      led.last_mirrored = spire_mag_step_bytes(hu, 1, 0, std::nullopt);
      auto e8 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 8, {}, 0);
      expect(e8.result);
      led.lean = SPIRE_RUN_MAG_LEAN_SHELL; // F10: a Shell run (chosen at the kit)
      // the RESULT is lost; the next room start writes the savestate with this ledger
      SpireSavestate ss;
      ss.run_id = led.run_id;
      ss.values.assign(SPIRE_SAVESTATE_NUM_VALUES, 0);
      ss.run_mag = led;
      auto back = spire_savestate_from_json(spire_savestate_to_json(ss, 1));
      expect(back.run_mag.has_value());
      expect_eq(2, back.run_mag->issued_step);
      SpireRunMagLedger resumed = *back.run_mag;
      expect(!spire_run_mag_decide(resumed, hu, SPIRE_RUN_MAG_ASK, 9, {}, 0).result);
      expect(!spire_run_mag_decide(resumed, hu, SPIRE_RUN_MAG_EXAM, 8, {}, 0).result);
      expect_eq(2, resumed.issued_step);
      // F10: the lean comes back with the ledger; a resume shows no menu, so the next EXAM carries lean bits 0 and
      // still gets the Shell row (the column follows the ledger, not the bits)
      expect_eq(SPIRE_RUN_MAG_LEAN_SHELL, resumed.lean);
      auto last = resumed.last_mirrored;
      auto e11 = spire_run_mag_handle(resumed, table, hu, SPIRE_RUN_MAG_EXAM, 11, {}, {}, 0, 0);
      expect(e11.decision.result && e11.shell);
      expect_eq(3u, e11.decision.expected_step);
      expect(e11.decision.bytes == spire_mag_step_bytes(spire_mag_column(hu, SPIRE_RUN_MAG_LEAN_SHELL).line, 3, 0, last));
      expect(!(e11.decision.bytes == spire_mag_step_bytes(hu, 3, 0, last)));
      expect_eq(4200, word(e11.decision.bytes, 0)); // DEF 42: ShellSteps row 3
      expect_eq(SPIRE_RUN_MAG_LEAN_SHELL, resumed.lean);
      // an old savestate without "Lean" loads as no choice (the default column)
      SpireRunMagLedger old_led = led;
      old_led.lean = SPIRE_RUN_MAG_LEAN_NONE;
      ss.run_mag = old_led;
      std::string old_json = spire_savestate_to_json(ss, 1);
      size_t lean_pos = old_json.find("\"Lean\"");
      expect(lean_pos != std::string::npos);
      old_json.replace(lean_pos, 6, "\"Xean\"");
      expect_eq(SPIRE_RUN_MAG_LEAN_NONE, spire_savestate_from_json(old_json).run_mag->lean);
      // fix round (F10S critic N6): a "Lean" outside 0-2 (an edited file) reads as no choice, never as a wrapped byte
      auto with_lean = [&](const std::string& value) {
        SpireRunMagLedger l = led;
        l.lean = SPIRE_RUN_MAG_LEAN_FANG;
        ss.run_mag = l;
        std::string j = spire_savestate_to_json(ss, 1);
        size_t pos = j.find("\"Lean\"");
        size_t colon = j.find(':', pos);
        size_t end = j.find_first_of(",}\n", colon);
        j.replace(colon + 1, end - colon - 1, " " + value);
        return spire_savestate_from_json(j).run_mag->lean;
      };
      expect_eq(SPIRE_RUN_MAG_LEAN_SHELL, with_lean("2"));
      expect_eq(SPIRE_RUN_MAG_LEAN_NONE, with_lean("258")); // would wrap to 2 (Shell) as a uint8_t
      expect_eq(SPIRE_RUN_MAG_LEAN_NONE, with_lean("3"));
      expect_eq(SPIRE_RUN_MAG_LEAN_NONE, with_lean("-1"));
      // fix round (F10S critic N1): a savestate with run id 0 is no savestate (a run with id 0 has no ledger, and the
      // 'Z' handler would use none); the file is refused as unreadable, never half read
      SpireSavestate zero = ss;
      zero.run_id = 0;
      zero.run_mag.reset();
      expect_raises(std::runtime_error, [&]() { spire_savestate_from_json(spire_savestate_to_json(zero, 1)); });
    });

    f6_case("review r3 B3: rule B's part of the ledger (Need, BoostDue, FeedLevels) comes back from the savestate file", [&]() {
      SpireRunMagLedger led;
      led.run_id = 0x5556;
      led.issued_step = 2;
      led.last_mirrored = spire_mag_step_bytes(hu, 2, 0, std::nullopt);
      led.need = SpireMagNeed{57, 31, 4};
      led.boost_due = true;
      led.feed_levels = 20;
      led.disk_mind = 4; // part B: the MIND the run's reward disks ask
      SpireSavestate ss;
      ss.run_id = led.run_id;
      ss.values.assign(SPIRE_SAVESTATE_NUM_VALUES, 0);
      ss.run_mag = led;
      std::string json = spire_savestate_to_json(ss, 1);
      auto back = spire_savestate_from_json(json);
      expect(back.run_mag.has_value());
      expect(back.run_mag->need == led.need);
      expect(back.run_mag->boost_due);
      expect_eq(20, back.run_mag->feed_levels);
      expect_eq(4, back.run_mag->disk_mind);
      // the resumed ledger answers the owed boost at the next ASK without a new step: the weapon's need and the food
      auto d = spire_run_mag_decide(*back.run_mag, hu, SPIRE_RUN_MAG_ASK, 3, {}, 0, 1);
      expect(d.result);
      expect(word(d.bytes, 1) >= std::max<uint32_t>(5700, word(*led.last_mirrored, 1) + 2000));
      expect(word(d.bytes, 2) >= 3100);
      expect(word(d.bytes, 3) >= 400);
      // nothing owed: none of the three keys is written, and the ledger reads back owing nothing
      SpireRunMagLedger plain = led;
      plain.need = SpireMagNeed{};
      plain.boost_due = false;
      plain.feed_levels = 0;
      plain.disk_mind = 0;
      ss.run_mag = plain;
      std::string plain_json = spire_savestate_to_json(ss, 1);
      expect((plain_json.find("\"Need\"") == std::string::npos) && (plain_json.find("\"BoostDue\"") == std::string::npos) &&
          (plain_json.find("\"FeedLevels\"") == std::string::npos) && (plain_json.find("\"DiskMind\"") == std::string::npos));
      auto plain_back = spire_savestate_from_json(plain_json);
      expect((plain_back.run_mag->need == SpireMagNeed{}) && !plain_back.run_mag->boost_due && !plain_back.run_mag->feed_levels &&
          !plain_back.run_mag->disk_mind);
      // an edited file: a value below 0 is nothing owed and one above 200 is 200, never a wrapped number
      auto edited = [&](const std::string& key, const std::string& value) {
        std::string j = json;
        size_t pos = j.find("\"" + key + "\"");
        expect(pos != std::string::npos);
        size_t colon = j.find(':', pos);
        size_t end = (key == "Need") ? (j.find(']', colon) + 1) : j.find_first_of(",}\n", colon);
        j.replace(colon + 1, end - colon - 1, " " + value);
        return *spire_savestate_from_json(j).run_mag;
      };
      expect(edited("Need", "[-1, 999, 7]").need == (SpireMagNeed{0, 200, 7}));
      expect_eq(0, edited("FeedLevels", "-1").feed_levels);
      expect_eq(200, edited("FeedLevels", "70000").feed_levels);
      expect_eq(0, edited("DiskMind", "-1").disk_mind);
      expect_eq(200, edited("DiskMind", "70000").disk_mind);
    });

    f6_case("the ledger follows the feed mirror", [&]() {
      SpireRunMagLedger led;
      auto issued = spire_mag_step_bytes(hu, 1, 0, std::nullopt);
      Bytes fed = issued;
      fed[6] = 0x2E; // POW 18.00 -> 23.50
      fed[7] = 0x09;
      fed[2] = 30;
      expect(!spire_run_mag_follow_mirror(led, {fed})); // no mag issued yet: nothing to follow
      led.issued_step = 1;
      led.last_mirrored = issued;
      expect(!spire_run_mag_follow_mirror(led, {fed, issued})); // two type-02 items: never guess
      expect(!spire_run_mag_follow_mirror(led, {})); // the mag is gone (dropped): the ledger keeps its bytes
      expect(spire_run_mag_follow_mirror(led, {fed}));
      expect(led.last_mirrored == fed);
      expect(!spire_run_mag_follow_mirror(led, {issued})); // a lower level is not a fed run mag
      expect(led.last_mirrored == fed);
    });

    f6_case("refusals", [&]() {
      using R = SpireRunMagRefusal;
      SpireRunMagState ok;
      ok.gc_v3 = true;
      ok.spire_game = true;
      ok.has_overlay = true;
      ok.run_in_progress = true;
      ok.enabled = true;
      ok.has_table = true;
      ok.overlay_mags = 1;
      expect_eq(R::NONE, spire_run_mag_refusal(ok));
      auto with = [&](const std::function<void(SpireRunMagState&)>& fn) {
        SpireRunMagState st = ok;
        fn(st);
        return spire_run_mag_refusal(st);
      };
      expect_eq(R::NOT_GC_V3, with([](auto& st) { st.gc_v3 = false; }));
      expect_eq(R::NO_SPIRE_GAME, with([](auto& st) { st.spire_game = false; }));
      expect_eq(R::NO_OVERLAY, with([](auto& st) { st.has_overlay = false; }));
      expect_eq(R::RESTORE_PENDING, with([](auto& st) { st.restore_pending = true; }));
      expect_eq(R::MARKER, with([](auto& st) { st.run_in_progress = false; }));
      expect_eq(R::BASE_RETURNED, with([](auto& st) { st.base_returned = true; }));
      expect_eq(R::RUN_AGAIN, with([](auto& st) { st.run_again = true; }));
      expect_eq(R::REFRESHED_BY_61, with([](auto& st) { st.refreshed_by_61 = true; }));
      expect_eq(R::DISABLED, with([](auto& st) { st.enabled = false; }));
      expect_eq(R::NO_TABLE, with([](auto& st) { st.has_table = false; }));
      expect_eq(R::TWO_MAGS, with([](auto& st) { st.overlay_mags = 2; }));
      expect_eq(R::NONE, with([](auto& st) { st.overlay_mags = 0; }));
      // amendment 8: the q925 base game, the real character with its real mag live and no run copy
      expect_eq(R::NO_OVERLAY, with([](auto& st) {
        st.has_overlay = false;
        st.overlay_mags = 1;
      }));
      // Enabled false answers 0 even where everything else would give a mag
      expect_eq(R::DISABLED, with([](auto& st) { st.enabled = false; st.has_table = false; }));
      expect_eq(std::string("BASE_RETURNED"), std::string(spire_run_mag_refusal_name(R::BASE_RETURNED)));
      expect_eq(std::string("NO_LEDGER"), std::string(spire_run_mag_refusal_name(R::NO_LEDGER))); // fix round N1
      expect_eq(0x5A000000u, SPIRE_RUN_MAG_TAG);
      expect_eq(0x5A000601u, spire_run_mag_answer(SPIRE_RUN_MAG_TAG, 0x06, true));
      expect_eq(0x5A00FF00u, spire_run_mag_answer(SPIRE_RUN_MAG_TAG, SPIRE_RUN_MAG_NO_OLD, false));
    });

    // routeb-f6-critic.md C3-6: each case below fails under the mutation it names
    f6_case("handler: old_type is data1[1] of the one run mag, else 0xFF (C3-6 mutation 1)", [&]() {
      SpireRunMagLedger led;
      auto kit = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_ASK, 1, {}, {}, 0, 0);
      expect(kit.decision.result);
      expect_eq(SPIRE_RUN_MAG_NO_OLD, kit.old_type); // no mag yet: the quest deletes nothing
      expect_eq(0x5A00FF01u, kit.answer);
      expect(spire_run_mag_result(led, true));
      // the Dragon: the run copy holds the kit mag (kind 0x00), so the quest must delete kind 0x00 after the create
      auto dragon = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_EXAM, 4, {kit.decision.bytes}, {}, 0, 0);
      expect(dragon.decision.result);
      expect_eq(kit.decision.bytes[1], dragon.old_type);
      expect_eq(0x00, dragon.old_type);
      expect_eq(0x5A000001u, dragon.answer);
      expect(spire_run_mag_result(led, true));
      // De Rol Le with the Dragon's mag (Varuna, kind 0x01) in the pack
      auto rol = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_EXAM, 8, {dragon.decision.bytes}, {}, 0, 0);
      expect(rol.decision.result);
      expect_eq(0x01, dragon.decision.bytes[1]);
      expect_eq(0x01, rol.old_type);
      expect_eq(0x5A000101u, rol.answer);
      // two type-02 items: never name one (the refusal catches this first; the pure part stays safe on its own)
      SpireRunMagLedger two;
      expect_eq(SPIRE_RUN_MAG_NO_OLD,
          spire_run_mag_handle(two, table, hu, SPIRE_RUN_MAG_ASK, 1, {kit.decision.bytes, dragon.decision.bytes}, {}, 0, 0).old_type);
      // a refusal's answer
      expect_eq(0x5A00FF00u, SpireRunMagHandled().answer);
    });

    f6_case("handler: a lost EXAM is counted from the server's boss clears (C3-6 mutation 2)", [&]() {
      // Run from room 1: the kit mag issued; the EXAM of k4 never arrived, but the server counted the Dragon's clear
      SpireRunMagLedger led;
      led.issued_step = 0;
      led.last_mirrored = spire_mag_step_bytes(hu, 0, 0, std::nullopt);
      auto a5 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 5, {4}, 0);
      expect(a5.result);
      expect_eq(1u, a5.expected_step);
      expect(a5.bytes == spire_mag_step_bytes(hu, 1, 0, spire_mag_step_bytes(hu, 0, 0, std::nullopt)));
      expect(led.boss_rooms == std::set<uint8_t>({4}));
      expect_eq(1, led.issued_step);
    });

    f6_case("'V': the ledger never follows an overlay a 61 refreshed (C3-6 mutation 3, amendment 3)", [&]() {
      SpireRunMagLedger led;
      led.issued_step = 1;
      led.last_mirrored = spire_mag_step_bytes(hu, 1, 0, std::nullopt);
      // after a push the refreshed overlay holds the real mag (Pitri LV200, above the run mag's level)
      const Bytes pitri = {0x02, 0x1D, 0xC8, 0xA3, 0xF4, 0x01, 0x1C, 0x25, 0x10, 0x27, 0x00, 0x00};
      expect(!spire_run_mag_savestate_follow(&led, true, {pitri}));
      expect(led.last_mirrored == spire_mag_step_bytes(hu, 1, 0, std::nullopt));
      // the same overlay without the 61 is a fed run mag: followed (the gate is the only difference)
      SpireRunMagLedger fed = led;
      expect(spire_run_mag_savestate_follow(&fed, false, {pitri}));
      expect(fed.last_mirrored == pitri);
      expect(!spire_run_mag_savestate_follow(nullptr, false, {pitri}));
    });

    f6_case("handler: the start step is set by the first ASK only (C3-6 mutation 5)", [&]() {
      // A run from room 1: kit at k1, the Dragon and De Rol Le examined, then an ASK at k9 (a non-boss clear)
      SpireRunMagLedger led;
      auto kit = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_ASK, 1, {}, {}, 0, 0);
      expect_eq(0u, led.start_step);
      expect(spire_run_mag_result(led, true));
      auto e4 = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_EXAM, 4, {kit.decision.bytes}, {4}, 0, 0);
      expect(spire_run_mag_result(led, true));
      auto e8 = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_EXAM, 8, {e4.decision.bytes}, {4, 8}, 0, 0);
      expect_eq(2u, e8.decision.expected_step);
      expect(spire_run_mag_result(led, true));
      auto a9 = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_ASK, 9, {e8.decision.bytes}, {4, 8}, 0, 0);
      expect_eq(0u, led.start_step); // spire_mag_start_step(table, 9) would be 2
      expect_eq(2u, a9.decision.expected_step);
      expect(!a9.decision.result); // no skipped step
      expect_eq(2, led.issued_step);
      // a practice run that first asks at k9 starts after two bosses
      SpireRunMagLedger practice;
      auto p9 = spire_run_mag_handle(practice, table, hu, SPIRE_RUN_MAG_ASK, 9, {}, {}, 0, 0);
      expect_eq(2u, practice.start_step);
      expect(p9.decision.result);
      expect_eq(2u, p9.decision.expected_step);
    });

    f6_case("the offered mag is confirmed by the mirror only as the one type-02 item (C3-6 mutation 6)", [&]() {
      SpireRunMagLedger led;
      led.issued_step = 0;
      led.last_mirrored = spire_mag_step_bytes(hu, 0, 0, std::nullopt);
      auto e4 = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0);
      expect(e4.result && led.offered);
      // the offered mag next to the old one (created, old not deleted yet): not confirmed, the offer stays open
      expect(!spire_run_mag_follow_mirror(led, {e4.bytes, *led.last_mirrored}));
      expect(led.offered && (*led.offered == e4.bytes));
      expect(led.last_mirrored == spire_mag_step_bytes(hu, 0, 0, std::nullopt));
      // alone: confirmed as by a RESULT 1
      expect(spire_run_mag_follow_mirror(led, {e4.bytes}));
      expect(!led.offered && (led.last_mirrored == e4.bytes));
    });

    f6_case("texts", [&]() {
      expect_eq(std::string("Your mag evolves: Varuna. Equip it: Item Pack, Mag."), spire_run_mag_evolve_text("Varuna"));
      for (const std::string& t : {spire_run_mag_evolve_text("Ashvinau"), std::string(SPIRE_RUN_MAG_PACK_FULL_TEXT),
               std::string(SPIRE_RUN_MAG_REMINDER_TEXT), std::string(SPIRE_BOSS_BURST_TEXT),
               std::string(SPIRE_KIT_USE_TEXT), std::string(SPIRE_RESUME_MAG_TEXT)}) {
        expect_eq(t, spire_ticker_line(t)); // one ticker line each
      }
      expect_eq(std::string("Your Spire Mag comes back: equip it."), std::string(SPIRE_RESUME_MAG_TEXT));
      // bundle 2 S: the burst text names no mag (a run may have none) and holds for Shifta only and Shifta + Deband
      expect_eq(std::string("Boss ahead: you are buffed"), std::string(SPIRE_BOSS_BURST_TEXT));
      expect(std::string(SPIRE_BOSS_BURST_TEXT).find("mag") == std::string::npos);
      expect(std::string(SPIRE_BOSS_BURST_TEXT).find("Mag") == std::string::npos);
    });

    // F10 mag lean (routeb2-f10-randgevallen.md, "Besluit na de critic" 1, 2, 6, 7, 8)
    const auto hu_shell = spire_mag_column(hu, SPIRE_RUN_MAG_LEAN_SHELL).line;
    using LS = SpireRunMagLeanStore;

    f6_case("F10: a Shell run with a start bonus starts on the start floor (fix round, F10S critic N2)", [&]() {
      // the floors are one list for both columns: spire_mag_column must carry them into the Shell line
      expect(spire_mag_step_bytes(hu_shell, 0, 14, std::nullopt) == spire_mag_step_bytes(hu, 0, 14, std::nullopt));
      SpireRunMagLedger led;
      auto kit = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_ASK, 1, {}, {}, 14, SPIRE_RUN_MAG_LEAN_SHELL);
      expect(kit.decision.result && kit.shell);
      expect_eq(0x01, kit.decision.bytes[1]); // Varuna L22, not Mag 5/0/0/0
      expect_eq(1100, word(kit.decision.bytes, 0));
      expect_eq(1000, word(kit.decision.bytes, 1));
      auto s1 = spire_mag_step_bytes(hu_shell, 1, 14, std::nullopt); // Shell 18/7/0/0 merged with the floor 11/10/0/1
      expect_eq(1800, word(s1, 0));
      expect_eq(1000, word(s1, 1)); // the floor's POW 10 beats the row's 7
      expect_eq(100, word(s1, 3));
      expect_eq(0x01, s1[1]);
    });

    f6_case("F10: the lean is bits 8-15 of the ASK/EXAM word", [&]() {
      expect_eq(2, spire_run_mag_lean_bits(0x5A040204)); // 'Z' EXAM, lean 2 (Shell), k4
      expect_eq(1, spire_run_mag_lean_bits(0x5A010101)); // 'Z' ASK, lean 1 (Fang/Mind), k1
      expect_eq(0, spire_run_mag_lean_bits(0x5A010001)); // an ASK from a quest before F10: no choice
      expect_eq(0x2A, spire_run_mag_lean_bits(0x5A012A01));
    });

    f6_case("F10: lean 2 picks the DEF-heavy row, lean 0 and 1 the default; only the row differs", [&]() {
      for (uint8_t lean : {SPIRE_RUN_MAG_LEAN_NONE, SPIRE_RUN_MAG_LEAN_FANG, SPIRE_RUN_MAG_LEAN_SHELL}) {
        bool is_shell = (lean == SPIRE_RUN_MAG_LEAN_SHELL);
        SpireRunMagLedger led;
        auto kit = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_ASK, 1, {}, {}, 0, lean);
        expect_eq(lean ? LS::STORED : LS::NONE, kit.lean_store);
        expect_eq(lean, led.lean);
        expect(kit.decision.result);
        expect(kit.decision.bytes == spire_mag_step_bytes(hu, 0, 0, std::nullopt)); // step 0 the same for every lean
        expect_eq(0x5A00FF01u, kit.answer);
        expect(spire_run_mag_result(led, true));
        expect(!led.offered_shell);
        auto e4 = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_EXAM, 4, {kit.decision.bytes}, {4}, 0, lean);
        expect(e4.decision.result);
        expect_eq(1u, e4.decision.expected_step);
        expect_eq(0x00, e4.old_type);
        expect_eq(0x5A000001u, e4.answer); // the same answer for every lean: create the new mag, delete kind 0x00
        expect(!e4.shell_fell_back);
        expect_eq(is_shell, e4.shell);
        expect_eq(is_shell, led.offered_shell);
        expect(e4.decision.bytes == spire_mag_step_bytes(is_shell ? hu_shell : hu, 1, 0, kit.decision.bytes));
        expect_eq(is_shell ? 1800 : 700, word(e4.decision.bytes, 0)); // DEF
        expect_eq(is_shell ? 700 : 1800, word(e4.decision.bytes, 1)); // POW
        expect_eq(0x01, e4.decision.bytes[1]); // Varuna in both columns (Sega's HU evolution reads no DEF)
        expect(spire_run_mag_result(led, true));
        expect(!led.offered_shell);
      }
    });

    f6_case("F10: a lean after a step above the kit's is ignored, as is a value above 2 (sub 5 too)", [&]() {
      SpireRunMagLedger led;
      led.issued_step = 1;
      led.boss_rooms = {4};
      led.last_mirrored = spire_mag_step_bytes(hu, 1, 0, std::nullopt);
      auto e8 = spire_run_mag_handle(led, table, hu, SPIRE_RUN_MAG_EXAM, 8, {*led.last_mirrored}, {4, 8}, 0,
          SPIRE_RUN_MAG_LEAN_SHELL);
      expect_eq(LS::AFTER_STEP, e8.lean_store);
      expect_eq(SPIRE_RUN_MAG_LEAN_NONE, led.lean);
      expect(e8.decision.result && !e8.shell);
      expect(e8.decision.bytes == spire_mag_step_bytes(hu, 2, 0, spire_mag_step_bytes(hu, 1, 0, std::nullopt)));
      // a Fang run never turns Shell midway either
      SpireRunMagLedger fang;
      fang.issued_step = 1;
      fang.lean = SPIRE_RUN_MAG_LEAN_FANG;
      expect_eq(LS::AFTER_STEP, spire_run_mag_lean_store(fang, SPIRE_RUN_MAG_LEAN_SHELL));
      expect_eq(SPIRE_RUN_MAG_LEAN_FANG, fang.lean);
      // with only the kit's step issued (0) the lean is still taken, and a second choice replaces the first
      SpireRunMagLedger kit;
      kit.issued_step = 0;
      expect_eq(LS::STORED, spire_run_mag_lean_store(kit, SPIRE_RUN_MAG_LEAN_SHELL));
      expect_eq(SPIRE_RUN_MAG_LEAN_SHELL, kit.lean);
      expect_eq(LS::STORED, spire_run_mag_lean_store(kit, SPIRE_RUN_MAG_LEAN_FANG));
      expect_eq(SPIRE_RUN_MAG_LEAN_FANG, kit.lean);
      // 0 stores nothing (it keeps a choice made earlier); above 2 is ignored
      SpireRunMagLedger none;
      expect_eq(LS::STORED, spire_run_mag_lean_store(none, SPIRE_RUN_MAG_LEAN_SHELL));
      expect_eq(LS::NONE, spire_run_mag_lean_store(none, SPIRE_RUN_MAG_LEAN_NONE));
      expect_eq(SPIRE_RUN_MAG_LEAN_SHELL, none.lean);
      expect_eq(LS::INVALID, spire_run_mag_lean_store(none, 3));
      expect_eq(LS::INVALID, spire_run_mag_lean_store(none, 0xFF));
      expect_eq(SPIRE_RUN_MAG_LEAN_SHELL, none.lean);
      expect_eq(std::string("stored"), std::string(spire_run_mag_lean_store_name(LS::STORED)));
    });

    f6_case("F10: a practice run from k >= 5 with Shell in its first ASK gets the Shell row of its start step", [&]() {
      SpireRunMagLedger p;
      auto p9 = spire_run_mag_handle(p, table, hu, SPIRE_RUN_MAG_ASK, 9, {}, {}, 0, SPIRE_RUN_MAG_LEAN_SHELL);
      expect_eq(LS::STORED, p9.lean_store);
      expect_eq(2u, p.start_step);
      expect(p9.decision.result && p9.shell);
      expect_eq(2u, p9.decision.expected_step);
      expect(p9.decision.bytes == spire_mag_row_bytes(hu.shell_steps[2])); // 30/9/1/0 Rudra
      expect_eq(3000, word(p9.decision.bytes, 0));
      expect_eq(900, word(p9.decision.bytes, 1));
      // FO from k16 (after four bosses): the last Shell step, Bana (FO's evolution reads DEF from L50 on)
      SpireRunMagLedger f;
      auto f16 = spire_run_mag_handle(f, table, table.classes.at("FO"), SPIRE_RUN_MAG_ASK, 16, {}, {}, 0,
          SPIRE_RUN_MAG_LEAN_SHELL);
      expect(f16.decision.result && f16.shell);
      expect_eq(4u, f16.decision.expected_step);
      expect_eq(0x24, f16.decision.bytes[1]);
      expect_eq(4600, word(f16.decision.bytes, 0)); // DEF
      expect_eq(1400, word(f16.decision.bytes, 3)); // MIND
    });

    f6_case("F10: a json without, with a short or with a bad ShellSteps answers Steps, never NO_TABLE", [&]() {
      const std::string row0 = R"({"Id": 0, "Def": 5, "Pow": 0, "Dex": 0, "Mind": 0})";
      const std::string fang1 = R"({"Id": 1, "Def": 7, "Pow": 18, "Dex": 0, "Mind": 0})";
      const std::string shell1 = R"({"Id": 1, "Def": 18, "Pow": 7, "Dex": 0, "Mind": 0})";
      auto json_with = [&](const std::string& shell) {
        return R"({"Enabled": true, "Classes": {"HU": {"Steps": [)" + row0 + ", " + fang1 + "]" + shell +
            R"(}}, "BossRooms": [4, 8]})";
      };
      auto steps_answer = [&](const SpireMagLine& line, bool want_shell) {
        SpireRunMagLedger led;
        led.issued_step = 0;
        led.lean = SPIRE_RUN_MAG_LEAN_SHELL;
        led.last_mirrored = spire_mag_row_bytes(line.steps[0]);
        auto t = spire_mag_parse(json_with(""));
        auto e4 = spire_run_mag_handle(led, t, line, SPIRE_RUN_MAG_EXAM, 4, {*led.last_mirrored}, {4}, 0, 0);
        expect(e4.decision.result); // a mag is answered: the table stays usable
        expect_eq(want_shell, e4.shell);
        expect_eq(!want_shell, e4.shell_fell_back);
        expect_eq(want_shell, led.offered_shell);
        expect_eq(want_shell ? 1800 : 700, word(e4.decision.bytes, 0));
      };
      for (const std::string& bad : {std::string(""), ", \"ShellSteps\": [" + row0 + "]",
               ", \"ShellSteps\": [" + row0 + R"(, {"Id": 1, "Def": 300, "Pow": 7, "Dex": 0, "Mind": 0}])",
               std::string(", \"ShellSteps\": 7"), ", \"ShellSteps\": [" + row0 + R"(, {"Id": 1, "Def": 18}])"}) {
        auto t = spire_mag_parse(json_with(bad)); // throws on none of them
        const auto& line = t.classes.at("HU");
        expect(line.shell_steps.empty());
        expect(!line.shell_note.empty());
        auto col = spire_mag_column(line, SPIRE_RUN_MAG_LEAN_SHELL);
        expect(col.fell_back && !col.shell);
        steps_answer(line, false);
      }
      expect_eq(std::string("no ShellSteps"), spire_mag_parse(json_with("")).classes.at("HU").shell_note);
      expect_eq(std::string("ShellSteps has 1 rows, Steps 2"),
          spire_mag_parse(json_with(", \"ShellSteps\": [" + row0 + "]")).classes.at("HU").shell_note);
      // the same json with a good ShellSteps: the Shell row
      auto good = spire_mag_parse(json_with(", \"ShellSteps\": [" + row0 + ", " + shell1 + "]"));
      expect_eq(std::string(""), good.classes.at("HU").shell_note);
      expect_eq(2u, good.classes.at("HU").shell_steps.size());
      steps_answer(good.classes.at("HU"), true);
      // a line whose shell_steps is not as long as steps (built by hand) falls back as well
      SpireMagLine short_line = hu;
      short_line.shell_steps.pop_back();
      short_line.shell_note.clear();
      auto col = spire_mag_column(short_line, SPIRE_RUN_MAG_LEAN_SHELL);
      expect(col.fell_back && !col.shell);
      expect_eq(hu.steps.size(), col.line.steps.size());
      expect(spire_mag_row_bytes(col.line.steps[1]) == spire_mag_row_bytes(hu.steps[1]));
      // lean 0 and 1 never fall back (nothing to fall back from)
      expect(!spire_mag_column(short_line, SPIRE_RUN_MAG_LEAN_FANG).fell_back);
      expect(!spire_mag_column(short_line, SPIRE_RUN_MAG_LEAN_NONE).fell_back);
    });

    f6_case("F10: the evolve text names Shell", [&]() {
      expect_eq(std::string("Your mag evolves: Varuna (Shell). Equip it: Item Pack, Mag."),
          spire_run_mag_evolve_text("Varuna", true));
      expect_eq(std::string("Your mag evolves: Varuna. Equip it: Item Pack, Mag."), spire_run_mag_evolve_text("Varuna", false));
      expect_eq(spire_run_mag_evolve_text("Bana", true), spire_ticker_line(spire_run_mag_evolve_text("Bana", true)));
      // fix round (F10S critic N4): the first mag of a run (the kit, or a practice run's first ASK) names Shell too
      expect_eq(std::string("Your mag (Shell) is in your pack: equip it (Item Pack, Mag)"), spire_run_mag_reminder_text(true));
      expect_eq(std::string(SPIRE_RUN_MAG_REMINDER_TEXT), spire_run_mag_reminder_text(false));
      expect_eq(std::string("Your mag is in your pack: equip it (Item Pack, Mag)"), spire_run_mag_reminder_text(false));
      // no run mag line is longer than the longest before this round: every Id of the shipped table, both columns
      auto names = phosg::JSON::parse(phosg::load_file("system/tables/names-v3.json"));
      size_t longest = 0;
      for (const auto& [cls, line] : table.classes) {
        std::vector<SpireMagRow> rows = line.steps;
        rows.insert(rows.end(), line.shell_steps.begin(), line.shell_steps.end());
        for (const auto& f : line.floors) {
          rows.emplace_back(f.row);
        }
        for (const auto& row : rows) {
          std::string name = names.get_string(std::format("02{:02X}00", row.id));
          for (bool shell : {false, true}) {
            longest = std::max(longest, spire_run_mag_evolve_text(name, shell).size());
          }
        }
      }
      for (bool shell : {false, true}) {
        longest = std::max(longest, spire_run_mag_reminder_text(shell).size());
      }
      expect_eq(SPIRE_RUN_MAG_TICKER_MAX, longest); // Kabanda (Shell): 60 (veilig-1: ", " for " > ")
      expect_eq(60u, spire_run_mag_evolve_text("Kabanda", true).size());
      expect(spire_run_mag_reminder_text(true).size() <= SPIRE_RUN_MAG_TICKER_MAX);
    });

    f6_case("bundle 2 S: the kit text after 'M' START", [&]() {
      expect(spire_kit_use_text_due(true, 1, 0, false, false)); // hp_mat only
      // hp_mat and atp_grind together (atp_grind alone: the bundle 8 case "kit ticker: materials and grinders")
      expect(spire_kit_use_text_due(true, 3, 3, false, false));
      expect(!spire_kit_use_text_due(true, 0, 0, false, false)); // no kit level (an old meta file): no text
      expect(!spire_kit_use_text_due(false, 1, 1, false, false)); // LIST or BUY: no text
      expect(!spire_kit_use_text_due(true, 1, 1, true, false)); // resumed: the kit is not given
      expect(!spire_kit_use_text_due(true, 1, 1, false, true)); // a second START of this client in this run
    });

    f6_case("bundle 2 S: the boss burst text at the boss door", [&]() {
      std::set<uint8_t> sent;
      expect(!spire_boss_burst_text_due(4, table.boss_rooms, sent)); // the Dragon: no burst
      expect(!spire_boss_burst_text_due(7, table.boss_rooms, sent)); // no boss room
      expect(spire_boss_burst_text_due(8, table.boss_rooms, sent)); // De Rol Le
      expect(!spire_boss_burst_text_due(8, table.boss_rooms, sent)); // the same room again in this run: no second line
      expect(!spire_boss_burst_text_due(9, table.boss_rooms, sent));
      expect(spire_boss_burst_text_due(11, table.boss_rooms, sent)); // Vol Opt
      expect(sent == std::set<uint8_t>({8, 11}));
      std::set<uint8_t> other_client; // per client: the partner's own 'V' gets its own line
      expect(spire_boss_burst_text_due(8, table.boss_rooms, other_client));
      std::set<uint8_t> none;
      expect(!spire_boss_burst_text_due(8, {}, none)); // no boss rooms known: no text
    });

    f6_case("bundle 2 S: only an in-game 61 marks the run copy refreshed", [&]() {
      expect(spire_run_mag_command_refreshes(0x61));
      expect(!spire_run_mag_command_refreshes(0x98));
      expect(!spire_run_mag_command_refreshes(0x00));
    });

    f6_case("pack-full notice: once per given-back step per run (hotfix 1)", [&]() {
      SpireRunMagPackFullNotices told;
      expect(spire_run_mag_pack_full_notice(told, 0x64D5C0EF, true, false, 1)); // first RESULT 0 for step 1
      expect(!spire_run_mag_pack_full_notice(told, 0x64D5C0EF, true, false, 1)); // the same step again: silent
      expect(spire_run_mag_pack_full_notice(told, 0x64D5C0EF, true, false, 2)); // a later step: told
      expect(!spire_run_mag_pack_full_notice(told, 0x64D5C0EF, true, false, 2));
      expect(spire_run_mag_pack_full_notice(told, 0x11111111, true, false, 1)); // a new run: told again
      expect(!spire_run_mag_pack_full_notice(told, 0x11111111, true, true, 3)); // RESULT 1: never
      expect(!spire_run_mag_pack_full_notice(told, 0x11111111, false, false, 4)); // no offer settled: never
      expect(spire_run_mag_pack_full_notice(told, 0x11111111, true, false, 3)); // RESULT 1 recorded nothing
      // the notice leaves the ledger as spire_run_mag_result left it
      SpireRunMagLedger led;
      led.run_id = 0x22222222;
      led.issued_step = 0;
      led.last_mirrored = spire_mag_step_bytes(hu, 0, 0, std::nullopt);
      auto e = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_EXAM, 4, {}, 0);
      expect(e.result && (led.offered_step == 1));
      int32_t offered_step = led.offered_step;
      expect(spire_run_mag_result(led, false));
      expect_eq(0, led.issued_step);
      auto before = led;
      expect(spire_run_mag_pack_full_notice(told, led.run_id, true, false, offered_step));
      expect_eq(before.issued_step, led.issued_step);
      expect(before.offered == led.offered);
      expect(before.last_mirrored == led.last_mirrored);
      expect(before.boss_rooms == led.boss_rooms);
      expect(before.offered_shell == led.offered_shell);
      // a Shell run: the same step is given back and offered again at the next ASK (the text names no column)
      SpireRunMagLedger sl;
      sl.run_id = 0x33333333;
      sl.issued_step = 0;
      sl.lean = SPIRE_RUN_MAG_LEAN_SHELL;
      auto col = spire_mag_column(hu, sl.lean);
      expect(col.shell);
      sl.last_mirrored = spire_mag_step_bytes(col.line, 0, 0, std::nullopt);
      expect(spire_run_mag_decide(sl, col.line, SPIRE_RUN_MAG_EXAM, 4, {}, 0).result);
      int32_t shell_step = sl.offered_step;
      expect(spire_run_mag_result(sl, false));
      expect(spire_run_mag_pack_full_notice(told, sl.run_id, true, false, shell_step));
      expect(spire_run_mag_decide(sl, col.line, SPIRE_RUN_MAG_ASK, 5, {}, 0).result);
      expect_eq(shell_step, sl.offered_step); // the retry offers the given-back step
      expect(spire_run_mag_result(sl, false));
      expect(!spire_run_mag_pack_full_notice(told, sl.run_id, true, false, shell_step)); // still full: silent
      expect_eq(std::string("Pack full: your mag did not grow. Keep slots free"),
          std::string(SPIRE_RUN_MAG_PACK_FULL_TEXT));
      expect_eq(std::string("Pack full: no room for your mag. Keep slots free"),
          std::string(SPIRE_RUN_MAG_PACK_FULL_FIRST_TEXT));
      for (const char* t : {SPIRE_RUN_MAG_PACK_FULL_TEXT, SPIRE_RUN_MAG_PACK_FULL_FIRST_TEXT}) {
        expect(std::string(t).size() <= SPIRE_RUN_MAG_TICKER_MAX);
        expect_eq(std::string(t), spire_ticker_line(t));
      }
    });

    f6_case("pack-full decision per client key, run and step (hotfix 1, critic N3)", [&]() {
      SpireRunMagPackFullTold told;
      const std::string a = "3117539661.A", b = "3117539662.B";
      const uint32_t run = 0x64D5C0EF;
      // the lab case: two players of one run, both RESULT 0 for step 1 (newserv.log:183897, :184006): each told once
      auto da = spire_run_mag_pack_full_decide(told, a, run, true, false, 1, 0);
      expect(da.tell && (std::string(da.text) == SPIRE_RUN_MAG_PACK_FULL_TEXT));
      auto db = spire_run_mag_pack_full_decide(told, b, run, true, false, 1, 0);
      expect(db.tell && (std::string(db.text) == SPIRE_RUN_MAG_PACK_FULL_TEXT));
      expect(!spire_run_mag_pack_full_decide(told, a, run, true, false, 1, 0).tell);
      expect(!spire_run_mag_pack_full_decide(told, b, run, true, false, 1, 0).tell);
      expect(spire_run_mag_pack_full_decide(told, a, run, true, false, 2, 1).tell); // a later step
      expect(spire_run_mag_pack_full_decide(told, a, run + 1, true, false, 1, 0).tell); // a new run
      expect(!spire_run_mag_pack_full_decide(told, a, run + 1, true, false, 1, 0).tell);
      // no notice and nothing recorded: RESULT 1, nothing settled, no key, run id 0
      expect(!spire_run_mag_pack_full_decide(told, a, run, true, true, 3, 2).tell);
      expect(!spire_run_mag_pack_full_decide(told, a, run, false, false, 3, 2).tell);
      expect(!spire_run_mag_pack_full_decide(told, "", run, true, false, 3, 2).tell);
      expect(!spire_run_mag_pack_full_decide(told, a, 0, true, false, 3, 2).tell);
      expect(!spire_run_mag_pack_full_decide(told, a, 0, true, false, 3, 2).tell);
      expect_eq(2u, told.size());
      // the first mag of a run (nothing issued before the offer): its own text
      auto first = spire_run_mag_pack_full_decide(told, b, run, true, false, 0, -1);
      expect(first.tell && (std::string(first.text) == SPIRE_RUN_MAG_PACK_FULL_FIRST_TEXT));
      // a third player below the key limit wipes nothing: a and b stay silent for their told steps
      expect(spire_run_mag_pack_full_decide(told, "c", run, true, false, 1, 0).tell);
      expect(!spire_run_mag_pack_full_decide(told, a, run + 1, true, false, 1, 0).tell);
      expect(!spire_run_mag_pack_full_decide(told, b, run, true, false, 1, 0).tell);
      // the key limit: a new key at SPIRE_RUN_MAG_PACK_FULL_KEYS_MAX records wipes every record (told once more)
      SpireRunMagPackFullTold full;
      for (size_t i = 0; i < SPIRE_RUN_MAG_PACK_FULL_KEYS_MAX; i++) {
        expect(spire_run_mag_pack_full_decide(full, std::format("k{}", i), run, true, false, 1, 0).tell);
      }
      expect_eq(SPIRE_RUN_MAG_PACK_FULL_KEYS_MAX, full.size());
      auto old_key = spire_run_mag_pack_full_decide(full, "k0", run, true, false, 1, 0);
      expect(!old_key.tell && !old_key.wiped); // a known key: no wipe
      auto new_key = spire_run_mag_pack_full_decide(full, "new", run, true, false, 1, 0);
      expect(new_key.tell && new_key.wiped);
      expect_eq(1u, full.size());
      expect(spire_run_mag_pack_full_decide(full, "k0", run, true, false, 1, 0).tell); // told once more after the wipe
    });

    f6_case("hex round trip", [&]() {
      Bytes b = {0x02, 0x01, 0x16, 0x00, 0x4C, 0x04, 0xE8, 0x03, 0x00, 0x00, 0x64, 0x00};
      expect_eq(std::string("020116004C04E80300006400"), spire_run_mag_bytes_hex(b));
      expect(b == spire_run_mag_bytes_from_hex("020116004C04E80300006400"));
    });
  }

  auto rewards = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));
  auto mags = spire_mag_parse(phosg::load_file(SPIRE_RUN_MAG_FILE));

  // the mag food reward: the levels by tier, the label starts with the tier word
  for (size_t t = 0; t < 4; t++) {
    auto offer = spire_reward_offer(rewards, SpireRewardKind::MAG_FOOD, 0, nullptr, 3, static_cast<SpireRewardTier>(t),
        [](uint32_t) -> uint32_t { return 0; });
    expect_eq(rewards.mag_food[t], offer.food_levels);
    expect(offer.label.starts_with(spire_reward_tier_label(static_cast<SpireRewardTier>(t))));
  }
  expect_eq(5u, rewards.mag_food[0]);
  expect_eq(30u, rewards.mag_food[3]);

  {
    // the mag holds a learned disk's MIND 50 and rule B raised POW to the weapon's 160: the MIND above the need
    // gives way (to 40) before POW goes below the weapon's need. A force: MIND is the main stat, cut last.
    auto set = [](SpireMagBytes& b, size_t which, uint16_t v) {
      b[4 + which * 2] = v & 0xFF;
      b[5 + which * 2] = v >> 8;
    };
    SpireMagBytes current{0x02};
    set(current, 0, 500); // DEF 5
    set(current, 1, 10000); // POW 100
    set(current, 3, 5000); // MIND 50
    SpireMagBytes raised = current;
    set(raised, 1, 16000);
    auto fit = spire_mag_fit_200(raised, current, spire_reward_need_with_disk(SpireMagNeed{160, 0, 0}, 50), 3);
    expect_eq(16000, spire_mag_stat_word(fit, 1));
    expect_eq(4000, spire_mag_stat_word(fit, 3));
    expect_eq(200u, spire_mag_level_of(fit));
    // the need alone above 200 (no room at all): the asked stats are cut as before, the main stat last
    auto over = spire_mag_fit_200(raised, current, SpireMagNeed{160, 0, 50}, 3);
    expect_eq(200u, spire_mag_level_of(over));
    expect_eq(5000, spire_mag_stat_word(over, 3));
  }

  // review r4: mag food goes on the stat of the lean (design par. 3): MIND for a force, POW for a hunter or a ranger,
  // through the 'Z' ledger step as the bridge calls it with the class's archetype
  for (const char* arch : {"HU", "RA", "FO"}) {
    const auto& line = mags.classes.at(arch);
    SpireRunMagLedger led;
    led.run_id = 0x52;
    led.issued_step = 0;
    led.start_step = 0;
    led.last_mirrored = spire_mag_step_bytes(line, 0, 0, std::nullopt);
    led.feed_levels = 10;
    led.boost_due = true;
    auto h = spire_run_mag_handle(led, mags, line, SPIRE_RUN_MAG_ASK, 1, {*led.last_mirrored}, {}, 0, 0, arch);
    expect(h.decision.result);
    size_t main = (std::string(arch) == "FO") ? 3 : 1;
    for (size_t z = 0; z < 4; z++) {
      expect_eq(spire_mag_stat_word(*led.last_mirrored, z) + ((z == main) ? 1000 : 0),
          spire_mag_stat_word(h.decision.bytes, z));
    }
  }

  // rule B in the ledger: a weapon's need makes the next answer a boost (boost_due), the kind follows Sega's
  // evolution, and mag food adds its levels on the main stat, the sum at most 200
  {
    const auto& hu = mags.classes.at("HU");
    SpireRunMagLedger led;
    led.run_id = 0x51;
    led.issued_step = 1;
    led.start_step = 0;
    led.last_mirrored = spire_mag_step_bytes(hu, 1, 0, std::nullopt);
    led.need = SpireMagNeed{40, 30, 0};
    led.boost_due = true;
    auto d = spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 2, {}, 0, 1,
        [](const SpireMagBytes& b, uint8_t) -> uint8_t { return (spire_mag_stat_word(b, 1) >= 4000) ? 0x2A : b[1]; });
    expect(d.result);
    expect(spire_mag_stat_word(d.bytes, 1) >= 4000);
    expect(spire_mag_stat_word(d.bytes, 2) >= 3000);
    expect(spire_mag_level_of(d.bytes) >= spire_mag_level_of(*led.last_mirrored));
    expect_eq(0x2A, d.bytes[1]);
    // review r3 B2: a RESULT 0 leaves the boost owed (the next ASK answers it again); a RESULT 1 settles it and the
    // food, so the next ASK without a new step changes nothing
    {
      SpireRunMagLedger lost = led;
      lost.feed_levels = 10;
      expect(spire_run_mag_result(lost, false));
      expect(lost.boost_due && (lost.feed_levels == 10));
      expect(spire_run_mag_decide(lost, hu, SPIRE_RUN_MAG_ASK, 2, {}, 0, 1).result);
    }
    led.feed_levels = 10;
    expect(spire_run_mag_result(led, true));
    expect(!led.boost_due && !led.offered_boost && (led.feed_levels == 0));
    expect(*led.last_mirrored == d.bytes);
    expect(led.need == (SpireMagNeed{40, 30, 0})); // the need stays: every later mag keeps it
    expect(!spire_run_mag_decide(led, hu, SPIRE_RUN_MAG_ASK, 3, {}, 0, 1).result);
    // mag food +30 on POW of the mirrored mag: POW never sinks, the sum stays at most 200
    SpireMagBytes big = *led.last_mirrored;
    auto fed = spire_mag_rule_b(hu, 1, 0, big, SpireMagNeed{}, 30, 1);
    expect(spire_mag_stat_word(fed, 1) >= spire_mag_stat_word(big, 1));
    uint32_t sum = 0;
    for (size_t z = 0; z < 4; z++) {
      sum += spire_mag_stat_word(fed, z) / 100;
    }
    expect(sum <= 200);
  }
}

[[maybe_unused]] const bool registered = spire_mech_register_tests("mags", spire_tests_mags);

} // namespace
