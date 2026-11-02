"""Shared, editable drawing geometry for coordination-template ligands.

Coordinates are 2D diagram conventions, not optimized structures.
This module has no third-party dependency and is also used by the full generator.
"""
import math
import xml.etree.ElementTree as ET


def add_atom(root, element, position, **attrs):
    identity = 1 + max((int(x.get('id', '0')) for x in root), default=0)
    values = dict(id=str(identity), e=str(element), p=' '.join(f'{v:.6f}' for v in position),
                  q='0', h='0', protect='1')
    values.update({k: str(v) for k, v in attrs.items()})
    ET.SubElement(root, 'atom', values)
    return identity


def add_bond(root, first, second, kind=0):
    identity = 1 + max(int(x.get('id', '0')) for x in root)
    ET.SubElement(root, 'bond', id=str(identity), a=str(first), b=str(second), kind=str(kind))


def add_motif(root, metal, species, orientation):
    if species == 'open':
        return
    atom = next(a for a in root.findall('atom') if a.get('id') == str(metal))
    origin = tuple(map(float, atom.get('p').split()))

    def point(x, y):
        dx, dy = (x, y) if orientation == 'axial' else (-y, x)
        return origin[0] + dx, origin[1] + dy

    def oxygen(x, y, **kw):
        return add_atom(root, 8, point(x, y), **kw)

    if species.startswith(('peroxo-', 'hydroperoxo-')):
        side = species.endswith('-side')
        if side:
            first, second = oxygen(-22, -39), oxygen(22, -39)
        else:
            first, second = oxygen(0, -34), oxygen(26, -49)
        add_bond(root, metal, first)
        add_bond(root, first, second)
        if side:
            add_bond(root, metal, second)
        if species.startswith('hydroperoxo'):
            hydrogen = add_atom(root, 1, point(40, -57) if side else point(47, -34))
            add_bond(root, second, hydrogen)
    elif species == 'nitrosyl':
        nitrogen = add_atom(root, 7, point(0, -34))
        terminal = oxygen(0, -59)
        add_bond(root, metal, nitrogen)
        add_bond(root, nitrogen, terminal, 1)
    elif species == 'nitro':
        nitrogen = add_atom(root, 7, point(0, -36))
        first, second = oxygen(-22, -50), oxygen(22, -50, q=-1)
        add_bond(root, metal, nitrogen)
        add_bond(root, nitrogen, first, 1)
        add_bond(root, nitrogen, second)
    elif species == 'nitrito':
        first = oxygen(0, -34, q=-1)
        nitrogen = add_atom(root, 7, point(23, -48))
        terminal = oxygen(23, -73)
        add_bond(root, metal, first)
        add_bond(root, first, nitrogen)
        add_bond(root, nitrogen, terminal, 1)
    elif species == 'nitrite-bidentate':
        nitrogen = add_atom(root, 7, point(0, -49))
        first, second = oxygen(-22, -31), oxygen(22, -31, q=-1)
        add_bond(root, metal, first)
        add_bond(root, metal, second)
        add_bond(root, nitrogen, first, 1)
        add_bond(root, nitrogen, second)
    else:
        raise ValueError(f'Unknown coordination motif: {species}')


def validate_motif(root, metal, species):
    atoms = {int(a.get('id')): a for a in root.findall('atom')}
    bonds = [(int(b.get('a')), int(b.get('b'))) for b in root.findall('bond')]
    assert all(a in atoms and b in atoms and a != b for a, b in bonds)
    assert len({frozenset((a, b)) for a, b in bonds}) == len(bonds)
    p = {i: tuple(map(float, a.get('p').split())) for i, a in atoms.items()}
    assert all(math.isfinite(v) for xy in p.values() for v in xy)
    oxygens = [i for i, a in atoms.items() if a.get('e') == '8']
    if species.startswith(('peroxo-', 'hydroperoxo-')):
        assert len(oxygens) == 2
        contacts = [i for i in oxygens if frozenset((metal, i)) in {frozenset(b) for b in bonds}]
        assert len(contacts) == (2 if species.endswith('-side') else 1)
        if species.endswith('-side'):
            oo = math.dist(p[oxygens[0]], p[oxygens[1]])
            mo = math.dist(p[metal], p[contacts[0]])
            assert .95 < oo / mo < 1.05
        else:
            assert abs(math.dist(p[metal], p[contacts[0]]) - 34) < .0001
