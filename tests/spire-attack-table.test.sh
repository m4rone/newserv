#!/bin/sh
# The Spire attack table (system/spire-attack.json) against Sega's own tables. Every rare weapon the table names must
# be a rare weapon of Sega's item table (item-parameter-table-gc-v3.json), carry Sega's name (names-v3.json), sit in the
# line Sega's weapon kind gives it, be wearable by the class it is listed for (Sega's usability flags), drop in
# Episode 1 on a difficulty of its rung and tier (rare-table-v3.json), and be reachable: in at least one room of that
# rung the run copy of the reward grid (system/spire-rewards.json: run level, weapon level, mag ceiling, ATA model) can
# carry it with rule B's mag. The lists are held against a fresh derivation from those tables, so the table cannot
# drift from them; Legendary is never weaker than Epic in any room; the attribute per area is Sega's most likely one;
# the specials are Sega's 40, each once, with Sega's stars.
#
#   sh tests/spire-attack-table.test.sh [newserv]      the test (the executable is not used; python3 is)
#   sh tests/spire-attack-table.test.sh --write        derive Items and Rares again and rewrite the table in place
#                                                      (after a change to the reward grid or to Sega's tables)
# Run from the repository root, as CMake does.

set -e
if ! command -v python3 >/dev/null 2>&1; then
  echo "FAIL: spire-attack-table needs python3"
  exit 1
fi

exec python3 - "$@" <<'PYEOF'
import json
import re
import sys

TABLE = 'system/spire-attack.json'
RUNGS = ['Normal', 'Hard', 'VeryHard', 'Ultimate']  # rung 0-3: Sega's difficulty names in rare-table-v3.json
TIERS = ['Epic', 'Legendary']
ALL_TIERS = ['Normal', 'Rare', 'Epic', 'Legendary']
# The levels a rung's run copy starts above Normal's: Sega's own online level gates, Lv20, Lv40 and Lv80 (pso-roguelike
# sim/ladder.json char_level 19, 39, 79). Only this test reads them: the server holds a weapon against the copy's real
# stats at the draw.
RUNG_START = [0, 19, 39, 79]
RARE_STARS = 9  # Sega's rare weapons have 9 stars or more (ItemParameterTable::is_item_rare)
# Sega's usability flags (SpireDrops.cc spire_weapon_usability_bits): 01 hunter, 02 ranger, 04 force, 08 human,
# 10 android, 20 newman, 40 male, 80 female
CLASS_FLAGS = {'HUmar': 0x49, 'HUnewearl': 0xA1, 'HUcast': 0x51, 'HUcaseal': 0x91, 'RAmar': 0x4A, 'RAmarl': 0x8A,
               'RAcast': 0x52, 'RAcaseal': 0x92, 'FOmar': 0x4C, 'FOmarl': 0x8C, 'FOnewm': 0x64, 'FOnewearl': 0xA4}
ANDROIDS = ['HUcast', 'HUcaseal', 'RAcast', 'RAcaseal']
ATTRIBUTE_IDS = {'Native': 1, 'A.Beast': 2, 'Machine': 3, 'Dark': 4, 'Hit': 5}  # data1[6], [8], [10]
SPECIAL_TYPE_EXP = 3    # Sega's special Type of Master's, Lord's, King's
SPECIAL_TYPES_MANA = (2, 5)  # Heart, Mind, Soul, Geist (they steal TP) and Spirit (it costs TP)


def load_sega(path):
    """newserv's tables carry // comments, hex literals and trailing commas"""
    s = open(path).read()
    s = re.sub(r'//[^\n]*', '', s)
    s = re.sub(r'(-?)0x([0-9A-Fa-f]+)', lambda m: str(-int(m.group(2), 16) if m.group(1) else int(m.group(2), 16)), s)
    s = re.sub(r',(\s*[}\]])', r'\1', s)
    return json.loads(s)


PMT = load_sega('system/tables/item-parameter-table-gc-v3.json')
RARE = load_sega('system/tables/rare-table-v3.json')
NAMES = load_sega('system/tables/names-v3.json')
LEVELS = load_sega('system/tables/level-table-v3.json')
COMMON = load_sega('system/tables/common-table-v3-v4.json')
GRID = json.load(open('system/spire-rewards.json'))
ITEMS = PMT['Items']
WEAPON_LEVELS = {w['W']: w for w in GRID['WeaponLevels']}
ROOM_W = {int(k): v for k, v in GRID['RoomW'].items()}
STEPS = GRID['RungWeaponSteps']
ATA_MODEL = GRID['AtaModel']


def stars(code):
    return PMT['StarValues'][ITEMS[code]['ID'] - PMT['StarValueBaseIndex']]


def special_stars(special):
    return PMT['StarValues'][PMT['SpecialStarsBaseIndex'] - PMT['StarValueBaseIndex'] + special]


def special_names():
    """Sega's special names as newserv prints them (ItemNameIndex.cc name_for_weapon_special), index = special id"""
    src = open('src/ItemNameIndex.cc').read()
    block = src[src.index('name_for_weapon_special = {'):]
    block = block[:block.index('};')]
    return [None] + [n.replace("\\'", "'") for n in re.findall(r'^\s*"((?:[^"\\]|\\.)*)"', block, re.M)]


def ep1_drop_difficulties():
    """rare weapon code -> the difficulties whose Episode 1 rare table drops it (any section id, enemy or box)"""
    out = {}
    for difficulty in RUNGS:
        for table in RARE['Normal']['Episode1'][difficulty].values():
            for specs in table.values():
                for _prob, code in specs:
                    out.setdefault('%06X' % code, set()).add(difficulty)
    return out


DROPS = ep1_drop_difficulties()


def class_stats(class_index, level):
    """ATP, ATA and MST of a class at a level (1 = the first), from Sega's level table"""
    base = LEVELS['BaseStats'][class_index]
    ret = {k: base[k] for k in ('ATP', 'ATA', 'MST')}
    for delta in LEVELS['LevelDeltas'][class_index][1:level]:
        for k in ret:
            ret[k] += delta[k]
    return ret


def ceil_div(a, b):
    return 0 if a <= 0 else -(-a // b)


def dex_needed(ata, weapon_ata, required):
    """SpireRewards.cc spire_ata_dex_needed"""
    if not required:
        return 0
    table = ceil_div(required - ata, 5)
    screen_weapon = ceil_div(10 * required - (600 + ata + 10 * weapon_ata), 5)
    screen_bare = ceil_div(10 * required - (600 + ata), 5)
    return {'table': table, 'screen_weapon': screen_weapon, 'screen_bare': screen_bare,
            'safe': max(table, screen_weapon)}[ATA_MODEL]


def mag_level_needed(code, st):
    """The level of rule B's mag for this weapon (SpireRewards.cc spire_reward_mag_need; the grid's GridMag level:
    DEF 5 plus the POW, DEX and MIND the weapon asks)"""
    it = ITEMS[code]
    return (5 + ceil_div(it['ATPRequired'] - st['ATP'], 2) + ceil_div(it['MSTRequired'] - st['MST'], 2) +
            dex_needed(st['ATA'], it['ATA'], it['ATARequired']))


def strength(code, grind=0):
    """SpireDrops.cc spire_drops_weapon_strength: twice the mean ATP plus 4 per grind"""
    it = ITEMS[code]
    return it['ATPMin'] + it['ATPMax'] + 4 * min(grind, it['MaxGrind'])


def room_state(cls, line, rung, k):
    """The run copy when the reward of room k is drawn on a rung: its stats, the mag ceiling, its level and W. The level
    is the grid's run level of the room's weapon level on Normal (the first room of that level, the lowest) plus the
    rung's start; the ceiling is the one MagCapKey names at the rung's weapon level."""
    c = GRID['Classes'][cls]
    w_normal = ROOM_W[k]
    level = c['Lines'][line]['W']['W%d' % w_normal]['RunLevel'] + RUNG_START[rung]
    w = min(6, w_normal + STEPS[rung])
    return class_stats(c['Index'], level), WEAPON_LEVELS[w][MAG_CAP_KEY], level, w


def fits(code, cls, line, rung, k):
    st, cap, _level, _w = room_state(cls, line, rung, k)
    return mag_level_needed(code, st) <= cap


def pick(codes, cls, line, rung, k):
    """What the server gives: the first of the list (the strongest) the copy can carry, None for the grid's cell"""
    for code in codes:
        if fits(code, cls, line, rung, k):
            return code
    return None


def candidates(cls, line, difficulties, kind_lines):
    out = []
    for code, dropped in DROPS.items():
        it = ITEMS.get(code)
        if (it is None) or (not code.startswith('00')) or ('WeaponKind' not in it) or (stars(code) < RARE_STARS):
            continue
        if kind_lines.get(str(it['WeaponKind'])) != line:
            continue
        if (it['UsabilityFlags'] & CLASS_FLAGS[cls]) != CLASS_FLAGS[cls]:
            continue
        if dropped & set(difficulties):
            out.append(code)
    return out


def ordered(codes):
    return sorted(codes, key=lambda c: (-strength(c), -stars(c), c))


def derive(table):
    """Items and Rares as the rules of the table give them from Sega's tables and the reward grid"""
    rares = {}
    for ri, rung in enumerate(RUNGS):
        rares[rung] = {}
        for cls, c in GRID['Classes'].items():
            rares[rung][cls] = {}
            for line in c['Lines']:
                def reachable(code):
                    return any(fits(code, cls, line, ri, k) for k in ROOM_W)
                columns = table['Columns'][rung]
                epic = [c2 for c2 in candidates(cls, line, columns['Epic'], table['KindLines']) if reachable(c2)]
                legendary = [c2 for c2 in candidates(cls, line, columns['Legendary'], table['KindLines'])
                             if reachable(c2)]
                # Legendary holds every Epic weapon too, so its strongest is never below Epic's
                rares[rung][cls][line] = {'Epic': ordered(epic), 'Legendary': ordered(set(legendary) | set(epic))}
    used = sorted({code for r in rares.values() for c in r.values() for l in c.values() for t in l.values()
                   for code in t})
    items = {code: {'Name': NAMES[code], 'Line': table['KindLines'][str(ITEMS[code]['WeaponKind'])]} for code in used}
    return items, rares


def dumps(value):
    return json.dumps(value, ensure_ascii=False, separators=(', ', ': '))


def render(table):
    """The table's own layout: one top-level key per line, the long ones one row per line"""
    one_per_line = ('Columns', 'KindLines', 'Items', 'RareSpecials', 'ExcludedSpecials')
    lines = ['{']
    keys = list(table)
    for i, key in enumerate(keys):
        comma = ',' if i + 1 < len(keys) else ''
        value = table[key]
        if key == 'Rares':
            lines.append('  "Rares": {')
            for ri, (rung, classes) in enumerate(value.items()):
                lines.append('    %s: {' % dumps(rung))
                for ci, (cls, cls_lines) in enumerate(classes.items()):
                    lines.append('      %s: {' % dumps(cls))
                    rows = list(cls_lines.items())
                    for li, (line, tiers) in enumerate(rows):
                        lines.append('        %s: %s%s' % (dumps(line), dumps(tiers), ',' if li + 1 < len(rows) else ''))
                    lines.append('      }%s' % (',' if ci + 1 < len(classes) else ''))
                lines.append('    }%s' % (',' if ri + 1 < len(value) else ''))
            lines.append('  }%s' % comma)
        elif key in one_per_line:
            lines.append('  %s: {' % dumps(key))
            rows = list(value.items())
            for ri, (k, v) in enumerate(rows):
                lines.append('    %s: %s%s' % (dumps(k), dumps(v), ',' if ri + 1 < len(rows) else ''))
            lines.append('  }%s' % comma)
        else:
            lines.append('  %s: %s%s' % (dumps(key), dumps(value), comma))
    lines.append('}')
    return '\n'.join(lines) + '\n'


table = json.load(open(TABLE))
MAG_CAP_KEY = table['MagCapKey']

if '--write' in sys.argv[1:]:
    table['Items'], table['Rares'] = derive(table)
    open(TABLE, 'w').write(render(table))
    print('wrote %s: %d rare weapons, %d list entries' % (
        TABLE, len(table['Items']),
        sum(len(t) for r in table['Rares'].values() for c in r.values() for l in c.values() for t in l.values())))
    sys.exit(0)

errors = []


def check(ok, text):
    if not ok:
        errors.append(text)


# --- the switch and the keys
KEYS = ['_what', 'Enabled', 'AttributeIds', 'AreaOfRoom', 'AreaAttribute', 'AttrPct', 'AttrPerRung',
        'AttrPerStepAboveW6', 'AttrCap', 'SecondAttr', 'HitPerRung', 'HitCap', 'MagCapKey', 'Columns', 'KindLines',
        'RareSpecials', 'ExcludedSpecials', 'Items', 'Rares']
check(list(table) == KEYS, 'keys are %s, expected %s' % (list(table), KEYS))
check(list(table)[1] == 'Enabled' and isinstance(table['Enabled'], bool), 'Enabled must be the first key after _what, a bool')
check(MAG_CAP_KEY in ('MagCap', 'MagCapLegendary'), 'MagCapKey %s is no key of the grid\'s WeaponLevels' % MAG_CAP_KEY)
check(open(TABLE).read() == render(table), 'the file is not in its own layout: run this script with --write')

# --- lines: Sega's weapon kind gives the line of every weapon the reward grid knows
kind_lines = table['KindLines']
for code, line in GRID['LineOf'].items():
    check(kind_lines.get(str(ITEMS[code]['WeaponKind'])) == line,
          'KindLines gives %s for %s, the reward grid says %s' % (kind_lines.get(str(ITEMS[code]['WeaponKind'])), code, line))
check(set(kind_lines.values()) <= set(GRID['LineGroups'].values()), 'KindLines names a line the reward grid has not')

# --- this script's stats of a class at a level are the reward grid's (RunStats of every cell)
for cls, c in GRID['Classes'].items():
    check(cls in CLASS_FLAGS, 'class %s of the reward grid has no usability flags here' % cls)
    for line, l in c['Lines'].items():
        for w, wl in l['W'].items():
            check(class_stats(c['Index'], wl['RunLevel']) == wl['RunStats'], '%s %s %s: Lv%d is %s by Sega\'s level table, '
                  'the reward grid says %s' % (cls, line, w, wl['RunLevel'], class_stats(c['Index'], wl['RunLevel']), wl['RunStats']))

# --- every rare weapon of the table against Sega's item table
for code, info in table['Items'].items():
    it = ITEMS.get(code)
    if (it is None) or (not code.startswith('00')) or ('WeaponKind' not in it):
        errors.append('%s is no weapon of Sega\'s item table' % code)
        continue
    check(stars(code) >= RARE_STARS, '%s %s has %d stars: no rare weapon' % (code, info['Name'], stars(code)))
    check(NAMES.get(code) == info['Name'], '%s is named %s, Sega names it %s' % (code, info['Name'], NAMES.get(code)))
    check(kind_lines.get(str(it['WeaponKind'])) == info['Line'], '%s %s is in line %s, its weapon kind %d gives %s' % (
        code, info['Name'], info['Line'], it['WeaponKind'], kind_lines.get(str(it['WeaponKind']))))
    check(code in DROPS, '%s %s does not drop in Episode 1' % (code, info['Name']))

# --- the lists per rung, class, line and tier
rares = table['Rares']
check(list(rares) == RUNGS, 'Rares has the rungs %s, expected %s' % (list(rares), RUNGS))
check(list(table['Columns']) == RUNGS, 'Columns has the rungs %s, expected %s' % (list(table['Columns']), RUNGS))
rows = 0
used = set()
for ri, rung in enumerate(RUNGS):
    check(list(rares.get(rung, {})) == list(GRID['Classes']), '%s: the classes are not the reward grid\'s' % rung)
    for cls, c in GRID['Classes'].items():
        cls_lines = rares.get(rung, {}).get(cls, {})
        check(list(cls_lines) == list(c['Lines']), '%s %s: the lines are not the reward grid\'s' % (rung, cls))
        for line in c['Lines']:
            tiers = cls_lines.get(line, {})
            check(list(tiers) == TIERS, '%s %s %s: tiers %s' % (rung, cls, line, list(tiers)))
            epic, legendary = tiers.get('Epic', []), tiers.get('Legendary', [])
            for tier, codes in (('Epic', epic), ('Legendary', legendary)):
                where = '%s %s %s %s' % (rung, cls, line, tier)
                columns = set(table['Columns'][rung][tier]) | set(table['Columns'][rung]['Epic'])
                check(len(set(codes)) == len(codes), '%s: a weapon is listed twice' % where)
                check(codes == ordered(codes), '%s: not strongest first' % where)
                for code in codes:
                    rows += 1
                    used.add(code)
                    if code not in table['Items'] or code not in ITEMS:
                        errors.append('%s: %s is not in Items' % (where, code))
                        continue
                    it = ITEMS[code]
                    check(table['Items'][code]['Line'] == line, '%s: %s is of line %s' % (
                        where, code, table['Items'][code]['Line']))
                    check((it['UsabilityFlags'] & CLASS_FLAGS[cls]) == CLASS_FLAGS[cls],
                          '%s: %s %s cannot be equipped by this class (flags %02X)' % (
                              where, code, NAMES[code], it['UsabilityFlags']))
                    check(DROPS.get(code, set()) & columns, '%s: %s %s drops on %s, not on %s' % (
                        where, code, NAMES[code], sorted(DROPS.get(code, [])), sorted(columns)))
                    check(any(fits(code, cls, line, ri, k) for k in ROOM_W),
                          '%s: %s %s cannot be carried in any room of this rung' % (where, code, NAMES[code]))
            check(set(epic) <= set(legendary), '%s %s %s: an Epic weapon is missing from Legendary' % (rung, cls, line))
            # Legendary never weaker than Epic, in every room
            for k in ROOM_W:
                e, l = pick(epic, cls, line, ri, k), pick(legendary, cls, line, ri, k)
                check((e is None) or ((l is not None) and (strength(l) >= strength(e))),
                      '%s %s %s room %d: Legendary %s is weaker than Epic %s' % (rung, cls, line, k, l, e))
check(used == set(table['Items']), 'Items and the lists differ: %s' % sorted(used ^ set(table['Items'])))
derived_items, derived_rares = derive(table)
check(derived_rares == rares, 'Rares is not what Sega\'s tables and the reward grid give: run this script with --write')
check(derived_items == table['Items'], 'Items is not what Sega\'s tables give: run this script with --write')

# --- the cells of the reward grid: class x line x weapon level, hunters and rangers (280 per tier)
first_room = {w: min(k for k, v in ROOM_W.items() if v == w) for w in set(ROOM_W.values())}
cells = {}
for ri, rung in enumerate(RUNGS):
    count = {t: {'cells': 0, 'rare': 0, 'grid': 0} for t in TIERS}
    for cls, c in GRID['Classes'].items():
        if c['Archetype'] == 'FO':
            continue
        for line, l in c['Lines'].items():
            for w_normal in sorted(first_room):
                k = first_room[w_normal]
                grid_w = l['W']['W%d' % min(6, w_normal + STEPS[ri])]
                for tier in TIERS:
                    n = count[tier]
                    n['cells'] += 1
                    got = pick(rares[rung][cls][line][tier], cls, line, ri, k)
                    n['rare'] += 1 if got else 0
                    cell = grid_w['Tiers'][tier]
                    grid_rare = (not cell.get('Fallback')) and (stars(cell['Code']) >= RARE_STARS)
                    n['grid'] += 1 if grid_rare else 0
                    # the table never gives less than the grid's own cell: the server can take the table's weapon first
                    check((not grid_rare) or (got is not None), '%s %s %s W%d %s: the grid has %s, the table nothing' % (
                        rung, cls, line, w_normal, tier, cell['Name']))
                    check((not grid_rare) or (got is None) or (strength(got) >= strength(cell['Code'], cell.get('Grind') or 0)),
                          '%s %s %s W%d %s: the grid\'s %s is stronger than the table\'s %s' % (
                              rung, cls, line, w_normal, tier, cell['Name'], got))
    cells[rung] = count

# --- the attribute of the next area
areas = table['AreaOfRoom']
check(list(areas) == [str(k) for k in range(1, 16)], 'AreaOfRoom must hold rooms 1-15 in order')
columns = [areas[str(k)] for k in range(1, 16)] if len(areas) == 15 else []
check(all(isinstance(a, int) and (0 <= a <= 9) for a in columns), 'AreaOfRoom: an area is not one of Sega\'s columns 0-9')
check(columns == sorted(columns) and columns[:1] == [0] and columns[-1:] == [9], 'AreaOfRoom must run from Forest 1 (0) to Ruins 3 (9)')
check(table['AttributeIds'] == ATTRIBUTE_IDS, 'AttributeIds are not the client\'s (data1[6]: 1 Native ... 5 Hit)')
area_attr = table['AreaAttribute']
check(len(area_attr) == 10 and all(a in ('Native', 'A.Beast', 'Machine', 'Dark') for a in area_attr),
      'AreaAttribute must name an attribute for each of Sega\'s 10 area columns')
sega_tables = 0
for name, t in COMMON.items():
    if name.startswith('Ep1:') and isinstance(t, dict) and ('BonusTypeProbTable' in t):
        sega_tables += 1
        probs = t['BonusTypeProbTable']  # rows: none, Native, A.Beast, Machine, Dark, Hit; columns: the 10 areas
        for col in range(10):  # a tie counts (Very Hard's Ruins 3 has all four at 12)
            best = max(probs[row][col] for row in range(1, 5))
            check(len(area_attr) == 10 and probs[ATTRIBUTE_IDS[area_attr[col]]][col] == best,
                  '%s: %s is not Sega\'s most likely attribute in area column %d (%s)' % (
                      name, area_attr[col] if len(area_attr) == 10 else '?', col, [probs[row][col] for row in range(1, 5)]))
check(sega_tables > 0, 'no Episode 1 table of Sega with a BonusTypeProbTable')
check(list(table['AttrPct']) == ALL_TIERS, 'AttrPct must hold the four tiers in order')
pcts = set()
for rung in range(4):
    for k in ROOM_W:
        above = max(0, ROOM_W[k] + STEPS[rung] - 6)
        for tier in ALL_TIERS:
            pct = min(table['AttrCap'], table['AttrPct'][tier] + table['AttrPerRung'] * rung +
                      table['AttrPerStepAboveW6'] * above)
            pcts.add(pct)
            second = pct * table['SecondAttr']['Percent'] // 100
            check((pct % 5 == 0) and (5 <= pct <= 100), 'attribute %d%% (rung %d room %d %s) is no value Sega has' % (
                pct, rung, k, tier))
            check((second % 5 == 0) and (second * 100 == pct * table['SecondAttr']['Percent']),
                  'second attribute of %d%% is no multiple of 5' % pct)
check(table['SecondAttr']['Tier'] in ALL_TIERS and table['SecondAttr']['FromRung'] in range(4), 'SecondAttr: tier or rung unknown')
check((table['HitCap'] <= 50) and (table['HitPerRung'] % 5 == 0), 'Hit: above the house limit of 50 or no multiple of 5')

# --- the specials
names = special_names()
check(len(names) == 41, 'ItemNameIndex.cc has %d special names, expected 40' % (len(names) - 1))
seen = {}
type_style = {}
for style, by_stars in table['RareSpecials'].items():
    for star, pairs in by_stars.items():
        check(pairs == sorted(pairs), 'RareSpecials %s %s: not in id order' % (style, star))
        for special, name in pairs:
            check(special not in seen, 'special %d is listed twice' % special)
            seen[special] = style
            check(1 <= special <= 40 and names[special] == name, 'special %d is named %s, Sega names it %s' % (
                special, name, names[special] if 1 <= special <= 40 else '?'))
            check(special_stars(special) == int(star), 'special %d %s has %d stars at Sega, the table says %s' % (
                special, name, special_stars(special), star))
            sega_type = PMT['Specials'][special]['Type']
            check(type_style.setdefault(sega_type, style) == style, 'Sega\'s special family %d is in two styles' % sega_type)
excluded = table['ExcludedSpecials']
check(list(excluded) == ['All', 'Android'], 'ExcludedSpecials must hold All and Android')
for who, pairs in excluded.items():
    for special, name in pairs:
        check(1 <= special <= 40 and names[special] == name, 'ExcludedSpecials %s: %d is not named %s' % (who, special, name))
exp = sorted(s for s in range(1, 41) if PMT['Specials'][s]['Type'] == SPECIAL_TYPE_EXP)
mana = sorted(s for s in range(1, 41) if PMT['Specials'][s]['Type'] in SPECIAL_TYPES_MANA)
check([p[0] for p in excluded.get('All', [])] == exp, 'ExcludedSpecials All must be the experience specials %s' % exp)
check([p[0] for p in excluded.get('Android', [])] == mana, 'ExcludedSpecials Android must be the mana specials %s' % mana)
check(not (set(seen) & set(exp)), 'an experience special is in RareSpecials')
check(sorted(set(seen) | set(exp)) == list(range(1, 41)), 'RareSpecials and the experience specials are not Sega\'s 40: %s' % sorted(
    set(range(1, 41)) ^ (set(seen) | set(exp))))
for cls in ANDROIDS:  # the classes ExcludedSpecials Android is for have no MST at all
    index = GRID['Classes'][cls]['Index']
    check(LEVELS['BaseStats'][index]['MST'] == 0 and class_stats(index, 200)['MST'] == 0, '%s has MST: no android?' % cls)

if errors:
    for e in errors[:40]:
        print('FAIL: ' + e)
    if len(errors) > 40:
        print('FAIL: ... and %d more' % (len(errors) - 40))
    sys.exit(1)

print('ok: %d rare weapons of Sega in %d list entries, each wearable by its class, dropping in Episode 1 on its rung, '
      'reachable in a room' % (len(table['Items']), rows))
for rung in RUNGS:
    n = cells[rung]
    print('ok: %-8s cells with a real rare weapon: Epic %d of %d (reward grid alone %d), Legendary %d of %d (%d)' % (
        rung, n['Epic']['rare'], n['Epic']['cells'], n['Epic']['grid'], n['Legendary']['rare'], n['Legendary']['cells'],
        n['Legendary']['grid']))
print('ok: Legendary never weaker than Epic in any room; attributes %s%% on %d of Sega\'s tables; %d specials in %d '
      'styles, %d left out for all, %d for androids' % (
          '/'.join(str(p) for p in sorted(pcts)), sega_tables, len(seen), len(table['RareSpecials']), len(exp), len(mana)))
PYEOF
