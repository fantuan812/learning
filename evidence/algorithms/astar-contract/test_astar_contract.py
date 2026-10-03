#!/usr/bin/env python3
"""Run from any directory. No third-party dependencies, no repository writes.

A C++11 compiler (g++) is mandatory: absence is a failure, never a silent skip.
Build products live in TemporaryDirectory. Article snippets are tested directly.
"""
import argparse
import heapq
import itertools
import math
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile
import unittest
from dataclasses import dataclass

from astar_contract import (astar, assert_heuristic, bellman_ford,
                            CONSISTENT_GRAPH, CONSISTENT_H,
                            INCONSISTENT_GRAPH, INCONSISTENT_H, DISCOVERY_GRAPH)

ROOT = Path(__file__).resolve().parents[3]
ARTICLE = ROOT / "游戏算法/01-寻路与图论/02-A星算法与优化.md"
TEXT = ARTICLE.read_text(encoding="utf-8")
MUTANT = None  # Optional CLI injection changes extracted strings, never source files.
HEURISTIC_GRID = [[1,1,1,1,1], [1,1,0,1,1], [1,1,1,0,1],
                  [1,1,1,1,1], [1,1,1,1,1]]
HEURISTIC_START, HEURISTIC_GOAL = (1, 0), (4, 4)


def snippet(heading, language):
    section = TEXT.split(heading, 1)[1].split("\n### ", 1)[0]
    code = re.search(r"```" + language + r"\n(.*?)\n```", section, re.S).group(1)
    replacements = {
        "python-manhattan": ("python", "return max(dx, dy) + (diagonal - 1.0) * min(dx, dy)",
                             "return dx + dy"),
        "cpp-manhattan": ("cpp", "return std::max(dx, dy) + (diagonal - 1.0) * std::min(dx, dy);",
                          "return static_cast<double>(dx + dy);"),
    }
    if MUTANT in replacements:
        target_language, old, new = replacements[MUTANT]
        if language == target_language:
            if code.count(old) != 1:
                raise RuntimeError("mutation target changed; no regression verdict available")
            code = code.replace(old, new)
    return code


def grid_graph(grid):
    # Independent enumerated adjacency: includes only legal non-corner-cutting edges.
    nodes = {(x, y) for y, row in enumerate(grid) for x, cell in enumerate(row) if cell}
    graph = {node: [] for node in sorted(nodes)}
    for x, y in graph:
        for nx, ny in sorted(nodes):
            dx, dy = abs(nx - x), abs(ny - y)
            if max(dx, dy) != 1:
                continue
            if dx and dy and ((nx, y) not in nodes or (x, ny) not in nodes):
                continue
            graph[x, y].append(((nx, ny), math.sqrt(2) if dx and dy else 1))
    return graph


def grid_cases():
    # All 512 obstacle masks and every ordered pair of walkable endpoints.
    cases = []
    for mask in range(512):
        grid = [[int(bool(mask & (1 << (y * 3 + x)))) for x in range(3)] for y in range(3)]
        graph = grid_graph(grid)
        for start, goal in itertools.product(graph, repeat=2):
            cases.append((grid, start, goal, graph, bellman_ford(graph, start, goal)))
    return cases


def path_cost(test, path, start, goal, graph):
    if not path:
        return math.inf
    test.assertEqual(path[0], start)
    test.assertEqual(path[-1], goal)
    test.assertEqual(len(path), len(set(path)), "unexpected parent cycle")
    total = 0
    for a, b in zip(path, path[1:]):
        edges = dict(graph[a])
        test.assertIn(b, edges, "path crosses an obstacle/corner or skips an edge")
        total += edges[b]
    return total


class ContractTests(unittest.TestCase):
    def test_01_stale_pops_are_not_expansions(self):
        assert_heuristic(CONSISTENT_GRAPH, "G", CONSISTENT_H, consistent=True)
        good = astar(CONSISTENT_GRAPH, "S", "G", CONSISTENT_H)
        extra = astar(CONSISTENT_GRAPH, "S", "G", CONSISTENT_H, skip_stale=False)
        self.assertEqual((good.cost, good.pops, good.stale, good.expanded["A"]), (22, 5, 1, 1))
        self.assertEqual(extra.expanded["A"], 2)
        self.assertEqual(good.cost, bellman_ford(CONSISTENT_GRAPH, "S", "G"))
        self.assertEqual(sum(good.expanded.values()), 3)

    def test_02_mutating_a_key_does_not_repair_heap(self):
        class MutableKey:
            def __init__(self, name, value):
                self.name, self.value = name, value
            def __lt__(self, other):
                return self.value < other.value
        a, b, goal = MutableKey("A", 10), MutableKey("B", 1), MutableKey("G", 5)
        heap = []
        for node in (a, b, goal):
            heapq.heappush(heap, node)
        self.assertEqual(heapq.heappop(heap).name, "B")
        a.value = 2  # B -> A relaxation without decrease-key/reinsert snapshots.
        wrong = heapq.heappop(heap)
        self.assertEqual((wrong.name, wrong.value), ("G", 5))
        graph = {"S": [("A", 10), ("B", 1), ("G", 5)],
                 "B": [("A", 1)], "A": [("G", 1)], "G": []}
        good = astar(graph, "S", "G", dict.fromkeys(graph, 0))
        self.assertEqual(good.cost, bellman_ford(graph, "S", "G"))
        self.assertEqual(good.cost, 3)

    def test_03_admissible_inconsistent_requires_reopen(self):
        assert_heuristic(INCONSISTENT_GRAPH, "G", INCONSISTENT_H, consistent=False)
        no = astar(INCONSISTENT_GRAPH, "S", "G", INCONSISTENT_H, reopen=False)
        yes = astar(INCONSISTENT_GRAPH, "S", "G", INCONSISTENT_H)
        self.assertEqual((no.cost, yes.cost, yes.expanded["A"]), (5, 4, 2))
        self.assertEqual(yes.path, ["S", "B", "A", "G"])
        self.assertEqual(yes.cost, bellman_ford(INCONSISTENT_GRAPH, "S", "G"))

    def test_04_goal_discovery_is_too_early(self):
        h = dict.fromkeys(DISCOVERY_GRAPH, 0)
        early = astar(DISCOVERY_GRAPH, "S", "G", h, stop_on_discovery=True)
        late = astar(DISCOVERY_GRAPH, "S", "G", h)
        self.assertEqual((early.cost, late.cost), (10, 2))

    def test_05_equal_priorities_do_not_compare_node_objects(self):
        @dataclass(frozen=True)
        class Node:
            name: str
        s, a, b, g = map(Node, "SABG")
        # Demonstrate why (priority, node) is not a general representation.
        with self.assertRaises(TypeError):
            heapq.heapify([(1, a), (1, b)])
        graph = {s: [(a, 1), (b, 1)], a: [(g, 1)], b: [(g, 1)], g: []}
        result = astar(graph, s, g, dict.fromkeys(graph, 0))
        self.assertEqual(result.path, [s, a, g])
        self.assertEqual(result.order, [s, a, b])
        self.assertEqual(result.pops, 4)  # Equal-cost B -> G does not reinsert G.

    def test_06_equal_f_policy_changes_work_not_cost(self):
        graph = {"S": [("A", 1), ("B", 1)], "A": [("G", 1)],
                 "B": [("G", 1)], "G": []}
        h = {"S": 2, "A": 1, "B": 1, "G": 0}
        fifo = astar(graph, "S", "G", h)
        larger = astar(graph, "S", "G", h, tie="larger_g")
        self.assertEqual((fifo.cost, larger.cost), (2, 2))
        self.assertEqual(fifo.order, ["S", "A", "B"])
        self.assertEqual(larger.order, ["S", "A"])

    def test_07_strict_relaxation_stops_zero_cost_cycle(self):
        graph = {"S": [("A", 0)], "A": [("B", 0)],
                 "B": [("A", 0), ("G", 1)], "G": []}
        result = astar(graph, "S", "G", dict.fromkeys(graph, 0))
        self.assertEqual((result.cost, result.pops), (1, 4))
        self.assertEqual(result.path, ["S", "A", "B", "G"])

    def test_08_graph_boundaries(self):
        graph = {"S": [], "G": []}
        result = astar(graph, "S", "G", dict.fromkeys(graph, 0))
        self.assertEqual((result.cost, result.path), (math.inf, []))
        result = astar(graph, "S", "S", dict.fromkeys(graph, 0))
        self.assertEqual((result.cost, result.path, result.pops), (0, ["S"], 1))
        self.assertFalse(result.expanded)
        graph = {None: [("G", 1)], "G": []}
        result = astar(graph, None, "G", dict.fromkeys(graph, 0))
        self.assertEqual(result.path, [None, "G"])

    def test_09_fixed_seed_graphs_match_independent_oracle(self):
        rng = random.Random(20261003)
        for case in range(200):
            graph = {n: [] for n in range(6)}
            for a in graph:
                for b in graph:
                    if a != b and (b == a + 1 or rng.random() < 0.3):
                        graph[a].append((b, rng.randrange(5)))
            distance = {n: bellman_ford(graph, n, 5) for n in graph}
            h = {n: rng.randrange(int(d) + 1) for n, d in distance.items()}
            result = astar(graph, 0, 5, h)
            with self.subTest(case=case):
                self.assertEqual(result.cost, distance[0])
                self.assertEqual(path_cost(self, result.path, 0, 5, graph), distance[0])

    def test_12_cpp_old_failure_and_snapshot_fix(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ missing: mutable-key regression was NOT validated")
        with tempfile.TemporaryDirectory(prefix="astar-mutable-queue-") as directory:
            executable = Path(directory) / "regression"
            source = Path(__file__).with_name("mutable_queue_regression.cpp")
            build = subprocess.run([compiler, "-std=c++11", "-O2", "-Wall", "-Wextra",
                                    "-Werror", str(source), "-o", str(executable)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, "mutable_queue: old_cost=5 optimal=3 fixed_cost=3\n")
            print(run.stdout.strip())


class ArticleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        namespace = {}
        exec(compile(snippet("### 3.5 Python", "python"), str(ARTICLE), "exec"), namespace)
        cls.search = staticmethod(namespace["a_star"])
        cls.cases = grid_cases()
        assert len(cls.cases) == 11520

    def test_10_python_article_boundaries_and_all_small_grids(self):
        for grid, start, goal in (([], (0,0), (0,0)), ([[]], (0,0), (0,0)),
                                  ([[1], [1,1]], (0,0), (0,1)), ([[0]], (0,0), (0,0)),
                                  ([[1]], (-1,0), (0,0)), ([[1]], (0,0), (1,0))):
            self.assertIsNone(self.search(grid, start, goal))
        for grid, start, goal, graph, expected in self.cases:
            path = self.search(grid, start, goal)
            self.assertAlmostEqual(path_cost(self, path, start, goal, graph), expected)

    def run_cpp_cases(self, cases):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ missing: C++ snippet was NOT validated")
        driver = r"""
#include <cassert>
#include <iostream>
int main() {
    using P = std::pair<int, int>;
    assert(aStar({}, P(0,0), P(0,0)).empty());
    assert(aStar({{}}, P(0,0), P(0,0)).empty());
    assert(aStar({{1},{1,1}}, P(0,0), P(0,1)).empty());
    assert(aStar({{0}}, P(0,0), P(0,0)).empty());
    assert(aStar({{1}}, P(-1,0), P(0,0)).empty());
    assert(aStar({{1}}, P(0,0), P(1,0)).empty());
    int height, width, sx, sy, gx, gy;
    while (std::cin >> height >> width >> sx >> sy >> gx >> gy) {
        std::vector<std::vector<int>> grid(height, std::vector<int>(width));
        for (auto& row : grid) for (int& cell : row) std::cin >> cell;
        auto path = aStar(grid, P(sx,sy), P(gx,gy));
        std::cout << path.size();
        for (const auto& point : path) std::cout << ' ' << point.first << ' ' << point.second;
        std::cout << '\n';
    }
}
"""
        inputs = []
        for grid, start, goal, _, _ in cases:
            cells = [cell for row in grid for cell in row]
            inputs.append(" ".join(map(str, (len(grid), len(grid[0]), *start, *goal, *cells))))
        with tempfile.TemporaryDirectory(prefix="astar-contract-") as directory:
            source, executable = Path(directory) / "article.cpp", Path(directory) / "article"
            source.write_text(snippet("### 3.4 C++", "cpp") + driver, encoding="utf-8")
            build = subprocess.run([compiler, "-std=c++11", "-O2", "-Wall", "-Wextra",
                                    "-Werror", str(source), "-o", str(executable)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], input="\n".join(inputs) + "\n",
                                 capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        lines = run.stdout.splitlines()
        self.assertEqual(len(lines), len(cases))
        costs = []
        for line, (_, start, goal, graph, _) in zip(lines, cases):
            values = list(map(int, line.split()))
            self.assertEqual(len(values), 1 + 2 * values[0])
            path = list(zip(values[1::2], values[2::2]))
            costs.append(path_cost(self, path, start, goal, graph))
        return costs

    def test_11_cpp_article_boundaries_and_all_small_grids(self):
        costs = self.run_cpp_cases(self.cases)
        for cost, (_, _, _, _, expected) in zip(costs, self.cases):
            self.assertAlmostEqual(cost, expected)

    def heuristic_case(self):
        graph = grid_graph(HEURISTIC_GRID)
        oracle = bellman_ford(graph, HEURISTIC_START, HEURISTIC_GOAL)
        # Assert the oracle against an analytically known 3 + 2*sqrt(2) route too.
        self.assertAlmostEqual(oracle, 3 + 2 * math.sqrt(2))
        return HEURISTIC_GRID, HEURISTIC_START, HEURISTIC_GOAL, graph, oracle

    def test_13_python_octile_regression_matches_bellman_ford(self):
        grid, start, goal, graph, oracle = self.heuristic_case()
        cost = path_cost(self, self.search(grid, start, goal), start, goal, graph)
        print(f"heuristic_regression[python]: cost={cost} oracle={oracle} mutant={MUTANT}")
        self.assertAlmostEqual(cost, oracle, msg="8-way heuristic regression: Python")

    def test_14_cpp_octile_regression_matches_bellman_ford(self):
        case = self.heuristic_case()
        cost = self.run_cpp_cases([case])[0]
        print(f"heuristic_regression[cpp]: cost={cost} oracle={case[4]} mutant={MUTANT}")
        self.assertAlmostEqual(cost, case[4], msg="8-way heuristic regression: C++")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutant", choices=("python-manhattan", "cpp-manhattan"),
                        help="in-memory negative control; the full suite MUST fail")
    MUTANT = parser.parse_args().mutant
    suite = unittest.defaultTestLoader.loadTestsFromModule(__import__(__name__))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if result.wasSuccessful():
        print("PASS: 14 tests; 200 graph cases; 11520 small-grid + 1 heuristic-regression queries per language; C++11 -Werror")
    raise SystemExit(not result.wasSuccessful())
