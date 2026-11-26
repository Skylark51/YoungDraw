"""Numerical, inventory, output and CLI regression tests for the UX auditor."""
import csv
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('ux-auditor.py')
SPEC = importlib.util.spec_from_file_location('ux_auditor', SCRIPT)
UX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(UX)


class AuditorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.evidence = self.root / 'evidence'
        self.evidence.mkdir()
        self.rows = {
            'menus': [['7', '2', 'File / <Export>']],
            'commands': [['7', '<script>alert(1)</script>', 'PNG image']],
            'hits': [['editor-800', '7', '10', '20', '40', '30']],
            'captures': [['editor-800.png', 'editor', '800', '600', '800', '600',
                          '192', '96', 'PrintWindow', 'synthetic-WM_DPICHANGED', '100']],
        }
        self.write_evidence()

    def write_evidence(self):
        for name, fields in UX.HEADERS.items():
            with (self.evidence / (name + '.csv')).open('w', encoding='utf-8-sig', newline='') as f:
                writer = csv.writer(f)
                writer.writerow(fields)
                writer.writerows(self.rows[name])

    def test_runtime_ids_merge_without_counting_routes_as_features(self):
        self.rows['menus'].append(['7', '3', 'Other / Export'])
        self.write_evidence()
        registry = UX.inventory(self.evidence)
        self.assertEqual(len(registry['features']), 1)
        self.assertEqual(len(registry['features'][0]['menus']), 2)
        self.assertEqual(registry['captures'][0]['requested_dpi'], 192)
        self.assertEqual(registry['captures'][0]['effective_dpi'], 96)

    def test_dijkstra_uses_cheaper_multistep_route_and_handles_unreachable(self):
        graph = {'nodes': [{'id': n} for n in 'abcd'], 'edges': [
            {'id': 'expensive', 'from': 'a', 'to': 'c', 'effective_cost': 9},
            {'id': 'first', 'from': 'a', 'to': 'b', 'effective_cost': 1},
            {'id': 'second', 'from': 'b', 'to': 'c', 'effective_cost': 2},
            {'id': 'cycle', 'from': 'b', 'to': 'a', 'effective_cost': 0}]}
        self.assertEqual(UX.shortest_path(graph, 'a', 'c'), {'cost': 3, 'edge_ids': ['first', 'second']})
        self.assertIsNone(UX.shortest_path(graph, 'a', 'd')['cost'])
        self.assertEqual(UX.shortest_path(graph, 'a', 'a')['cost'], 0)
        with self.assertRaises(UX.AuditError):
            UX.shortest_path(graph, 'missing', 'a')

    def test_weighted_cost_and_fitts_keep_units_explicit(self):
        result = UX.effective_cost({'costs': {'actions': 2, 'penalty': 1},
                                   'pointer': {'distance': 300, 'width': 32}},
                                  dict(UX.WEIGHTS, penalty=2))
        self.assertEqual(result['effective_cost'], 4)
        self.assertAlmostEqual(result['fitts_index_bits'], 3.375039, places=5)
        self.assertIsNone(result['fitts_predicted_seconds'])
        for bad in (-1, float('nan'), float('inf'), True):
            with self.subTest(bad=bad), self.assertRaises(UX.AuditError):
                UX.effective_cost({'costs': {'actions': bad}}, UX.WEIGHTS)
        with self.assertRaises(UX.AuditError):
            UX.effective_cost({'pointer': {'distance': 1, 'width': 0}}, UX.WEIGHTS)

    def test_graph_rejects_unknown_nodes_and_negative_weights(self):
        registry = UX.inventory(self.evidence)
        for config in ({'edges': [{'from': 'unknown', 'to': 'command:7', 'evidence': 'model'}]},
                       {'weights': {'actions': -1}}, {'nodes': [{'id': 'command:7'}]}):
            with self.subTest(config=config), self.assertRaises(UX.AuditError):
                UX.make_graph(registry, config)

    def test_inventory_rejects_dangling_hits_and_bad_csv(self):
        self.rows['hits'][0][0] = 'missing'
        self.write_evidence()
        with self.assertRaises(UX.AuditError):
            UX.inventory(self.evidence)
        (self.evidence / 'commands.csv').write_text('id,id\n7,7\n', encoding='utf-8')
        with self.assertRaises(UX.AuditError):
            UX.inventory(self.evidence)

    def test_constraints_report_missing_and_unreachable_instead_of_passing(self):
        registry = UX.inventory(self.evidence)
        graph = UX.make_graph(registry, {'nodes': [{'id': 'disconnected'}]})
        scenarios = UX.scenarios(graph, {'workflows': [{'id': 'task', 'start': 'disconnected',
                                'goal': 'command:7', 'evidence': 'explicit test model'}]})
        checks = UX.constraints(registry, scenarios, {'required_command_ids': [7, 99],
                                'max_workflow_cost': {'task': 2}})
        self.assertEqual([c['status'] for c in checks['checks']], ['pass', 'fail', 'unverifiable'])

    def test_audit_writes_eight_artifacts_escapes_html_and_keeps_human_data_missing(self):
        output = self.root / 'out'
        summary = UX.audit(self.evidence, output)
        self.assertEqual(len(list(output.iterdir())), 8)
        self.assertEqual(summary['human_evidence']['status'], 'missing')
        self.assertEqual(summary['model_coverage']['covered'], 0)
        report = (output / 'ux_report.html').read_text(encoding='utf-8')
        self.assertNotIn('<script>alert(1)</script>', report)
        self.assertIn('&lt;script&gt;', report)
        for file in output.glob('*.json'):
            json.loads(file.read_text(encoding='utf-8'))

    def test_duplicate_json_keys_and_cli_constraints_fail_closed(self):
        config = self.root / 'constraints.json'
        config.write_text('{"required_command_ids":[99]}', encoding='utf-8')
        command = [sys.executable, str(SCRIPT), '--evidence-dir', str(self.evidence),
                   '--output-dir', str(self.root / 'cli'), '--constraints', str(config), '--fail-on-constraints']
        self.assertEqual(subprocess.run(command, capture_output=True).returncode, 1)
        config.write_text('{"a":1,"a":2}', encoding='utf-8')
        self.assertEqual(subprocess.run(command, capture_output=True).returncode, 2)


if __name__ == '__main__':
    unittest.main()
