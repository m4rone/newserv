# veilig-1 vangnet r2 (newserv) - voortgang

- 2026-11-11 start op spike/veilig 1a00938. Punt 1 (tweede grens 300 s, vijandtekens) en punt 2 (baas nooit in de keten).
- punt 1: rood (StaticTests.cc:2619 counted at -1 s, fix uitgeschakeld), groen (counted at 300 s, notice 45 s, alle statische tests exit 0). Commit volgt.
- punt 2: rood (StaticTests.cc:2736), groen na de is_boss-check; alle statische tests exit 0. Commit volgt.
