# Route B bundle 9, newserv part F3 (SpireEquipItemGC.s): progress

- punt 1: system/client-functions/SpireEquipItemGC.s geschreven (3OP0, zes adressen uit F2, elk met bron)
- punt 2: SPIRE_EQUIP_ITEM_FUNCTION_KEY -> SPIRE_EQUIP_FUNCTION_NAME = "SpireEquipItem", zelfde commit als de .s
- punt 3: StaticTests: nieuwe case "SpireEquipItem assembles for 3OP0 ..." rood zonder .s (exit 134), groen met (exit 0)
- punt 4: precedent-onderzoek: geen exacte (geen functie roept 0x801E4BAC of een 6x-zender vanuit zijn eigen B2-aanroep); twee deel-precedenten (GetExtendedPlayerInfoGC.s, CallProtectedHandler.s)
- pc-build-check (regels 81-91) tegen cab8652: required.txt 'SpireBBATxGuard true' + 'SpireEquipItem true', beide .s gevonden, exit 0
- rapport ROUTEB9-NEWSERV-F3-REPORT.md geschreven
