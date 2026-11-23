"""Compare isolated runtime inventories, allowing added routes and icons."""
import argparse
import csv
import json
from collections import Counter
from pathlib import Path


def rows(folder, name):
    with (folder / name).open(encoding='utf-8-sig', newline='') as source:
        return list(csv.DictReader(source))


def compare(before, after):
    old_menus, new_menus = rows(before, 'menus.csv'), rows(after, 'menus.csv')
    old_commands, new_commands = rows(before, 'commands.csv'), rows(after, 'commands.csv')
    # Duplicate menu IDs matter: an existing second entry must not disappear.
    missing_menus = Counter(r['id'] for r in old_menus)-Counter(r['id'] for r in new_menus)
    missing_commands = sorted(set(r['id'] for r in old_commands)-set(r['id'] for r in new_commands))
    old_hits, new_hits = rows(before, 'hits.csv'), rows(after, 'hits.csv')
    # In the unselected editor scene, existing controls must occupy the same rect.
    def editor_hits(data):
        return {(r['surface'], r['id'], r['x'], r['y'], r['width'], r['height'])
                for r in data if r['surface'].startswith('editor-')}
    missing_geometry = sorted(editor_hits(old_hits)-editor_hits(new_hits))
    new_capture_rows = rows(after, 'captures.csv')
    old_capture_rows = rows(before, 'captures.csv')
    old_surfaces = {r['file'] for r in old_capture_rows}
    new_surfaces = {r['file'] for r in new_capture_rows}
    absent_captures = sorted(old_surfaces-new_surfaces)
    bounds = [r for r in rows(after, 'controls.csv') if r['outside_parent']=='1']
    return {'baseline_menu_entries': len(old_menus), 'after_menu_entries': len(new_menus),
            'baseline_search_entries': len(old_commands), 'after_search_entries': len(new_commands),
            'missing_menu_ids_with_counts': dict(missing_menus), 'missing_search_ids': missing_commands,
            'missing_or_changed_editor_hit_rectangles': missing_geometry,
            'missing_captures': absent_captures, 'after_outside_parent_controls': bounds,
            'effective_dpi_values': sorted(set(r['effective_dpi'] for r in new_capture_rows)),
            'dpi_scope': 'Synthetic WM_DPICHANGED + fixed pixel viewports; effective DPI separately observed. No physical monitor scaling claim.',
            'scope': 'Runtime menu/search inventory and visible editor hit rectangles in matching fixtures; offscreen scroll positions and menu semantic equivalence need separate tests.',
            'passed_preservation': not(missing_menus or missing_commands or missing_geometry or absent_captures)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = compare(args.before, args.after)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='after_outside_parent_controls'}, ensure_ascii=False, indent=2))
    print('Controls outside direct parent:', len(result['after_outside_parent_controls']))
    raise SystemExit(0 if result['passed_preservation'] else 1)
