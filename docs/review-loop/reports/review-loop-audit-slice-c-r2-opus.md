# Koude review audit-slice-c, ronde 2 (opus, invalshoek doel)

## Maatstaf en scope
`git diff 80e8986 HEAD` (tot en met 89b3d50), getoetst tegen de vijf doelpunten S-12, S-3, S-6, Q2-2 en de StaticTests. Elke dragende regel is omgedraaid en daarna is `run-static-tests` en `tests/spire-follow-city.test.sh` gedraaid. Eerdere verslagen heb ik niet gelezen.

## Wat er goed staat (getoetst, niet aangenomen)
- S-12: `spire_follow_should_warp` weigert een partner met LOADING of op floor 0x0F. De mutanten m1b en m2 worden rood in beide suites. `on_ready` wist LOADING vóór `spire_replay_after_floor_load`, dus een partner die in de wachtrij staat volgt alsnog na zijn eigen laadmoment.
- Q2-2: `>` naar `>=` (m5) en `>=` naar `>` bij de zero-tag-grens (m6) worden allebei rood.
- StaticTests voor kamer-clear, draw en meta draaien en slagen.
- `spire-follow-check ... loading` (m9) wordt rood in de shelltest.

## Mutanten (letterlijk)
Vóór de fix:
```
m1b: static=134 shell=1   (!partner_loading weg)
m2:  static=134 shell=1   (floor 0x0F-check weg)
m3:  static=0   shell=0   (Client.cc: challenge = false)        GROEN
m4:  static=0   shell=0   (registerbestand zonder karakter-id)  GROEN
m5:  static=134 shell=0
m6:  static=134 shell=0
m7:  static=0   shell=0   (lege bestandsnaam toch opslaan)      GROEN
m8:  static=0   shell=0   (pending.insert weg)                  GROEN
m9:  static=0   shell=1
```
Na de fix:
```
m3:  static=134  (DataIndex: challenge-tak uit)
m4:  static=134  (karakter-id vervangen door "x")
m7:  static=134  (geen disp toch een bestandsnaam)
m8:  static=134  (pending-beslissing altijd waar)
m10: static=134  (zonder karakter wordt een Challenge niet meer geblokkeerd)
m11: static=134  (de check op een lege beschermde lijst weg)
```

## Bevindingen
- **B1 (S-3, major):** de Challenge-blokkade zat alleen in `Client::is_spire_blocked`; mutant m3 bleef groen. Fix: de beslissing staat nu in `DataIndex::is_spire_blocked_quest(serial, name*, quest, challenge)`, getest voor een beschermd en een onbeschermd karakter, zonder karakter (fail closed) en met een lege lijst. Wat overblijft: één regel in Client.cc die `challenge_template_index >= 0` afleidt. Die is zonder Quest-object niet te testen.
- **B2 (S-6, major):** dat het registerbestand per karakter is, werd alleen getoetst op de formatter. De aanroeper die het karakter kiest (m4) en het pad zonder karakter (m7) bleven groen. Fix: `spire_bridge_registers_filename_for(serial, disp*)`. De test toont dat twee karakters op dezelfde kaart twee bestanden krijgen en dat geen karakter een lege naam geeft.
- **B3 (S-12, minor):** de beslissing "onthouden en later volgen" voor een partner die nog laadt was niet getest (m8). Fix: `spire_follow_partner_pending`, met static tests voor laden, floor 0x0F, een normale warp en de stad.

## Verificatie na de fixes (letterlijk)
Fix-commit 42800e2.
```
-- Build files have been written to: .../audit-slice-c/build
[100%] Built target newserv
I ... -- All static tests passed      (exit 0)
ok: partner floor 1, leader floor 2 -> skip
ok: partner floor 15, leader floor 2 -> skip
ok: spire-lives-check                 (exit 0)
```

## Wat ik niet heb aangeraakt
- `spire_follow_pending` gebruikt het adres van de client als sleutel en ruimt een client die de game verlaat niet op. Hergebruik van dat adres kost hooguit één extra volgpoging, en die doorloopt dezelfde checks. Dit is pre-existing in deze slice en niet gefixt.
- De draw-test laat zijn tijdelijke JSON in /tmp staan als een expect afbreekt. Dit is cosmetisch.
- Floor 0x0F is in Ep2 een echte floor (Gol Dragon), wat de header ook zegt. Dat ligt buiten de huidige kamers.
- Er is niets uitgerold naar de pc of het lab.

## Stopadvies
Alle doelpunten zijn nu gedekt door een mutant die rood wordt. Wat nog open staat is minor en valt buiten het doel. Advies: stoppen na deze ronde.
