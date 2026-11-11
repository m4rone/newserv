# Voortgang kwaliteit-3 partnerweg (newserv)

- 1. Nameting op 7895fd9: klopt (de achterblijver stuurt K/F/X binnen 15 frames zonder wachten; 'F' wist vóór de timertik)
- 2. Fix gebouwd en gecommit (bac2b35): elk einde terwijl de runklok minder spelers telt dan bij de start = "speler weg"
- 3. Rood gemeten (StaticTests.cc:5631 met de oude regel), groen op bac2b35 (alle statische tests, exit 0)
- 4. Build newserv-bac2b35, SpireDrops gelijk aan ee0fa66; rapport kwaliteit-3-partnerweg-newserv.md: klaar
