# Audit 1, run 3 in newserv: voortgang

Tak spike/audit-1-run3, vanaf f612d6d. Meting: audit-1-a1-grote-bestanden.md.

- 23:02 SpireMeta.cc/.hh, DataIndex.cc, ExpressionEnvironment.hh, Client.cc, ReceiveCommands.cc en de tests in
  StaticTests.cc geschreven. Build groen, `run-static-tests` exit 0, laatste regel "All static tests passed".
- Volgende: rode meting van de nieuwe tests zonder de wijziging, dan de patch voor SpireFlagBridge.cc.
- 23:10-23:17 Rode meting in zes runs (bronnen van f612d6d met stubs voor de nieuwe functies): alle tien de nieuwe
  blokken zijn rood, en ook de aangepaste Olga Flow-tekst. Teruggezet met git checkout van de eigen bestanden.
- 23:19 De patch voor SpireFlagBridge.cc/.hh, met testhunks: build groen, static tests exit 0. Zonder de nieuwe body
  is de gift-test rood (StaticTests.cc:7220). De patch staat in output/audit-1-a1-spireflagbridge.patch en is
  teruggezet met git checkout. `git apply --check` slaagt op veilig-1-ship; daar is ook f612d6d + 04b88fe nodig.
- Klaar: AUDIT1-RUN3-REPORT.md.
- 23:29 Ronde 2. De winkel over drie runs staat in fee5a8b: groen (exit 0), rood met de beslissing van twee runs.
  Daarna in de patch: de array van drie runs in SpireFlagBridge.cc en `spire_checkpoint_status_text` met een test.
  Met de patch groen (exit 0), rood met grens 31. De patch heeft nu 207 regels. Apply-check groen op run3 en op
  veilig-1-ship (los en als stapel). SpireFlagBridge.cc is teruggezet.
