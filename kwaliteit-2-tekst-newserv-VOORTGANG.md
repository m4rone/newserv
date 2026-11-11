# Kwaliteit 2 (tekst en ux), newserv-kant: voortgang

Tak spike/kwaliteit-2, start 4f63e70. Eén regel per afgeronde inventarisregel.

- 2026-11-11 08:58 pre-flight: tak spike/kwaliteit-2, HEAD 4f63e70, werkboom schoon, build gestart in build/ (Release, Unix Makefiles, als kwaliteit-1).
- 2026-11-11 09:01 eerste build klaar (make exit 0, 0 compilerwarnings), `build/newserv help` meldt newserv-4f63e70 = HEAD; basis static tests exit 0 ("-- All static tests passed", 10 W-regels van negatieve gevallen, gelijk aan kwaliteit-1).
- 51 hermeten: geen serverregel noemt kamerbonus, Purse of Clean Room; alle drie zijn pl_add_meseta in de quest zonder code naar de server. Niets gebouwd, questkant = blocker.
- 197 + 202 gebouwd (d7e5e17): hermeting gaf voor k18/k21/k27 "The way is open. Walk to the warp: Room 19/22/28 normal." (geen "pipe", wel de pijpwoorden); nu "The way is open. Take the teleporter: Room 19." Rood StaticTests.cc:6942, groen exit 0.
- 258 gebouwd (a9a50d0): gemeten dat de quest een wachtende gift alleen in room_start geeft (rewards.py:1692/1712) en run_end alleen ex_retry/rr_retry roept (1699-1700): na de laatste clear komt de gift nooit. Nu "Hoarder: inventory full. This was the last room: the gift is not given." als de laatste 'V' van de schrijver de eindbaaskamer van de episode is. Rood StaticTests.cc:6961, groen exit 0.
- 145 gebouwd (b73a95d): beide mag-herinneringen (NO_MAG bij een baaskamerstart en de eerste mag) gaan via spire_send_ticker_later met de pack-line-afstand; de baasdeurregel houdt die volgorde 4 s vast (spire_ticker_hold). Rood StaticTests.cc:6997, groen exit 0. grep: geen directe spire_send_ticker van een mag-herinnering meer.
- 276 gebouwd (2bbccaf): de eerste-mag-herinnering wacht 500 ms op de 'C' 14 en vervalt als newserv de mag uitrust. Rood StaticTests.cc:7024, groen exit 0.
- 275 gebouwd (be19ed9): kitregel eerst, de uitrust-hint wordt vastgehouden tot de kit-ticker eindigt en neemt dan zijn beurt 4 s later; kitregel, late kitregel en hint delen de volgorde per client met de mag-herinneringen. Rood StaticTests.cc:7052, groen exit 0.
- 2026-11-11 09:22 eindbuild op 4ed85f2 (laatste codecommit): make exit 0, 0 warnings, `build/newserv help` meldt newserv-4ed85f2, static tests exit 0 ("-- All static tests passed", 0 E-regels, 10 W-regels als de basis). Rapport geschreven: kwaliteit-2-tekst-newserv.md. Telling: gebouwd 6, al opgelost 0, geblokkeerd 1 (51).
