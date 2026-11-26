"""Stdlib regression tests; run: python -m unittest discover -s scripts -p test_compare_ui_evidence.py."""
import copy
import csv
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name('compare-ui-evidence.py')
SPEC = importlib.util.spec_from_file_location('compare_ui_evidence', SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class PreservationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.before, self.after = self.root / 'before', self.root / 'after'
        self.old = {
            'menus.csv': [{'id': '7', 'depth': '1', 'route': 'File'},
                          {'id': '7', 'depth': '2', 'route': 'Other'}],
            'commands.csv': [{'id': '7', 'title': 'Open', 'detail': ''}] * 2,
            'hits.csv': [self.hit(), self.hit('dialog-800', '8')],
            'captures.csv': [{'file': 'editor.png', 'surface': 'editor-800', 'effective_dpi': '96'}],
            'controls.csv': [self.control('0')],
        }
        self.new = copy.deepcopy(self.old)

    @staticmethod
    def hit(surface='editor-800', identifier='7', x='10'):
        return dict(surface=surface, id=identifier, x=x, y='20', width='30', height='40')

    @staticmethod
    def control(outside='1', left='10'):
        return dict(surface='dialog-800', id='8', **{'class': 'Button'}, label='Apply',
                    left=left, top='20', right='40', bottom='60', outside_parent=outside)

    def write(self):
        for folder, data in ((self.before, self.old), (self.after, self.new)):
            folder.mkdir(exist_ok=True)
            for name, records in data.items():
                with (folder / name).open('w', encoding='utf-8-sig', newline='') as stream:
                    writer = csv.DictWriter(stream, fieldnames=self.old[name][0].keys())
                    writer.writeheader()
                    writer.writerows(records)

    def compare(self, constraints=None):
        self.write()
        return MODULE.compare(self.before, self.after, constraints)

    @staticmethod
    def rules(name='approved_moves', **extra):
        entry = dict(surface='editor-800', id='7', **extra)
        return {'version': 1, name: [entry]}

    def test_two_argument_api_and_additions(self):
        self.new['hits.csv'].append(self.hit('new-dialog', '99'))
        self.new['menus.csv'].append(dict(id='99', depth='1', route='New'))
        self.new['commands.csv'].append(dict(id='99', title='New', detail=''))
        self.write()
        self.assertTrue(MODULE.compare(self.before, self.after)['passed_preservation'])

    def test_menu_and_search_duplicates_cannot_disappear(self):
        self.new['menus.csv'].pop()
        self.new['commands.csv'].pop()
        result = self.compare()
        self.assertFalse(result['passed_preservation'])
        self.assertEqual(result['missing_menu_ids_with_counts'], {'7': 1})
        self.assertEqual(result['missing_search_ids_with_counts'], {'7': 1})
        self.assertEqual(result['missing_search_ids'], ['7'])

    def test_all_surfaces_default_to_strict_geometry(self):
        for index in (0, 1):
            with self.subTest(surface=self.new['hits.csv'][index]['surface']):
                self.new = copy.deepcopy(self.old)
                self.new['hits.csv'][index]['x'] = '100'
                result = self.compare()
                self.assertFalse(result['passed_preservation'])
                self.assertEqual(len(result['missing_or_changed_hit_rectangles']), 1)
                self.assertEqual(result['missing_hit_routes_with_counts'], [])

    def test_exact_move_and_optional_destination(self):
        self.new['hits.csv'][0]['x'] = '100'
        for rule in (self.rules(), self.rules(rect=[100, 20, 30, 40])):
            with self.subTest(rule=rule):
                result = self.compare(rule)
                self.assertTrue(result['passed_preservation'])
                self.assertEqual(result['approved_relocated_hit_routes'],
                                 [{'surface': 'editor-800', 'id': '7'}])
        result = self.compare(self.rules(rect=[101, 20, 30, 40]))
        self.assertFalse(result['passed_preservation'])
        self.assertEqual(result['constraint_violations'][0]['rule'], 'approved_moves.rect')
        self.assertEqual(result['approved_relocated_hit_routes'], [])

    def test_approval_does_not_allow_other_moves(self):
        self.new['hits.csv'][0]['x'] = '100'
        self.new['hits.csv'][1]['x'] = '100'
        self.assertFalse(self.compare(self.rules())['passed_preservation'])

    def test_approval_cannot_remove_route_or_move_to_another_surface(self):
        for replacement in ([], [self.hit('other-surface')], [self.hit(identifier='9')]):
            with self.subTest(replacement=replacement):
                self.new['hits.csv'] = replacement + [self.old['hits.csv'][1]]
                result = self.compare(self.rules())
                self.assertFalse(result['passed_preservation'])
                self.assertEqual(result['missing_hit_routes_with_counts'],
                                 [{'surface': 'editor-800', 'id': '7', 'count': 1}])

    def test_hit_duplicate_loss_fails_even_with_approval(self):
        self.old['hits.csv'].append(self.hit())
        self.assertFalse(self.compare()['passed_preservation'])
        result = self.compare(self.rules())
        self.assertFalse(result['passed_preservation'])
        self.assertEqual(result['missing_hit_routes_with_counts'][0]['count'], 1)

    def test_destination_applies_to_every_occurrence(self):
        self.old['hits.csv'].append(self.hit())
        self.new['hits.csv'].append(self.hit(x='100'))
        self.assertFalse(self.compare(self.rules(rect=[100, 20, 30, 40]))['passed_preservation'])
        self.new['hits.csv'][0]['x'] = '100'
        self.assertTrue(self.compare(self.rules(rect=[100, 20, 30, 40]))['passed_preservation'])

    def test_locked_baseline_overrides_move_approval(self):
        rules = self.rules()
        rules['locked'] = [{'surface': 'editor-800', 'id': '7'}]
        self.assertTrue(self.compare(rules)['passed_preservation'])
        self.new['hits.csv'][0]['x'] = '100'
        result = self.compare(rules)
        self.assertFalse(result['passed_preservation'])
        self.assertEqual(result['constraint_violations'][0]['rule'], 'locked')

    def test_locked_explicit_destination_and_forbidden_override(self):
        self.new['hits.csv'][0]['x'] = '100'
        rules = self.rules('locked', rect=[100, 20, 30, 40])
        self.assertTrue(self.compare(rules)['passed_preservation'])
        rules['forbidden'] = [{'surface': 'editor-800', 'id': '7', 'rect': [100, 20, 30, 40]}]
        self.assertFalse(self.compare(rules)['passed_preservation'])

    def test_lock_rejects_added_occurrence_at_another_position(self):
        self.new['hits.csv'].append(self.hit(x='100'))
        self.assertTrue(self.compare()['passed_preservation'])
        self.assertFalse(self.compare(self.rules('locked'))['passed_preservation'])

    def test_forbidden_applies_to_added_route(self):
        self.new['hits.csv'].append(self.hit('new', '99'))
        rules = {'version': 1, 'forbidden': [{'surface': 'new', 'id': '99', 'rect': [10, 20, 30, 40]}]}
        self.assertFalse(self.compare(rules)['passed_preservation'])
        rules['forbidden'][0]['rect'][0] = 11
        self.assertTrue(self.compare(rules)['passed_preservation'])

    def test_bounds_baseline_is_reported_but_only_regressions_fail(self):
        self.old['controls.csv'] = [self.control()]
        self.new['controls.csv'] = [self.control()]
        result = self.compare()
        self.assertTrue(result['passed_preservation'])
        self.assertEqual(len(result['after_outside_parent_controls']), 1)
        for records in ([self.control(left='11')], [self.control(), self.control()]):
            with self.subTest(records=records):
                self.new['controls.csv'] = records
                self.assertFalse(self.compare(self.rules())['passed_preservation'])
        self.new['controls.csv'] = [self.control('0')]
        self.assertTrue(self.compare()['passed_preservation'])

    def test_new_bounds_violation_and_missing_capture_fail(self):
        self.new['controls.csv'] = [self.control()]
        self.assertFalse(self.compare()['passed_preservation'])
        self.new['controls.csv'] = copy.deepcopy(self.old['controls.csv'])
        self.new['captures.csv'] = []
        self.assertEqual(self.compare()['missing_captures'], ['editor.png'])

    def test_invalid_constraints_fail_closed(self):
        cases = [{}, {'version': True}, {'version': 2}, {'version': 1, 'approve_all': True},
                 {'version': 1, 'approved_moves': {}}, self.rules(rect=None),
                 self.rules(rect=[0, 0, 0, 1]), self.rules(rect=[True, 0, 1, 1]),
                 self.rules(rect=[0, 0, 1.5, 1]), self.rules(rect=[0, 1]),
                 self.rules('forbidden'),
                 {'version': 1, 'locked': [{'surface': 'typo', 'id': '7'}]},
                 {'version': 1, 'approved_moves': [{'surface': '*', 'id': '7'}]},
                 {'version': 1, 'approved_moves': [{'surface': 'editor-800', 'id': 7}]},
                 {'version': 1, 'approved_moves': [{'surface': 'editor-800', 'id': '7', 'typo': 1}]}]
        duplicate = self.rules()
        duplicate['approved_moves'] *= 2
        cases.append(duplicate)
        for rules in cases:
            with self.subTest(rules=rules), self.assertRaises(ValueError):
                self.compare(rules)

    def test_cli_constraints_report_and_exit_status(self):
        self.new['hits.csv'][0]['x'] = '100'
        self.write()
        constraints, report = self.root / 'rules.json', self.root / 'report.json'
        constraints.write_text(json.dumps(self.rules(rect=[100, 20, 30, 40])), encoding='utf-8')
        command = [sys.executable, str(SCRIPT), '--before', str(self.before),
                   '--after', str(self.after), '--output', str(report)]
        strict = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(strict.returncode, 1, strict.stderr)
        approved = subprocess.run(command + ['--constraints', str(constraints)], capture_output=True, text=True)
        self.assertEqual(approved.returncode, 0, approved.stderr)
        self.assertTrue(json.loads(report.read_text(encoding='utf-8'))['passed_preservation'])
        constraints.write_text('{"version":1,"approve_all":true}', encoding='utf-8')
        invalid = subprocess.run(command + ['--constraints', str(constraints)], capture_output=True, text=True)
        self.assertEqual(invalid.returncode, 2)
        self.assertIn('documented keys', invalid.stderr)
        constraints.write_text('null', encoding='utf-8')
        invalid = subprocess.run(command + ['--constraints', str(constraints)], capture_output=True, text=True)
        self.assertEqual(invalid.returncode, 2)


if __name__ == '__main__':
    unittest.main()
