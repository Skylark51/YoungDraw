"""Reproducible source inventory; string candidates are NOT a runtime feature count."""
import argparse
import bisect
import hashlib
import json
import re
import subprocess
from pathlib import Path


def inventory(root):
    files = []
    strings = []
    routes = []
    groups = []
    # Include every source/header/resource, not just the three edited dialogs.
    for path in sorted([*root.glob('src/*.cpp'), *root.glob('src/*.h'), *root.glob('resources/*.rc')]):
        text = path.read_text(encoding='utf-8-sig')
        newlines = [m.start() for m in re.finditer('\n', text)]
        def line_at(position):
            return bisect.bisect_right(newlines, position)+1
        relative = path.relative_to(root).as_posix()
        literals = list(re.finditer(r'(?:L|u8)?"(?:\\.|[^"\\])*"', text))
        files.append({'path': relative, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                      'literal_count': len(literals),
                      'window_classes': sorted(set(re.findall(r'L"(YoungDraw\w+)"', text)))})
        for match in literals:
            line = line_at(match.start())
            value = match.group()
            strings.append({'path': relative, 'line': line, 'literal': value,
                            'wide': value.startswith('L"'),
                            'context': text[max(0, match.start()-90):min(len(text), match.end()+80)].replace('\n', ' ')})
        for match in re.finditer(r'\{\s*([\w]+)\s*,\s*(L"(?:\\.|[^"\\])*")', text):
            routes.append({'path': relative, 'line': line_at(match.start()),
                           'id_expression': match[1], 'label_literal': match[2]})
        for match in re.finditer(r'group\(\{([^}]+)\}\)', text):
            groups.append({'path': relative, 'line': line_at(match.start()),
                           'ids': [s.strip() for s in match[1].split(',')]})
    commit = subprocess.check_output(['git', '-c', f'safe.directory={root.as_posix()}',
                                      '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    return {'scope': 'All src/*.cpp, src/*.h, resources/*.rc literals including non-UI strings; candidate initializer routes and toolbar group calls. Runtime menu/hit inventory is authoritative.',
            'commit': commit, 'files': files, 'string_candidates': strings,
            'initializer_route_candidates': routes, 'toolbar_groups': groups}


def compare(before, after):
    # Labels may intentionally change. IDs and group order must remain available.
    existing = {(r['path'], r['id_expression']) for r in before['initializer_route_candidates']}
    current = {(r['path'], r['id_expression']) for r in after['initializer_route_candidates']}
    missing = sorted(existing-current)
    def stable_groups(items):
        return [(item['path'], item['ids']) for item in items]
    groups_changed = stable_groups(before['toolbar_groups']) != stable_groups(after['toolbar_groups'])
    return {'missing_route_candidates': missing, 'toolbar_group_order_changed': groups_changed,
            'warning': 'Candidate changes need review; this parser is not a C++ interpreter and cannot prove reachability or unchanged icon geometry.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    args = parser.parse_args()
    result = inventory(args.root.resolve())
    if args.baseline:
        result['comparison'] = compare(json.loads(args.baseline.read_text(encoding='utf-8')), result)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f"Inventoried {len(result['files'])} files, {len(result['string_candidates'])} literal candidates; {args.output}")
