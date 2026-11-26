"""Compare runtime inventories; additions are allowed, existing routes preserved.

Optional --constraints JSON schema (also compare(before, after, constraints=dict)):
{"version": 1,
 "approved_moves": [{"surface": "editor-800", "id": "42",
                     "rect": [10, 20, 30, 40]}],
 "locked": [{"surface": "editor-800", "id": "43"}],
 "forbidden": [{"surface": "editor-800", "id": "42",
                "rect": [0, 0, 30, 40]}]}

All keys are optional except version; unknown keys and duplicate surface/id keys
within a list are errors. IDs and surfaces are nonempty strings, matched exactly
(no wildcards). Rectangles are integer [x, y, width, height], positive size.
Approved moves waive geometry only for that existing route; optional rect pins
ALL its resulting occurrences. Without rect, any destination on that same surface
is allowed. They never waive missing occurrences. Locked routes must exist in
the baseline and retain its rectangle multiset, or ALL match the supplied rect;
locks override approvals. Forbidden entries reject any exact rectangle match,
including newly added routes. They require rect and need not exist at baseline.
Approvals and locks naming absent baseline routes are errors (catching typos).

Every hit surface is checked. Menu/search ID and hit-route multiplicity must not
decrease. Existing out-of-parent control records are grandfathered only when
identical, with multiplicity; new/changed violations fail even if a move is
approved. Bounds records include class, label and rectangle, not only control ID.
"""
import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path


def rows(folder, name):
    with (Path(folder) / name).open(encoding='utf-8-sig', newline='') as source:
        return list(csv.DictReader(source))


def hit_groups(data):
    grouped = defaultdict(Counter)
    for row in data:
        grouped[row['surface'], row['id']][tuple(int(row[k]) for k in
                                                 ('x', 'y', 'width', 'height'))] += 1
    return grouped


def validate_constraints(constraints, baseline):
    if constraints is None:
        return {name: {} for name in ('approved_moves', 'locked', 'forbidden')}
    names = ('approved_moves', 'locked', 'forbidden')
    if (not isinstance(constraints, dict) or set(constraints) - {'version', *names}
            or type(constraints.get('version')) is not int or constraints['version'] != 1):
        raise ValueError('constraints must have version 1 and only documented keys')
    result = {}
    for name in names:
        entries = constraints.get(name, [])
        if not isinstance(entries, list):
            raise ValueError(f'{name} must be a list')
        result[name] = {}
        for entry in entries:
            if (not isinstance(entry, dict) or set(entry) - {'surface', 'id', 'rect'}
                    or any(not isinstance(entry.get(k), str) or not entry[k]
                           for k in ('surface', 'id'))):
                raise ValueError(f'invalid {name} entry: {entry!r}')
            key = entry['surface'], entry['id']
            if key in result[name]:
                raise ValueError(f'duplicate {name} route: {key}')
            rect = entry.get('rect')
            if 'rect' in entry:
                if (not isinstance(rect, list) or len(rect) != 4
                        or any(type(n) is not int for n in rect)
                        or rect[2] <= 0 or rect[3] <= 0):
                    raise ValueError(f'invalid rectangle: {rect!r}')
                rect = tuple(rect)
            elif name == 'forbidden':
                raise ValueError('forbidden entries require rect')
            if name != 'forbidden' and key not in baseline:
                raise ValueError(f'{name} route absent from baseline: {key}')
            result[name][key] = rect
    return result


def compare(before, after, constraints=None):
    old_menus, new_menus = rows(before, 'menus.csv'), rows(after, 'menus.csv')
    old_commands, new_commands = rows(before, 'commands.csv'), rows(after, 'commands.csv')
    missing_menus = Counter(r['id'] for r in old_menus) - Counter(r['id'] for r in new_menus)
    missing_commands = Counter(r['id'] for r in old_commands) - Counter(r['id'] for r in new_commands)
    old_hits = hit_groups(rows(before, 'hits.csv'))
    new_hits = hit_groups(rows(after, 'hits.csv'))
    rules = validate_constraints(constraints, old_hits)
    missing_routes, changed, approved, violations = [], [], [], []
    for key, old_rects in sorted(old_hits.items()):
        new_rects = new_hits.get(key, Counter())
        missing_count = max(0, sum(old_rects.values()) - sum(new_rects.values()))
        if missing_count:
            missing_routes.append({'surface': key[0], 'id': key[1], 'count': missing_count})
        deficits = old_rects - new_rects
        if key in rules['locked']:
            target = rules['locked'][key]
            valid = (new_rects == old_rects if target is None else
                     bool(new_rects) and all(rect == target for rect in new_rects))
            if not valid:
                violations.append({'rule': 'locked', 'surface': key[0], 'id': key[1]})
        elif key in rules['approved_moves']:
            target = rules['approved_moves'][key]
            if target is not None and any(rect != target for rect in new_rects):
                violations.append({'rule': 'approved_moves.rect', 'surface': key[0], 'id': key[1]})
            if (deficits and not missing_count
                    and (target is None or all(rect == target for rect in new_rects))):
                approved.append({'surface': key[0], 'id': key[1]})
        else:
            changed.extend((*key, *map(str, rect)) for rect in sorted(deficits)
                           for _ in range(deficits[rect]))
    for key, rect in rules['forbidden'].items():
        if rect in new_hits.get(key, {}):
            violations.append({'rule': 'forbidden', 'surface': key[0], 'id': key[1], 'rect': rect})

    old_captures, new_captures = rows(before, 'captures.csv'), rows(after, 'captures.csv')
    absent_captures = sorted({r['file'] for r in old_captures} - {r['file'] for r in new_captures})
    old_bounds = [r for r in rows(before, 'controls.csv') if r['outside_parent'] == '1']
    bounds = [r for r in rows(after, 'controls.csv') if r['outside_parent'] == '1']
    known_bounds = Counter(tuple(sorted(r.items())) for r in old_bounds)
    regressions = []
    for row in bounds:
        key = tuple(sorted(row.items()))
        if known_bounds[key]:
            known_bounds[key] -= 1
        else:
            regressions.append(row)
    return {
        'baseline_menu_entries': len(old_menus), 'after_menu_entries': len(new_menus),
        'baseline_search_entries': len(old_commands), 'after_search_entries': len(new_commands),
        'missing_menu_ids_with_counts': dict(missing_menus),
        'missing_search_ids': sorted(missing_commands),
        'missing_search_ids_with_counts': dict(missing_commands),
        'missing_hit_routes_with_counts': missing_routes,
        'missing_or_changed_editor_hit_rectangles': [r for r in changed if r[0].startswith('editor-')],
        'missing_or_changed_hit_rectangles': changed,
        'approved_relocated_hit_routes': approved, 'constraint_violations': violations,
        'missing_captures': absent_captures, 'after_outside_parent_controls': bounds,
        'new_or_changed_outside_parent_controls': regressions,
        'effective_dpi_values': sorted({r['effective_dpi'] for r in new_captures}),
        'dpi_scope': 'Synthetic WM_DPICHANGED + fixed pixel viewports; effective DPI separately observed. No physical monitor scaling claim.',
        'scope': 'Runtime menu/search ID multiplicity and all visible hit routes/rectangles in matching fixtures; offscreen scroll positions and menu semantic equivalence need separate tests.',
        'passed_preservation': not (missing_menus or missing_commands or missing_routes
                                    or changed or violations or absent_captures or regressions),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--constraints', type=Path, help='Version 1 explicit route constraints JSON; see schema above')
    args = parser.parse_args()
    try:
        constraints = (json.loads(args.constraints.read_text(encoding='utf-8-sig'))
                       if args.constraints else None)
        if args.constraints and constraints is None:
            raise ValueError('constraints file must contain a version 1 object, not null')
        result = compare(args.before, args.after, constraints)
    except (ValueError, OSError, KeyError) as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k != 'after_outside_parent_controls'}, ensure_ascii=False, indent=2))
    print('Controls outside direct parent:', len(result['after_outside_parent_controls']))
    return 0 if result['passed_preservation'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
