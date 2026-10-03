// Minimal reproduction of the article's former mutable Node* priority-key bug.
// Graph: S->A 10, S->B 1, S->G 5, B->A 1, A->G 1. All h=0.
#include <cassert>
#include <iostream>
#include <limits>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>

using Graph = std::vector<std::vector<std::pair<int, int>>>;
const Graph graph = {{{1,10}, {2,1}, {3,5}}, {{3,1}}, {{1,1}}, {}};
const int goal = 3;

int old_mutable_key() {
    struct Node { int id; int g; bool opened; bool closed; };
    std::vector<Node> nodes;
    for (int id=0; id<4; ++id) nodes.push_back({id, 0, false, false});
    auto cmp = [](const Node* a, const Node* b) { return a->g > b->g; };
    std::priority_queue<Node*, std::vector<Node*>, decltype(cmp)> open(cmp);
    nodes[0].opened = true;
    open.push(&nodes[0]);
    while (!open.empty()) {
        Node* n = open.top(); open.pop();
        if (n->closed) continue;
        n->closed = true;
        if (n->id == goal) return n->g;
        for (const auto& edge : graph[n->id]) {
            Node* m = &nodes[edge.first];
            if (m->closed) continue;
            const int ng = n->g + edge.second;
            if (!m->opened || ng < m->g) {
                m->g = ng;  // BUG: queue contains pointers to this mutable key.
                if (!m->opened) { m->opened = true; open.push(m); }
            }
        }
    }
    return -1;
}

int fixed_snapshot_queue() {
    // h=0 here, so f and g are equal. sequence makes ties independent of node id.
    using Entry = std::tuple<int, int, int>; // g snapshot, sequence, id
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    std::vector<int> best(4, std::numeric_limits<int>::max());
    int sequence = 0;
    best[0] = 0; open.push(Entry(0, sequence++, 0));
    while (!open.empty()) {
        const Entry entry = open.top(); open.pop();
        const int g = std::get<0>(entry), id = std::get<2>(entry);
        if (g != best[id]) continue;
        if (id == goal) return g;
        for (const auto& edge : graph[id]) {
            const int ng = g + edge.second;
            if (ng < best[edge.first]) {
                best[edge.first] = ng;
                open.push(Entry(ng, sequence++, edge.first));
            }
        }
    }
    return -1;
}

int main() {
    const int old_cost = old_mutable_key(), fixed_cost = fixed_snapshot_queue();
    assert(old_cost == 5);   // Expected demonstration of a wrong result.
    assert(fixed_cost == 3); // S -> B -> A -> G.
    std::cout << "mutable_queue: old_cost=" << old_cost
              << " optimal=3 fixed_cost=" << fixed_cost << '\n';
}
