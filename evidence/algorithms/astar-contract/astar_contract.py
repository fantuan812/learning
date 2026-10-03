"""Small deterministic A* contract laboratory; Python standard library only.

The switches intentionally expose broken policies for counterexamples. Production
policy: reopen=True, skip_stale=True, stop_on_discovery=False, tie='fifo'.
Graph keys are arbitrary hashable objects; edge costs are finite nonnegative
integers in this laboratory. h is fixed during one search, admissible, h(goal)=0.
"""
from collections import Counter
from dataclasses import dataclass
from heapq import heappop, heappush
from itertools import count
from math import inf


@dataclass
class Result:
    cost: object
    path: list
    pops: int
    stale: int
    expanded: Counter
    order: list


def astar(graph, start, goal, h, *, reopen=True, skip_stale=True,
          stop_on_discovery=False, tie="fifo"):
    if tie not in ("fifo", "larger_g"):
        raise ValueError("unknown tie policy")
    serial = count()
    best, parent, expanded = {start: 0}, {}, Counter()
    heap, order = [], []
    pops = stale = 0

    def push(node, g):
        # Immutable value snapshots; unique serial prevents comparing node objects.
        secondary = -g if tie == "larger_g" else 0
        heappush(heap, (g + h[node], secondary, next(serial), g, node))

    missing = object()

    def result(cost, node=missing):
        path = []
        if node is not missing:
            while True:
                path.append(node)
                if node == start:
                    break
                node = parent[node]
        return Result(cost, path[::-1], pops, stale, expanded, order)

    push(start, 0)
    while heap:
        _, _, _, queued_g, node = heappop(heap)
        pops += 1
        if queued_g != best[node]:
            stale += 1
            if skip_stale:
                continue
        if node == goal:
            return result(best[node], node)
        expanded[node] += 1
        order.append(node)
        for neighbor, weight in graph.get(node, ()):
            if not reopen and expanded[neighbor]:
                continue
            candidate = best[node] + weight
            if candidate < best.get(neighbor, inf):
                best[neighbor], parent[neighbor] = candidate, node
                push(neighbor, candidate)  # Also push improvements to OPEN/CLOSED.
                if stop_on_discovery and neighbor == goal:
                    return result(candidate, neighbor)
    return result(inf)


def bellman_ford(graph, start, goal):
    """Independent cost oracle; no heap and no heuristic."""
    nodes = set(graph) | {start, goal}
    nodes.update(v for edges in graph.values() for v, _ in edges)
    best = {node: inf for node in nodes}
    best[start] = 0
    for _ in range(len(nodes) - 1):
        changed = False
        for node, edges in graph.items():
            for neighbor, weight in edges:
                if best[node] + weight < best[neighbor]:
                    best[neighbor] = best[node] + weight
                    changed = True
        if not changed:
            break
    return best[goal]


def assert_heuristic(graph, goal, h, *, consistent):
    assert h[goal] == 0
    assert all(0 <= h[n] <= bellman_ford(graph, n, goal) for n in graph)
    actual = all(h[n] <= w + h[m] for n, es in graph.items() for m, w in es)
    assert actual == consistent


CONSISTENT_GRAPH = {"S": [("A", 10), ("B", 1)], "B": [("A", 1)],
                    "A": [("G", 20)], "G": []}
CONSISTENT_H = {"S": 2, "B": 1, "A": 0, "G": 0}
INCONSISTENT_GRAPH = {"S": [("A", 3), ("B", 1)], "B": [("A", 1)],
                      "A": [("G", 2)], "G": []}
INCONSISTENT_H = {"S": 4, "B": 3, "A": 0, "G": 0}
DISCOVERY_GRAPH = {"S": [("G", 10), ("A", 1)], "A": [("G", 1)], "G": []}


if __name__ == "__main__":
    good = astar(CONSISTENT_GRAPH, "S", "G", CONSISTENT_H)
    duplicate = astar(CONSISTENT_GRAPH, "S", "G", CONSISTENT_H, skip_stale=False)
    print(f"stale: cost={good.cost} pops={good.pops} stale={good.stale} "
          f"A_expansions={good.expanded['A']} without_filter={duplicate.expanded['A']}")
    no = astar(INCONSISTENT_GRAPH, "S", "G", INCONSISTENT_H, reopen=False)
    yes = astar(INCONSISTENT_GRAPH, "S", "G", INCONSISTENT_H)
    print(f"reopen: disabled={no.cost} enabled={yes.cost} A_expansions={yes.expanded['A']}")
    h = dict.fromkeys(DISCOVERY_GRAPH, 0)
    early = astar(DISCOVERY_GRAPH, "S", "G", h, stop_on_discovery=True)
    late = astar(DISCOVERY_GRAPH, "S", "G", h)
    print(f"goal: on_discovery={early.cost} on_valid_pop={late.cost}")
    graph = {"S": [("A", 1), ("B", 1)], "A": [("G", 1)],
             "B": [("G", 1)], "G": []}
    h = {"S": 2, "A": 1, "B": 1, "G": 0}
    fifo = astar(graph, "S", "G", h)
    larger = astar(graph, "S", "G", h, tie="larger_g")
    print(f"ties: fifo_cost={fifo.cost} larger_g_cost={larger.cost} "
          f"fifo_expansions={sum(fifo.expanded.values())} "
          f"larger_g_expansions={sum(larger.expanded.values())}")
