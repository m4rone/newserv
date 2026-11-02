# Route B bundle 9 deel F2 (statisch onderzoek SpireEquipItem, 3OP0): voortgang

- start: worktree spike/route-b-9-equip op 8c8c1ab gecontroleerd; vorige stand F-REPORT § 1 gelezen
- ijking gelukt: PsoV3.dol (niet main.dol, die is de switcher-loader) uit FST, 4 hookadressen + 2 functiestarts kloppen byte voor byte (rapport § ijking)
- vraag 1: send_equip_item_6x25 = 0x801BA5AC (player, item_id, slot); lokaal uitrusten via handler 0x801BA51C -> 0x801BA3FC (rapport § 2)
- vraag 2: TObjPlayer_for_client_id = 0x801BAC08, array 0x8050FA30; 144 plekken na lwz r13-0x5220 (rapport § 3)
- vraag 3: +0xD04 = 13 uitgeruste slots, tas = +0xD00, lookup 0x801D53D0 + 0x801D5C20, TItem+0xD8 bevestigd (rapport § 4)
- rapport geschreven, open: alleen herintreding in B2-context (breakpoint 0x801BA3FC)
