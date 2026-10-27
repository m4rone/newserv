# Route B bundle 9, newserv part F (server equip): progress

- 0 pre-flight: branch spike/route-b-9-equip at 5e374c8; contract § 1/§ 3 and research read.
- 1 client function: NOT written. No source in this tree gives the 3OP0 address of the Item Pack equip routine (the one
  that sends 6x25) or a 3OP0 way from the local player to its live TItem list; see the report.
- 2 r15 + switch: SpireEquipItemPatch (default true), r15 in every 'M' answer before DONE; key SPIRE_EQUIP_ITEM_FUNCTION_KEY
  (not *_FUNCTION_NAME while the .s is missing, so pc-build.sh's required.txt stays SpireBBATxGuard only). r15 = 0 today.
- 3 'C' 13 / 'C' 14: spire_equip_prompt_handle (find, log, send SpireEquipItem when r15 = 1; else one log line).
- 4 texts: spire_kit_use_text and spire_run_mag_evolve_text(..., server_equips); 'C' comment block in SpireFlagBridge.hh.
- 5 static tests: section "Spire bundle 9", 4 cases; failed against stubs, green with the code (build exit 0,
  "-- All static tests passed").
