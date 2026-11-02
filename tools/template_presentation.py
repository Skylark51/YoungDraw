"""Shared, idempotent drawing conventions for generated and bundled templates."""
import math
import xml.etree.ElementTree as ET

ELEMENT_COLORS = {'7': '#0000ff', '16': '#d4aa00'}


def apply_presentation(root, slug):
    atoms = {a.get('id'): a for a in root.findall('atom')}
    bonds = root.findall('bond')
    if slug in ('benzaldehyde', 'cyclohexanecarboxaldehyde'):
        for carbon in list(atoms.values()):
            if carbon.get('e') != '6':
                continue
            edges = [b for b in bonds if carbon.get('id') in (b.get('a'), b.get('b'))]
            adjacent = [(b, atoms[b.get('b') if b.get('a') == carbon.get('id') else b.get('a')]) for b in edges]
            if len(edges) != 2 or not any(b.get('kind') == '1' and a.get('e') == '8' for b, a in adjacent):
                continue
            x, y = map(float, carbon.get('p').split())
            dx = dy = 0
            for _, a in adjacent:
                ax, ay = map(float, a.get('p').split())
                length = math.hypot(ax-x, ay-y)
                dx -= (ax-x)/length
                dy -= (ay-y)/length
            length = math.hypot(dx, dy)
            new_id = 1 + max(int(c.get('id', '0')) for c in root)
            ET.SubElement(root, 'atom', id=str(new_id), e='1', p=f'{x+30*dx/length:.6f} {y+30*dy/length:.6f}', q='0', h='0')
            ET.SubElement(root, 'bond', id=str(new_id+1), a=carbon.get('id'), b=str(new_id), kind='0')
    if slug == 'mecn':
        for b in bonds:
            if b.get('kind') == '2':
                for key in ('a', 'b'):
                    a = atoms[b.get(key)]
                    if a.get('e') == '6':
                        a.set('label', 'C')
                        a.set('h', '0')
    # Fixed atom IDs come from the corresponding SMILES in prepare_lab_templates.
    positions = {
        'hbf4': {1: (-65, 0), 2: (0, 0), 3: (-26, -15), 4: (26, -15), 5: (-13, 28), 6: (13, 28)},
        'hclo4': {1: (-30, 17), 2: (0, 0), 3: (-16, -28), 4: (16, -28), 5: (30, 17), 6: (-56, 2)},
        'hotf': {1: (-30, 17), 2: (0, 0), 3: (-16, -28), 4: (16, -28), 5: (32, 18), 6: (32, 50), 7: (60, 2), 8: (60, 34), 9: (-56, 2)},
    }
    for i, p in positions.get(slug, {}).items():
        atoms[str(i)].set('p', f'{p[0]:.6f} {p[1]:.6f}')
        atoms[str(i)].set('protect', '1')
    if slug == 'hclo4':
        # Conventional neutral HO-Cl(=O)3 avoids charge-separated RDKit depiction.
        for a in atoms.values():
            a.set('q', '0')
        for b in bonds:
            if b.get('a') == '2' and b.get('b') in ('3', '4', '5'):
                b.set('kind', '1')
    if slug == 'hbf4':
        atoms['2'].set('annotationManual', '1')
        atoms['2'].set('annotationOffset', '-3 -23')
        for b in bonds:
            if b.get('b') in ('5', '6'):
                b.set('kind', '3' if b.get('b') == '5' else '4')
    if slug == 'hotf':
        for b in bonds:
            if b.get('a') == '5' and b.get('b') in ('6', '8'):
                b.set('kind', '3' if b.get('b') == '6' else '4')
    for a in root.findall('atom'):
        color = ELEMENT_COLORS.get(a.get('e'))
        if color:
            a.set('color', color)
            for run in a.findall('run'):
                run.set('color', color)
