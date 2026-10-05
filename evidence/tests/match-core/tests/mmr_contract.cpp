// Focused, NDEBUG-safe admission contract. The historical main/bench are never called.
#ifndef MMR_SOURCE
#define MMR_SOURCE "../src/mmr_pool.cpp"
#endif
#define main mmr_historical_main_not_called
#include MMR_SOURCE
#undef main
#include <climits>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace contract {
int checks = 0, failures = 0, cases = 0, localChecks = 0, localFailures = 0;
std::string current;
void Begin(const std::string& name) { current = name; localChecks = localFailures = 0; ++cases; }
void Require(bool ok) {
    ++checks; ++localChecks;
    if (!ok) { ++failures; ++localFailures; }
    std::cout << "CHECK " << current << ' ' << localChecks << (ok ? " PASS\n" : " FAIL\n");
}
void End() { std::cout << "CASE " << current << " checks=" << localChecks << " fail=" << localFailures << '\n'; }
bool Equal(const Player& a, const Player& b) {
    return a.id == b.id && a.mmr == b.mmr && a.partyId == b.partyId && a.queuedAt == b.queuedAt &&
        a.matched == b.matched && a.matchedAtTick == b.matchedAtTick;
}
bool PopulationEqual(const std::vector<Player>& a, const std::vector<Player>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (!Equal(a[i], b[i])) return false;
    return true;
}
std::string Players(const std::vector<Player>& ps) {
    std::ostringstream s; s << std::setprecision(17) << '[';
    for (size_t i = 0; i < ps.size(); ++i) {
        const auto& p = ps[i]; if (i) s << ',';
        s << '[' << p.id << ',' << p.mmr << ',' << p.partyId << ',' << p.queuedAt << ','
          << (p.matched ? "true" : "false") << ',' << p.matchedAtTick << ']';
    }
    s << ']'; return s.str();
}
std::string Matches(const std::vector<Match>& ms) {
    std::ostringstream s; s << std::setprecision(17) << '[';
    for (size_t i = 0; i < ms.size(); ++i) {
        if (i) s << ',';
        s << "{\"indices\":[";
        for (size_t j = 0; j < ms[i].ids.size(); ++j) { if (j) s << ','; s << ms[i].ids[j]; }
        s << "],\"spread\":" << ms[i].mmrDiff << ",\"relaxed\":" << (ms[i].relaxed ? "true" : "false")
          << ",\"tick\":" << ms[i].tick << '}';
    }
    s << ']'; return s.str();
}
std::vector<Match> Step(Pool& p, int mode, int width = 25) {
    if (mode == 0) return p.StepNaive();
    if (mode == 1) return p.Step(width);
    return p.StepSorted();
}
// Independent oracle: no Compatible, WindowFor, WaitingSorted or TryForm calls.
void Verify(const Pool& before, const Pool& after, const std::vector<Match>& ms,
            int minMatches, int maxMatches, std::vector<int> expectedIds = {}) {
    Require(before.players.size() == after.players.size());
    Require(before.tick == after.tick && before.cfg.teamSize == after.cfg.teamSize &&
        before.cfg.baseWindow == after.cfg.baseWindow && before.cfg.growthPerTick == after.cfg.growthPerTick &&
        before.cfg.maxWindow == after.cfg.maxWindow && before.cfg.scanLimit == after.cfg.scanLimit &&
        before.cfg.maxWaitTicks == after.cfg.maxWaitTicks);
    if (before.players.size() != after.players.size()) {
        for (int n = 0; n < 12; ++n) Require(false);
        return; // Explicit semantic failures, never unsafe indexing or sanitizer-as-oracle.
    }
    bool indices = true, size = true, unique = true, eligible = true, windows = true;
    bool party = true, untouched = true, selectedState = true, immutable = true, diagnostic = true;
    std::map<int, int> used; std::vector<int> selectedIds;
    auto window = [&](const Player& p) { return std::min(before.cfg.maxWindow,
        before.cfg.baseWindow + static_cast<double>(before.tick - p.queuedAt) * before.cfg.growthPerTick); };
    for (size_t n = 0; n < ms.size(); ++n) {
        const Match& m = ms[n];
        size = size && m.ids.size() == static_cast<size_t>(before.cfg.teamSize) * 2;
        bool valid = !m.ids.empty();
        for (int idx : m.ids) valid = valid && idx >= 0 && static_cast<size_t>(idx) < before.players.size();
        indices = indices && valid;
        if (!valid) continue; // Malformed mutants must fail without invoking undefined behavior.
        const auto& anchor = before.players[static_cast<size_t>(m.ids[0])];
        double lo = anchor.mmr, hi = anchor.mmr;
        for (int idx : m.ids) {
            const auto& p = before.players[static_cast<size_t>(idx)];
            unique = unique && used.emplace(idx, static_cast<int>(n)).second;
            selectedIds.push_back(p.id);
            eligible = eligible && !p.matched && p.queuedAt <= before.tick;
            if (p.queuedAt <= before.tick && anchor.queuedAt <= before.tick)
                windows = windows && std::fabs(p.mmr - anchor.mmr) <= std::min(window(p), window(anchor));
            else windows = false;
            lo = std::min(lo, p.mmr); hi = std::max(hi, p.mmr);
        }
        diagnostic = diagnostic && m.tick == before.tick && m.mmrDiff == hi - lo &&
            m.relaxed == (hi - lo > before.cfg.baseWindow);
    }
    for (size_t i = 0; i < before.players.size(); ++i) {
        const auto& p = before.players[i]; const auto& q = after.players[i];
        auto selected = used.find(static_cast<int>(i));
        if (selected == used.end()) untouched = untouched && Equal(p, q);
        else {
            selectedState = selectedState && q.matched && q.matchedAtTick == before.tick;
            if (p.partyId != 0) for (size_t j = 0; j < before.players.size(); ++j) {
                if (before.players[j].partyId != p.partyId) continue;
                auto other = used.find(static_cast<int>(j));
                party = party && other != used.end() && other->second == selected->second;
            }
        }
        immutable = immutable && p.id == q.id && p.mmr == q.mmr && p.partyId == q.partyId && p.queuedAt == q.queuedAt;
    }
    Require(indices); Require(size); Require(unique); Require(eligible); Require(windows); Require(party);
    Require(untouched); Require(selectedState); Require(immutable); Require(diagnostic);
    Require(static_cast<int>(ms.size()) >= minMatches && static_cast<int>(ms.size()) <= maxMatches);
    std::sort(selectedIds.begin(), selectedIds.end()); std::sort(expectedIds.begin(), expectedIds.end());
    Require(expectedIds.empty() || selectedIds == expectedIds);
}
void Run(const std::string& name, Pool& p, int mode, int low, int high, std::vector<int> ids = {}, int width = 25) {
    Begin(name + "." + std::to_string(mode));
    const Pool before = p; const auto ms = Step(p, mode, width);
    std::cout << "OBS {\"case\":\"" << current << "\",\"mode\":" << mode << ",\"tick\":" << before.tick
        << ",\"bucket_width\":" << width << ",\"config\":[" << before.cfg.teamSize << ',' << before.cfg.baseWindow
        << ',' << before.cfg.growthPerTick << ',' << before.cfg.maxWindow << ',' << before.cfg.scanLimit << ',' << before.cfg.maxWaitTicks
        << "],\"before\":" << Players(before.players) << ",\"after\":" << Players(p.players)
        << ",\"after_tick\":" << p.tick << ",\"after_config\":[" << p.cfg.teamSize << ',' << p.cfg.baseWindow
        << ',' << p.cfg.growthPerTick << ',' << p.cfg.maxWindow << ',' << p.cfg.scanLimit << ',' << p.cfg.maxWaitTicks << ']'
        << ",\"matches\":" << Matches(ms) << "}\n";
    Verify(before, p, ms, low, high, ids); End();
}
Pool Make(std::initializer_list<Player> ps, int cap = 200) {
    Pool p; p.cfg.teamSize = 2; p.cfg.scanLimit = cap; p.players = ps; return p;
}
void Matrix() {
    for (int mode = 0; mode < 3; ++mode) {
        Pool p = Make({Mk(101,1000,0),Mk(202,1001,0),Mk(303,1002,0),Mk(404,1003,0)});
        Run("solo",p,mode,1,1,{101,202,303,404});
        Run("repeat",p,mode,0,0);
        p = Make({Mk(101,1000,0,7),Mk(202,1001,0,7),Mk(303,1002,0),Mk(404,1003,0)});
        Run("seed_party",p,mode,1,1,{101,202,303,404});
        p = Make({Mk(101,1000,0),Mk(202,1001,0,7),Mk(303,1002,0,7),Mk(404,1003,0)});
        Run("nonseed_party",p,mode,1,1,{101,202,303,404});
        p = Make({Mk(101,1000,0,7),Mk(202,1001,0,7),Mk(303,1002,0,8),Mk(404,1003,0,8)});
        Run("two_parties",p,mode,1,1);
        p = Make({Mk(101,1000,0),Mk(202,1000,0,7),Mk(303,1001,0),Mk(404,1002,0),Mk(505,1049,0,7)});
        Run("nonseed_remaining",p,mode,1,1);
        p = Make({Mk(101,1000,0,7),Mk(202,1001,0,7),Mk(303,1002,0,7),Mk(404,1003,0,7),Mk(505,1004,0,7)});
        Run("oversized",p,mode,0,0);
        p = Make({Mk(101,1000,0,7),Mk(202,1400,0,7),Mk(303,1002,0),Mk(404,1003,0)});
        Run("party_outside_window",p,mode,0,0);
        p = Make({Mk(101,1000,0),Mk(202,1000,0,7),Mk(303,1001,0),Mk(404,1002,0),Mk(505,1049,0,7)},3);
        Run("candidate_cap",p,mode,0,1);
        p = Make({Mk(101,1000,0,7),Mk(202,1000,0,7),Mk(303,1000,0,7),Mk(404,1000,0)},1);
        Run("cap_below_group",p,mode,0,0);
        p = Make({Mk(101,1000,0,7),Mk(202,1000,0,7),Mk(303,1000,0),Mk(404,1000,0)});
        p.players[1].matched=true; p.players[1].matchedAtTick=0;
        Run("partly_matched",p,mode,0,0);
        p = Make({Mk(101,1000,10),Mk(202,1000,10),Mk(303,1000,10),Mk(404,1000,10)}); p.tick=9;
        Run("future9",p,mode,0,0); p.tick=10;
        Run("future10",p,mode,1,1); p.tick=11;
        Run("future11",p,mode,0,0);
        p = Make({Mk(101,1000,0),Mk(202,1000,10),Mk(303,1000,0),Mk(404,1000,0)}); p.tick=9;
        Run("future_candidate",p,mode,0,0);
        p = Make({Mk(101,1000,0,7),Mk(202,1000,10,7),Mk(303,1000,0),Mk(404,1000,0),Mk(505,1000,0)}); p.tick=9;
        Run("future_party",p,mode,0,0); p.tick=10;
        Run("future_party_boundary",p,mode,1,1);
        p = Make({Mk(101,1000,0),Mk(202,950,0),Mk(303,1050,0),Mk(404,1000,0)});
        Run("anchor_not_allpair",p,mode,1,1,{101,202,303,404});
        p = Make({Mk(101,1000,0),Mk(202,1050,0)}); p.cfg.teamSize=1;
        Run("window_equal",p,mode,1,1);
        p = Make({Mk(101,1000,0),Mk(202,1050.25,0)}); p.cfg.teamSize=1;
        Run("window_outside",p,mode,0,0);
        p = Make({Mk(101,1000,0),Mk(202,1200,100)}); p.cfg.teamSize=1; p.tick=100;
        Run("reciprocal",p,mode,0,0);
        p = Make({Mk(101,3000,0),Mk(202,1000,0)}); p.cfg.teamSize=1; p.tick=300;
        Run("capped_outlier",p,mode,0,0);
        p = Make({Mk(101,1000,0),Mk(202,1000,0),Mk(303,1000,0),Mk(404,1000,0),Mk(505,1000,0)});
        std::reverse(p.players.begin(),p.players.end()); Run("reverse_order",p,mode,1,1,{101,202,303,404});
        p = Make({Mk(101,-1000,0),Mk(202,-1010,0),Mk(303,-990,0),Mk(404,-1001,0)});
        Run("negative_mmr",p,mode,1,1);
        p = Make({Mk(101,static_cast<double>(INT_MAX),0),Mk(202,static_cast<double>(INT_MAX),0)}); p.cfg.teamSize=1;
        Run("bucket_int_max",p,mode,1,1,{},1);
        p = Make({Mk(101,static_cast<double>(INT_MIN),0),Mk(202,static_cast<double>(INT_MIN),0)}); p.cfg.teamSize=1;
        Run("bucket_int_min",p,mode,1,1,{},1);
        p = Make({Mk(101,1000,INT64_MAX),Mk(202,1000,INT64_MAX)}); p.cfg.teamSize=1; p.tick=INT64_MAX;
        Run("tick_int64_max",p,mode,1,1);
        p = Make({Mk(101,1000,0),Mk(202,1000,0)}); p.cfg.teamSize=INT_MAX/2;
        Run("capacity_int_bound",p,mode,0,0);
    }
}
void Helpers() {
    Begin("helper_boundaries");
    Pool p=Make({Mk(101,1000,10),Mk(202,1000,10),Mk(303,1000,10),Mk(404,1000,10)}); p.tick=9;
    Require(p.WindowFor(p.players[0])==50);
    Require(!p.Compatible(p.players[0],p.players[1]));
    std::vector<int> cand{1,2,3}; std::vector<Match> ms; const auto old=p.players;
    const bool formed=p.TryForm(0,cand,&ms); Require(!formed && ms.empty() && PopulationEqual(old,p.players));
    p=Make({Mk(101,1e20,0),Mk(202,1e20,0)}); p.cfg.teamSize=1;
    const auto large=p.StepSorted(); Require(large.size()==1 && large[0].mmrDiff==0);
    p=Make({Mk(101,-1e20,0),Mk(202,-1e20,0)}); p.cfg.teamSize=1;
    const auto small=p.StepNaive(); Require(small.size()==1 && small[0].mmrDiff==0);
    p=Make({Mk(101,1000,0)}); p.tick=INT64_MAX; Require(p.WindowFor(p.players[0])==600);
    End();
    Begin("rng_population");
    const Pool pop=ReplayPopulation(12345,4);
    std::cout << "POP {\"case\":\"rng_population\",\"players\":" << Players(pop.players) << "}\n";
    Require(pop.players.size()==4);
    Require(pop.players[0].mmr==1280 && pop.players[0].queuedAt==1);
    const Pool repeat=ReplayPopulation(12345,4); Require(PopulationEqual(pop.players,repeat.players));
    const Pool other=ReplayPopulation(999,4); Require(!PopulationEqual(pop.players,other.players)); End();
    Begin("benchmark_inputs");
    auto inputs=BenchmarkInputs(12,10); const auto original=inputs[0].players;
    std::cout << "POP {\"case\":\"benchmark_inputs\",\"players\":" << Players(original) << "}\n";
    Require(PopulationEqual(inputs[0].players,inputs[1].players) && PopulationEqual(inputs[0].players,inputs[2].players));
    Require(inputs[0].tick==10 && inputs[1].tick==10 && inputs[2].tick==10);
    inputs[0].players[0].matched=true;
    Require(PopulationEqual(original,inputs[1].players) && PopulationEqual(original,inputs[2].players));
    Require(original.size()==12); End();
}
void Cohorts() {
    Begin("cohort_accounting"); Pool p=QueuePopulation(); const auto before=p.players;
    for(int64_t t=0;t<=200;++t) { p.tick=t; const auto ignored=p.Step(); (void)ignored; }
    const auto normal=QueueCohort(p.players,false), extreme=QueueCohort(p.players,true);
    std::cout << "COHORT {\"before\":" << Players(before) << ",\"after\":" << Players(p.players) << ",\"reported\":[";
    for(int c=0;c<2;++c) { const auto& s=c?extreme:normal; if(c)std::cout<<',';
        std::cout<<'['<<s.total<<','<<s.matched<<','<<s.unmatched<<",[";
        for(size_t i=0;i<s.waits.size();++i){if(i)std::cout<<',';std::cout<<s.waits[i];}std::cout<<"]]"; }
    std::cout << "]}\n";
    std::array<CohortStats,2> expected;
    bool state=before.size()==p.players.size();
    for(size_t i=0;i<std::min(before.size(),p.players.size());++i){
        const auto& a=before[i]; const auto& b=p.players[i]; auto& c=expected[i<1200?0:1]; ++c.total;
        state=state && a.id==b.id && a.mmr==b.mmr && a.queuedAt==b.queuedAt && a.partyId==b.partyId;
        if(b.matched){++c.matched; if(b.matchedAtTick<b.queuedAt || b.matchedAtTick>200)state=false; else c.waits.push_back(b.matchedAtTick-b.queuedAt);}
        else ++c.unmatched;
    }
    for(auto& c:expected)std::sort(c.waits.begin(),c.waits.end());
    Require(state && before.size()==1220); Require(CohortAccounting(normal,extreme));
    Require(normal.total==expected[0].total && normal.matched==expected[0].matched && normal.unmatched==expected[0].unmatched && normal.waits==expected[0].waits);
    Require(extreme.total==expected[1].total && extreme.matched==expected[1].matched && extreme.unmatched==expected[1].unmatched && extreme.waits==expected[1].waits);
    Require(!CohortAccounting(extreme,normal));
    auto corrupt=normal; corrupt.validTimes=false; Require(!CohortAccounting(corrupt,extreme));
    corrupt=normal; ++corrupt.unmatched; Require(!CohortAccounting(corrupt,extreme));
    corrupt=normal; corrupt.waits.clear(); Require(!CohortAccounting(corrupt,extreme)); End();
}
} // namespace contract
int main(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--retained") {
        TestWindowGrowth(); TestFairnessIntersection(); TestPartyIntegrity(); TestDeterminism();
        TestTimeoutAndRelaxation(); TestQueueTimeDistribution(); TestFairnessVsBaseline(); TestIndexedMatchesLinear();
        std::printf("RESULT pass=%d fail=%d\n",g_pass,g_fail); return g_fail?1:0;
    }
    if(argc!=1)return 2;
    std::cout << std::setprecision(17) << "MMR_CONTRACT version=1\n";
    contract::Matrix(); contract::Helpers(); contract::Cohorts();
    std::cout << "RESULT version=1 cases=" << contract::cases << " checks=" << contract::checks << " fail=" << contract::failures << '\n';
    return contract::failures?1:0;
}
