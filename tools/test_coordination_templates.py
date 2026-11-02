"""Validate all refreshed scaffolds and linkage isomers, without third-party tools."""
from pathlib import Path
import json
import math
import unittest
import xml.etree.ElementTree as ET
from coordination_motifs import validate_motif

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'resources' / 'lab_templates'


class CoordinationTemplates(unittest.TestCase):
    def test_presentation(self):
        from template_presentation import apply_presentation, ELEMENT_COLORS
        import hashlib
        for entry in json.loads((OUT / 'manifest.json').read_text(encoding='utf-8')):
            data = (OUT / (entry['slug'] + '.ydraw')).read_text(encoding='utf-8')
            self.assertEqual(hashlib.sha256(data.encode()).hexdigest(), entry['sha256'])
            root = ET.fromstring(data)
            apply_presentation(root, entry['slug'])
            self.assertEqual(ET.tostring(root, encoding='unicode'), data, entry['slug'] + ' is idempotent')
            for a in root.findall('atom'):
                if a.get('e') in ELEMENT_COLORS:
                    self.assertEqual(a.get('color'), ELEMENT_COLORS[a.get('e')])
        for slug in ('benzaldehyde', 'cyclohexanecarboxaldehyde'):
            root = ET.parse(OUT / (slug + '.ydraw')).getroot()
            atoms = {a.get('id'): a for a in root.findall('atom')}
            h = next(a for a in atoms.values() if a.get('e') == '1')
            edge = next(b for b in root.findall('bond') if h.get('id') in (b.get('a'), b.get('b')))
            carbon = edge.get('b') if edge.get('a') == h.get('id') else edge.get('a')
            self.assertEqual(atoms[carbon].get('e'), '6')
            self.assertTrue(any(b.get('kind') == '1' and carbon in (b.get('a'), b.get('b')) for b in root.findall('bond')))
        root = ET.parse(OUT / 'mecn.ydraw').getroot()
        self.assertEqual(next(a for a in root.findall('atom') if a.get('id') == '2').get('label'), 'C')
        root = ET.parse(OUT / 'hclo4.ydraw').getroot()
        self.assertTrue(all(a.get('q') == '0' for a in root.findall('atom')))
        self.assertEqual(sum(b.get('kind') == '1' for b in root.findall('bond')), 3)
        root = ET.parse(OUT / 'hbf4.ydraw').getroot()
        self.assertEqual(sorted(int(b.get('kind')) for b in root.findall('bond')), [0, 0, 3, 4])
        self.assertEqual(sum(int(a.get('q')) for a in root.findall('atom')), 0)

    def test_all_motifs(self):
        entries = json.loads((OUT / 'manifest.json').read_text(encoding='utf-8'))
        counts = {}
        for entry in entries:
            root = ET.parse(OUT / (entry['slug'] + '.ydraw')).getroot()
            if entry.get('geometryRevision') != 2:
                continue
            atoms = {int(a.get('id')): a for a in root.findall('atom')}
            metal = next(i for i, a in atoms.items() if a.get('label') == 'M')
            species = entry.get('motif')
            if not species:
                species = next(s for s in ['hydroperoxo-side', 'hydroperoxo-end', 'peroxo-side', 'peroxo-end'] if entry['slug'].endswith(s))
            validate_motif(root, metal, species)
            if not entry.get('motif'):
                continue
            counts[species] = counts.get(species, 0) + 1
            base_slug = entry['slug'].removesuffix('-' + species) + '-open'
            base = ET.parse(OUT / (base_slug + '.ydraw')).getroot()
            base_ids = {int(a.get('id')) for a in base.findall('atom')}
            added = set(atoms) - base_ids
            self.assertEqual(sorted(int(atoms[i].get('e')) for i in added), [7, 8] if species == 'nitrosyl' else [7, 8, 8])
            bonds = {frozenset((int(b.get('a')), int(b.get('b')))): int(b.get('kind')) for b in root.findall('bond')}
            contacts = [i for i in added if frozenset((metal, i)) in bonds]
            self.assertEqual([int(atoms[i].get('e')) for i in contacts], [8, 8] if species == 'nitrite-bidentate' else [8] if species == 'nitrito' else [7])
            self.assertTrue(all(atoms[i].get('protect') == '1' for i in added))
            self.assertEqual(atoms[metal].get('label'), 'M')
            if species == 'nitrosyl':
                nitrogen = contacts[0]
                oxygen = next(i for i in added if atoms[i].get('e') == '8')
                self.assertEqual(bonds[frozenset((nitrogen, oxygen))], 1)
                p = {i: tuple(map(float, atoms[i].get('p').split())) for i in [metal, nitrogen, oxygen]}
                self.assertAlmostEqual(math.dist(p[metal], p[oxygen]), math.dist(p[metal], p[nitrogen]) + math.dist(p[nitrogen], p[oxygen]))
        self.assertEqual(counts, dict.fromkeys(['nitrosyl', 'nitro', 'nitrito', 'nitrite-bidentate'], 14))


if __name__ == '__main__':
    unittest.main()
