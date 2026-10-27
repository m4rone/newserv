# BEL-DROPS voortgang (spike/bel-drops, basis 695844f)

- start: pre-flight ok, tak spike/bel-drops op 695844f
- B meseta: spire_drops_item_rules (quest-regels met meseta toegestaan) op de item creator bij quest-load en elke drop; static test groen (boxes 0 -> 1515 van 3000), rood met stub (StaticTests.cc:3566)
- C armour: spire_drops_fit_armor (frame/barrier die de klasse niet draagt -> sterkste gewone draagbare, niet sterker, anders basis); groen, rood met stub (StaticTests.cc:3645 wears)
- D terugvalwapen: eerst de wapengroepen van de rol (spire_drops_role_groups), dan alles; RAmar Lv1 krijgt 0600 Handgun; groen, rood met stub (StaticTests.cc:3701)
- E specials: spire_drops_identify_weapon in spire_drops_filter na filter_item; groen, rood met stub (StaticTests.cc:3733)
- rapport BEL-DROPS-REPORT.md; eindrun static tests exit 0, All static tests passed; A: geen newserv-code, quest box_drop_area 0 (tabel in rapport)
