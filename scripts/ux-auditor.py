#!/usr/bin/env python3
"""Evidence-first YoungDraw UX inventory and explicitly modeled route analysis.

Python standard library only. See docs/ux/README.md for the version 1 contract.
No UI is launched and no source-string inventory is used.
"""
import argparse
import csv
import hashlib
import heapq
import html
import io
import json
import math
from pathlib import Path
import sys


class AuditError(ValueError):
    """An actionable input error, suitable for CLI stderr."""


HEADERS = {
    "menus": "id depth route".split(),
    "commands": "id title detail".split(),
    "hits": "surface id x y width height".split(),
    "captures": ("file surface requested_width requested_height client_width "
                 "client_height requested_dpi effective_dpi method dpi_probe "
                 "nonuniform_samples").split(),
}
WEIGHTS = {"actions": 1.0, "key_presses": 1.0, "context_switches": 1.0,
           "penalty": 1.0}
LIMITATIONS = [
    "Inventory is observed command IDs, not a count of all application features.",
    "Menu depth and direct hit activation are modeled access routes, not task completion proof.",
    "Search titles establish discoverability inventory only; no search cost is inferred.",
    "Selection, enabled state, modal preconditions and document correctness need explicit workflow evidence.",
    "Capture DPI probes may be synthetic WM_DPICHANGED; requested DPI is not physical monitor validation.",
    "No human completion times, satisfaction, error rates or cognitive scores were measured.",
]


def require(condition, message):
    if not condition:
        raise AuditError(message)


def number(value, context, minimum=0):
    require(not isinstance(value, bool), f"{context}: expected a finite number")
    try:
        result = float(value)
    except (ValueError, TypeError):
        raise AuditError(f"{context}: expected a finite number") from None
    require(math.isfinite(result) and result >= minimum,
            f"{context}: expected a finite number >= {minimum}")
    return result


def integer(value, context, minimum=None):
    require(not isinstance(value, bool), f"{context}: expected an integer")
    try:
        result = int(str(value))
    except (ValueError, TypeError):
        raise AuditError(f"{context}: expected an integer") from None
    require(minimum is None or result >= minimum, f"{context}: must be >= {minimum}")
    return result


def text_id(value, context):
    require(isinstance(value, str) and bool(value.strip()), f"{context}: expected nonempty string")
    return value


def object_value(value, context):
    require(isinstance(value, dict), f"{context}: expected object")
    return value


def array(value, context):
    require(isinstance(value, list), f"{context}: expected array")
    return value


def keys(value, allowed, context):
    object_value(value, context)
    require(not (set(value) - set(allowed)),
            f"{context}: unknown fields {sorted(set(value) - set(allowed))}")


def read_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f"{path}: duplicate JSON key {key!r}")
            result[key] = value
        return result
    try:
        return json.loads(Path(path).read_text(encoding="utf-8-sig"), object_pairs_hook=unique,
                          parse_constant=lambda v: (_ for _ in ()).throw(AuditError(f"{path}: invalid {v}")))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise AuditError(f"{path}: {exc}") from None


def read_csv(path, kind):
    try:
        with Path(path).open(encoding="utf-8-sig", newline="") as stream:
            reader = csv.DictReader(stream, strict=True)
            headers = reader.fieldnames or []
            require(len(headers) == len(set(headers)), f"{path}: duplicate columns")
            require(set(HEADERS[kind]) <= set(headers), f"{path}: requires {HEADERS[kind]}")
            result = []
            for row in reader:
                require(None not in row and all(v is not None for v in row.values()),
                        f"{path}:{reader.line_num}: wrong column count")
                result.append(row)
            return result
    except (OSError, UnicodeError, csv.Error) as exc:
        raise AuditError(f"{path}: {exc}") from None


def inventory(directory):
    """Load the exact CSV layout written by tests/ui_access_validation.cpp."""
    rows = {kind: read_csv(directory / (kind + ".csv"), kind) for kind in HEADERS}
    features, captures = {}, {}

    def feature(value):
        ident = integer(value, "command/hit id")
        return features.setdefault(ident, {"id": f"command:{ident}", "command_id": ident,
                                           "menus": [], "search": [], "hits": []})

    for row in rows["captures"]:
        name = text_id(row["file"], "capture file")
        require("/" not in name and "\\" not in name and name.lower().endswith(".png"),
                "capture file must be a PNG basename")
        key = name[:-4]
        require(key not in captures, f"duplicate capture key {key}")
        capture = dict(row, key=key)
        for field in ("requested_width", "requested_height", "client_width", "client_height",
                      "requested_dpi", "effective_dpi", "nonuniform_samples"):
            capture[field] = integer(row[field], f"capture {key}.{field}",
                                     0 if field == "nonuniform_samples" else 1)
        text_id(row["surface"], "capture surface")
        captures[key] = capture
    for row in rows["menus"]:
        integer(row["id"], "menu id", 1)
        feature(row["id"])["menus"].append({
            "route": text_id(row["route"], "menu route"),
            "depth": integer(row["depth"], "menu depth", 1)})
    for row in rows["commands"]:
        integer(row["id"], "search command id", 1)
        feature(row["id"])["search"].append({
            "title": text_id(row["title"], "command title"), "detail": row["detail"]})
    for row in rows["hits"]:
        require(row["surface"] in captures, f"hit references missing capture {row['surface']!r}")
        hit = {"surface": row["surface"]}
        for field in ("x", "y", "width", "height"):
            hit[field] = number(row[field], "hit " + field, -math.inf if field in ("x", "y") else 0)
        require(hit["width"] > 0 and hit["height"] > 0, "hit width and height must be positive")
        feature(row["id"])["hits"].append(hit)
    result = []
    for ident, item in sorted(features.items()):
        item["label"] = (item["search"][0]["title"] if item["search"] else
                         item["menus"][0]["route"] if item["menus"] else f"Hit ID {ident}")
        item["evidence"] = "runtime_inventory"
        item["semantic_status"] = "command_inventory" if item["menus"] or item["search"] else "hit_only_semantics_unverified"
        result.append(item)
    return {"schema_version": 1, "features": result, "captures": list(captures.values()),
            "row_counts": {k: len(v) for k, v in rows.items()}}


def effective_cost(edge, weights, fitts=None):
    costs = edge.get("costs", {"actions": 1})
    keys(costs, WEIGHTS, "edge costs")
    components = {key: number(value, "cost " + key) for key, value in costs.items()}
    total = sum(value * weights[key] for key, value in components.items())
    index, predicted = None, None
    if "pointer" in edge:
        pointer = edge["pointer"]
        keys(pointer, ("distance", "width"), "pointer")
        distance = number(pointer.get("distance"), "pointer distance")
        width = number(pointer.get("width"), "pointer width")
        require(width > 0, "pointer width must be positive")
        # Stable even when D/W would overflow floating point division.
        index = (math.log2(distance + width) - math.log2(width)
                 if math.isfinite(distance + width) else
                 math.log2(distance) + math.log2(1 + width / distance) - math.log2(width))
        if fitts is not None:
            predicted = fitts["a"] + fitts["b"] * index
            total += predicted * fitts["weight"]
    require(math.isfinite(total), "effective edge cost overflow")
    return {"effective_cost": total, "components": components, "fitts_index_bits": index,
            "fitts_predicted_seconds": predicted}


def make_graph(registry, config=None):
    config = {} if config is None else config
    keys(config, ("nodes", "edges", "weights", "fitts"), "graph")
    weights = dict(WEIGHTS)
    keys(config.get("weights", {}), WEIGHTS, "weights")
    weights.update({k: number(v, "weight " + k) for k, v in config.get("weights", {}).items()})
    fitts = config.get("fitts")
    if fitts is not None:
        keys(fitts, ("a", "b", "weight"), "fitts")
        fitts = {k: number(fitts.get(k), "fitts " + k) for k in ("a", "b", "weight")}
    nodes = {"editor:menu": {"id": "editor:menu", "kind": "modeled_menu_start"}}
    for capture in registry["captures"]:
        ident = "capture:" + capture["key"]
        nodes[ident] = {"id": ident, "kind": "observed_capture"}
    edges = []

    def add(source, target, costs, basis, **extras):
        edge = {"id": f"edge:{len(edges)}", "from": source, "to": target,
                "costs": costs, "basis": basis, **extras}
        edge.update(effective_cost(edge, weights, fitts))
        edges.append(edge)

    for item in registry["features"]:
        nodes[item["id"]] = {"id": item["id"], "kind": "observed_command"}
        for menu in item["menus"]:
            add("editor:menu", item["id"], {"actions": menu["depth"]},
                "modeled_from_runtime_menu_depth", route=menu["route"])
        for hit in item["hits"]:
            add("capture:" + hit["surface"], item["id"], {"actions": 1},
                "modeled_direct_hit_activation", rectangle=hit)
    for node in array(config.get("nodes", []), "graph nodes"):
        keys(node, ("id", "label"), "node")
        ident = text_id(node.get("id"), "node id")
        require(ident not in nodes, f"duplicate/reserved node {ident}")
        require(not ident.startswith(("command:", "capture:")), "custom nodes cannot impersonate inventory")
        nodes[ident] = dict(node, kind="explicit_state")
    for edge in array(config.get("edges", []), "graph edges"):
        keys(edge, ("from", "to", "costs", "pointer", "evidence"), "edge")
        source, target = edge.get("from"), edge.get("to")
        require(isinstance(source, str) and source in nodes, f"unknown edge source {source!r}")
        require(isinstance(target, str) and target in nodes, f"unknown edge target {target!r}")
        evidence = text_id(edge.get("evidence"), "explicit edge evidence/assumption")
        extras = {"evidence": evidence}
        if "pointer" in edge:
            extras["pointer"] = edge["pointer"]
        add(source, target, edge.get("costs", {"actions": 1}), "explicit_model", **extras)
    return {"schema_version": 1, "cost_unit": "weighted_model_units_not_human_time",
            "weights": weights, "fitts": fitts, "nodes": list(nodes.values()), "edges": edges}


def shortest_path(graph, start, goal):
    adjacency = {node["id"]: [] for node in graph["nodes"]}
    require(start in adjacency and goal in adjacency, "workflow references unknown node")
    for edge in graph["edges"]:
        adjacency[edge["from"]].append(edge)
    distances, parents, queue = {start: 0.0}, {}, [(0.0, start)]
    while queue:
        distance, node = heapq.heappop(queue)
        if distance != distances[node]:
            continue
        if node == goal:
            path = []
            while node != start:
                edge = parents[node]
                path.append(edge["id"])
                node = edge["from"]
            return {"cost": distance, "edge_ids": path[::-1]}
        for edge in adjacency[node]:
            value = distance + edge["effective_cost"]
            require(math.isfinite(value), "path cost overflow")
            if value < distances.get(edge["to"], math.inf):
                distances[edge["to"]] = value
                parents[edge["to"]] = edge
                heapq.heappush(queue, (value, edge["to"]))
    return {"cost": None, "edge_ids": []}


def scenarios(graph, config=None):
    config = {} if config is None else config
    keys(config, ("workflows",), "workflows document")
    result, seen = [], set()
    for workflow in array(config.get("workflows", []), "workflows"):
        keys(workflow, ("id", "start", "goal", "via", "frequency", "evidence"), "workflow")
        ident = text_id(workflow.get("id"), "workflow id")
        require(ident not in seen, f"duplicate workflow {ident}")
        seen.add(ident)
        start = text_id(workflow.get("start"), "workflow start")
        goal = text_id(workflow.get("goal"), "workflow goal")
        via = array(workflow.get("via", []), "workflow via")
        for node in via:
            text_id(node, "via node")
        frequency = number(workflow.get("frequency", 1), "workflow frequency")
        evidence = text_id(workflow.get("evidence"), "workflow evidence/assumption")
        waypoints = [start, *via, goal]
        segments = [shortest_path(graph, a, b) for a, b in zip(waypoints, waypoints[1:])]
        reachable = all(segment["cost"] is not None for segment in segments)
        cost = sum(segment["cost"] for segment in segments) if reachable else None
        require(cost is None or math.isfinite(cost), "workflow cost overflow")
        result.append({"id": ident, "start": start, "goal": goal, "via": via,
                       "frequency": frequency, "evidence": evidence,
                       "status": "modeled_reachable" if reachable else "unreachable_in_model",
                       "cost": cost, "segments": segments,
                       "edge_ids": [e for s in segments for e in s["edge_ids"]] if reachable else []})
    return {"schema_version": 1, "workflows": result,
            "note": "Explicit scenarios only; frequency defaults to model weight 1, not observed usage."}


def affinity(registry, workflows, graph):
    """Sparse symmetric co-occurrence; include intermediate commands on reachable routes."""
    known = {item["id"] for item in registry["features"]}
    edge_by_id = {edge["id"]: edge for edge in graph["edges"]}
    pairs = {}
    for workflow in workflows["workflows"]:
        if workflow["cost"] is None:
            continue
        visited = {workflow["start"], *workflow["via"], workflow["goal"]}
        for ident in workflow["edge_ids"]:
            visited.update((edge_by_id[ident]["from"], edge_by_id[ident]["to"]))
        commands = sorted(visited & known)
        for a in commands:
            for b in commands:
                pairs[a, b] = pairs.get((a, b), 0.0) + workflow["frequency"]
                require(math.isfinite(pairs[a, b]), "affinity weight overflow")
    return [{"feature_a": a, "feature_b": b, "modeled_cooccurrence_weight": value}
            for (a, b), value in sorted(pairs.items())]


def constraints(registry, workflows, config=None):
    config = {} if config is None else config
    keys(config, ("required_command_ids", "max_workflow_cost"), "constraints")
    required = array(config.get("required_command_ids", []), "required_command_ids")
    required = [integer(value, "required command id") for value in required]
    limits = object_value(config.get("max_workflow_cost", {}), "max_workflow_cost")
    known = {item["command_id"] for item in registry["features"]}
    by_id = {w["id"]: w for w in workflows["workflows"]}
    checks = [{"kind": "required_command", "id": ident, "status": "pass" if ident in known else "fail"}
              for ident in required]
    normalized = {}
    for ident, value in limits.items():
        require(ident in by_id, f"constraint references unknown workflow {ident!r}")
        limit = number(value, "workflow cost limit")
        normalized[ident] = limit
        actual = by_id[ident]["cost"]
        checks.append({"kind": "max_workflow_cost", "id": ident, "limit": limit, "actual": actual,
                       "status": "unverifiable" if actual is None else "pass" if actual <= limit else "fail"})
    return {"schema_version": 1, "required_command_ids": required, "max_workflow_cost": normalized,
            "checks": checks, "note": "No implicit UX thresholds; pass refers only to explicit model constraints."}


def baseline(registry, graph, workflows, checks, sources):
    features = registry["features"]
    selected = set()
    for workflow in workflows["workflows"]:
        if workflow["cost"] is not None:
            selected.update([workflow["start"], *workflow["via"], workflow["goal"]])
            edge_map = {e["id"]: e for e in graph["edges"]}
            for ident in workflow["edge_ids"]:
                selected.update((edge_map[ident]["from"], edge_map[ident]["to"]))
    covered = sorted(item["id"] for item in features if item["id"] in selected)
    total = len(features)
    return {"schema_version": 1, "sources": sources, "inventory_rows": registry["row_counts"],
            "unique_runtime_command_ids": total,
            "menu_command_ids": sum(bool(f["menus"]) for f in features),
            "search_command_ids": sum(bool(f["search"]) for f in features),
            "hit_command_ids": sum(bool(f["hits"]) for f in features),
            "capture_count": len(registry["captures"]),
            "requested_dpi": sorted({c["requested_dpi"] for c in registry["captures"]}),
            "effective_dpi": sorted({c["effective_dpi"] for c in registry["captures"]}),
            "model_coverage": {"covered_command_ids": covered, "covered": len(covered), "total": total,
                               "fraction": len(covered) / total if total else None,
                               "meaning": "reachable explicit scenarios; not executed or human-tested coverage"},
            "workflow_count": len(workflows["workflows"]),
            "unreachable_workflows": [w["id"] for w in workflows["workflows"] if w["cost"] is None],
            "constraint_failures": sum(c["status"] == "fail" for c in checks["checks"]),
            "constraint_unverifiable": sum(c["status"] == "unverifiable" for c in checks["checks"]),
            "human_evidence": {"status": "missing", "completion_time": None, "error_rate": None,
                               "satisfaction": None}, "limitations": LIMITATIONS}


def report_html(summary, registry, workflows, checks):
    escape = lambda value: html.escape(str(value), quote=True)

    def table(headers, rows):
        return ("<table><thead><tr>" + "".join("<th>" + escape(h) + "</th>" for h in headers)
                + "</tr></thead><tbody>" + "".join("<tr>" + "".join("<td>" + escape(v) + "</td>" for v in row)
                + "</tr>" for row in rows) + "</tbody></table>")

    return ("<!doctype html><html lang='en'><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<meta http-equiv='Content-Security-Policy' content=\"default-src 'none'; style-src 'unsafe-inline'\">"
            "<title>YoungDraw UX evidence audit</title><style>body{font:16px system-ui;margin:2rem;"
            "color:#182635;line-height:1.5}table{border-collapse:collapse;width:100%;margin:1rem 0}"
            "td,th{border:1px solid #bbc;padding:.5rem;text-align:left;overflow-wrap:anywhere}"
            "th{background:#eef}pre{white-space:pre-wrap;overflow-wrap:anywhere}</style>"
            "<h1>YoungDraw UX evidence audit</h1><p>Runtime inventory and modeled access costs. "
            "Human evidence: missing. Model cost units are not measured task time.</p>"
            "<ul>" + "".join("<li>" + escape(note) + "</li>" for note in LIMITATIONS) + "</ul>"
            "<h2>Baseline and provenance</h2><pre>" + escape(json.dumps(summary, ensure_ascii=False, indent=2))
            + "</pre><h2>Explicit workflow models</h2>"
            + table(["Workflow", "Status", "Effective cost", "Evidence / assumption"],
                    ((w["id"], w["status"], w["cost"], w["evidence"]) for w in workflows["workflows"]))
            + "<h2>Constraints</h2>" + table(["Check", "ID", "Status"],
                    ((c["kind"], c["id"], c["status"]) for c in checks["checks"]))
            + "<h2>Observed inventory</h2>" + table(["Command", "Label", "Menu rows", "Search rows", "Hit rows", "Semantics"],
                    ((f["command_id"], f["label"], len(f["menus"]), len(f["search"]), len(f["hits"]),
                      f["semantic_status"]) for f in registry["features"])) + "</html>")


def csv_text(fields, rows):
    stream = io.StringIO(newline="")
    writer = csv.DictWriter(stream, fieldnames=fields)
    writer.writeheader()
    writer.writerows(rows)
    return stream.getvalue()


def audit(evidence, output, graph_path=None, workflows_path=None, constraints_path=None):
    evidence, output = Path(evidence), Path(output)
    paths = [evidence / (kind + ".csv") for kind in HEADERS]
    paths.extend(Path(p) for p in (graph_path, workflows_path, constraints_path) if p is not None)
    # Hash before parsing and confirm unchanged before writing a coherent evidence snapshot.
    sources = [{"file": str(p.resolve()), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in paths]
    registry = inventory(evidence)
    graph = make_graph(registry, read_json(graph_path) if graph_path is not None else None)
    workflows = scenarios(graph, read_json(workflows_path) if workflows_path is not None else None)
    checks = constraints(registry, workflows, read_json(constraints_path) if constraints_path is not None else None)
    summary = baseline(registry, graph, workflows, checks, sources)
    artifacts = {"feature_registry.json": registry, "ui_graph.json": graph,
                 "workflow_scenarios.json": workflows, "ux_baseline.json": summary, "ux_constraints.json": checks}
    rendered = {name: json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n"
                for name, value in artifacts.items()}
    rendered["affinity_matrix.csv"] = csv_text(
        ["feature_a", "feature_b", "modeled_cooccurrence_weight"], affinity(registry, workflows, graph))
    rendered["interaction_cost.csv"] = csv_text(
        ["workflow_id", "status", "effective_cost", "cost_unit", "frequency", "path_edges"],
        ({"workflow_id": w["id"], "status": w["status"], "effective_cost": w["cost"],
          "cost_unit": graph["cost_unit"], "frequency": w["frequency"], "path_edges": json.dumps(w["edge_ids"])}
         for w in workflows["workflows"]))
    rendered["ux_report.html"] = report_html(summary, registry, workflows, checks)
    require(not ({(output / name).resolve() for name in rendered} & {p.resolve() for p in paths}),
            "output would overwrite an input")
    for source, path in zip(sources, paths):
        require(source["sha256"] == hashlib.sha256(path.read_bytes()).hexdigest(), f"input changed during audit: {path}")
    output.mkdir(parents=True, exist_ok=True)
    for name, content in rendered.items():
        (output / name).write_text(content, encoding="utf-8", newline="")
    return summary


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence-dir", type=Path, required=True, help="runtime CSV directory")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--graph", type=Path, help="explicit additional directed graph and cost model JSON")
    parser.add_argument("--workflows", type=Path, help="explicit workflow scenarios JSON")
    parser.add_argument("--constraints", type=Path, help="explicit inventory and model-cost limits JSON")
    parser.add_argument("--fail-on-constraints", action="store_true", help="exit 1 for failed/unverifiable checks")
    args = parser.parse_args(argv)
    try:
        summary = audit(args.evidence_dir, args.output_dir, args.graph, args.workflows, args.constraints)
    except (AuditError, OSError, UnicodeError) as exc:
        print(f"ux-auditor: {exc}", file=sys.stderr)
        return 2
    print(f"Wrote 8 artifacts to {args.output_dir}; {summary['unique_runtime_command_ids']} runtime IDs; "
          f"{summary['workflow_count']} explicit workflow models; human evidence missing.")
    return int(args.fail_on_constraints and bool(summary["constraint_failures"] or summary["constraint_unverifiable"]))


if __name__ == "__main__":
    sys.exit(main())
