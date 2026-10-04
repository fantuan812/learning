// Original contract/measurement laboratory, 2026-10-04. No UE or production claim.
// Historical Windows results are preserved separately; never infer speed from tests.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <list>
#include <locale>
#include <queue>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

using Clock = std::chrono::steady_clock;
constexpr double INF = std::numeric_limits<double>::infinity();
constexpr double DIAGONAL = 1.4142135623730950488;
struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed ? seed : 1) {}
    std::uint64_t next() { state ^= state >> 12; state ^= state << 25;
        state ^= state >> 27; return state * UINT64_C(2685821657736338717); }
    int bounded(int n) { return static_cast<int>(next() % static_cast<unsigned>(n)); }
}; // Fixed fixture generator; modulo reduction, not a cryptographic/randomness claim.
struct Grid {
    int width, height;
    bool cornerCutting = false;
    std::vector<unsigned char> blocked;
    Grid(int w, int h) : width(w), height(h), blocked(static_cast<std::size_t>(w*h), 0) {
        if (w <= 0 || h <= 0) throw std::invalid_argument("grid dimensions");
    }
    bool free(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height
        && blocked[static_cast<std::size_t>(y*width+x)] == 0; }
    int id(int x, int y) const { return y*width+x; }
    int size() const { return width*height; }
};
struct Query { int sx, sy, gx, gy; bool scheduledRepeat = false; };
enum class Status { Found, Unreachable, Invalid };
struct Result { Status status = Status::Unreachable; std::vector<int> path;
    double cost = INF; std::uint64_t pops = 0, stale = 0, expanded = 0; };
bool validEndpoints(const Grid& g, const Query& q) {
    return g.free(q.sx,q.sy) && g.free(q.gx,q.gy);
}
double heuristic(int x, int y, const Query& q) {
    const int dx=std::abs(x-q.gx), dy=std::abs(y-q.gy);
    return static_cast<double>(std::max(dx,dy))+(DIAGONAL-1.0)*std::min(dx,dy);
}
struct Entry { double f, queuedG; int node; std::uint64_t serial; };
bool better(const Entry& a, const Entry& b) {
    if (a.f != b.f) return a.f < b.f;
    if (a.queuedG != b.queuedG) return a.queuedG > b.queuedG;
    return a.serial < b.serial;
}
struct HeapLater { bool operator()(const Entry& a,const Entry& b) const { return better(b,a); } };
Result search(const Grid& grid, const Query& q, bool useHeap) {
    Result out;
    if (!validEndpoints(grid,q)) { out.status=Status::Invalid; return out; }
    const int start=grid.id(q.sx,q.sy), goal=grid.id(q.gx,q.gy);
    std::vector<double> best(static_cast<std::size_t>(grid.size()),INF);
    std::vector<int> parent(static_cast<std::size_t>(grid.size()),-1);
    std::priority_queue<Entry,std::vector<Entry>,HeapLater> heap;
    std::vector<Entry> linear;
    std::uint64_t serial=0;
    auto push=[&](int n,double cost) {
        Entry e{cost+heuristic(n%grid.width,n/grid.width,q),cost,n,serial++};
        if (useHeap) heap.push(e); else linear.push_back(e);
    };
    best[static_cast<std::size_t>(start)]=0; push(start,0);
    while (useHeap ? !heap.empty() : !linear.empty()) {
        Entry e{};
        if (useHeap) { e=heap.top(); heap.pop(); }
        else { const auto it=std::min_element(linear.begin(),linear.end(),better);
            e=*it; *it=linear.back(); linear.pop_back(); }
        ++out.pops;
        if (e.queuedG != best[static_cast<std::size_t>(e.node)]) { ++out.stale; continue; }
        if (e.node==goal) {
            out.status=Status::Found; out.cost=e.queuedG;
            for (int n=goal;n!=-1;n=parent[static_cast<std::size_t>(n)]) out.path.push_back(n);
            std::reverse(out.path.begin(),out.path.end()); return out;
        }
        ++out.expanded; // Valid neighbor-expanding pop only; goal/stale not counted.
        const int x=e.node%grid.width,y=e.node/grid.width;
        for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
            if (dx==0 && dy==0) continue;
            const int nx=x+dx,ny=y+dy;
            if (!grid.free(nx,ny)) continue;
            const bool diagonal=dx!=0 && dy!=0;
            if (diagonal && !grid.cornerCutting && (!grid.free(x+dx,y)||!grid.free(x,y+dy))) continue;
            const int n=grid.id(nx,ny); const double cost=e.queuedG+(diagonal?DIAGONAL:1.0);
            if (cost < best[static_cast<std::size_t>(n)]) {
                best[static_cast<std::size_t>(n)]=cost; parent[static_cast<std::size_t>(n)]=e.node; push(n,cost);
            } // Reinsert immutable snapshot, including any improved previously expanded node.
        }
    }
    return out;
}
// Independent Bellman-Ford oracle: enumerate vertex pairs; no search neighbor helper,
// heuristic, OPEN queue, parent array, or permanent CLOSED set is reused.
double oracle(const Grid& g,const Query& q) {
    if (!validEndpoints(g,q)) return INF;
    std::vector<double> d(static_cast<std::size_t>(g.size()),INF);
    d[static_cast<std::size_t>(g.id(q.sx,q.sy))]=0;
    struct Edge { int a,b; double w; }; std::vector<Edge> edges;
    for (int a=0;a<g.size();++a) for (int b=0;b<g.size();++b) {
        const int ax=a%g.width,ay=a/g.width,bx=b%g.width,by=b/g.width;
        const int dx=std::abs(ax-bx),dy=std::abs(ay-by);
        if (!g.free(ax,ay)||!g.free(bx,by)||dx>1||dy>1||dx+dy==0) continue;
        if (dx==1 && dy==1 && !g.cornerCutting && (!g.free(ax,by)||!g.free(bx,ay))) continue;
        edges.push_back({a,b,dx+dy==2?DIAGONAL:1.0});
    }
    for (int pass=1;pass<g.size();++pass) {
        bool changed=false;
        for (const auto& e:edges) if (d[static_cast<std::size_t>(e.a)]+e.w < d[static_cast<std::size_t>(e.b)]) {
            d[static_cast<std::size_t>(e.b)]=d[static_cast<std::size_t>(e.a)]+e.w; changed=true;
        }
        if (!changed) break;
    }
    return d[static_cast<std::size_t>(g.id(q.gx,q.gy))];
}
bool near(double a,double b) { return std::isfinite(a)&&std::isfinite(b)
    && std::abs(a-b)<=1e-9*std::max(1.0,std::max(std::abs(a),std::abs(b))); }
bool pathValid(const Grid& g,const Query& q,const Result& r) {
    if (r.status==Status::Invalid) return !validEndpoints(g,q)&&r.path.empty();
    if (r.status==Status::Unreachable) return validEndpoints(g,q)&&r.path.empty()&&std::isinf(r.cost);
    if (!validEndpoints(g,q)||r.path.empty()||r.path.front()!=g.id(q.sx,q.sy)
        ||r.path.back()!=g.id(q.gx,q.gy)) return false;
    std::vector<bool> seen(static_cast<std::size_t>(g.size()),false); double cost=0;
    for (std::size_t i=0;i<r.path.size();++i) {
        const int n=r.path[i]; if (n<0||n>=g.size()) return false;
        const int x=n%g.width,y=n/g.width;
        if (!g.free(x,y)||seen[static_cast<std::size_t>(n)]) return false;
        seen[static_cast<std::size_t>(n)]=true;
        if (i==0) continue;
        const int previous=r.path[i-1],px=previous%g.width,py=previous/g.width;
        const int dx=std::abs(x-px),dy=std::abs(y-py);
        if (dx>1||dy>1||dx+dy==0) return false;
        if (dx==1&&dy==1) {
            if (!g.cornerCutting&&(!g.free(px,y)||!g.free(x,py))) return false;
            cost+=DIAGONAL;
        } else cost+=1.0;
    }
    return near(cost,r.cost);
}
bool agrees(const Result& r,double expected) {
    return std::isinf(expected) ? r.status==Status::Unreachable : r.status==Status::Found&&near(r.cost,expected);
}
struct Key {
    int start,goal,profile; std::uint64_t epoch;
    bool operator==(const Key& k) const { return start==k.start&&goal==k.goal&&profile==k.profile&&epoch==k.epoch; }
};
struct KeyHash { std::size_t operator()(const Key& k) const {
    std::size_t h=static_cast<std::size_t>(k.epoch);
    for (int v:{k.start,k.goal,k.profile}) h^=static_cast<std::size_t>(v)+0x9e3779b9U+(h<<6)+(h>>2);
    return h;
} };
class PathCache {
    struct Item { Result value; std::list<Key>::iterator where; };
    std::size_t capacity_; std::uint64_t epoch_; bool noTouch_;
    std::list<Key> recency_; // Most recently used at front. One node per map key.
    std::unordered_map<Key,Item,KeyHash> values_;
public:
    PathCache(std::size_t capacity,std::uint64_t epoch,bool noTouch=false)
        : capacity_(capacity),epoch_(epoch),noTouch_(noTouch) {}
    // Item iterators belong to this recency_ list. Default copying would retain
    // iterators into another cache; relocation is also deliberately unsupported.
    PathCache(const PathCache&) = delete;
    PathCache& operator=(const PathCache&) = delete;
    PathCache(PathCache&&) = delete;
    PathCache& operator=(PathCache&&) = delete;
    void setEpoch(std::uint64_t epoch) { if (epoch!=epoch_) { values_.clear();recency_.clear();epoch_=epoch; } }
    std::size_t size() const { return values_.size(); }
    const Result* lookup(const Key& key) {
        if (key.epoch!=epoch_) return nullptr;
        auto it=values_.find(key); if (it==values_.end()) return nullptr;
        if (!noTouch_) recency_.splice(recency_.begin(),recency_,it->second.where);
        return &it->second.value; // Borrowed until next mutation; never retained by benchmark.
    }
    bool get(const Key& key,Result& out) { const auto* p=lookup(key); if (!p) return false;out=*p;return true; }
    void put(const Key& key,const Result& value) {
        if (capacity_==0||key.epoch!=epoch_||value.status==Status::Invalid) return;
        auto it=values_.find(key);
        if (it!=values_.end()) { it->second.value=value;
            recency_.splice(recency_.begin(),recency_,it->second.where);return; }
        if (values_.size()==capacity_) { values_.erase(recency_.back());recency_.pop_back(); }
        recency_.push_front(key); values_.emplace(key,Item{value,recency_.begin()});
    }
}; // Single-threaded teaching cache, scoped to one immutable navigation dataset.
static_assert(!std::is_copy_constructible<PathCache>::value &&
              !std::is_copy_assignable<PathCache>::value, "cache iterators must not escape by copying");
static_assert(!std::is_move_constructible<PathCache>::value &&
              !std::is_move_assignable<PathCache>::value, "cache relocation is not supported");
Key keyFor(const Grid& g,const Query& q,std::uint64_t epoch=1,int profile=0) {
    return {g.id(q.sx,q.sy),g.id(q.gx,q.gy),profile,epoch};
}
Result cachedSearch(const Grid& g,const Query& q,PathCache& cache,bool& hit,std::uint64_t epoch=1,int profile=0) {
    Result out; hit=false;
    if (!validEndpoints(g,q)) { out.status=Status::Invalid;return out; }
    const Key key=keyFor(g,q,epoch,profile);
    if (cache.get(key,out)) {
        hit=true; out.pops=0; out.stale=0; out.expanded=0; return out;
    }
    out=search(g,q,true); cache.put(key,out); return out;
}
std::uint64_t consume(const Result& r) {
    std::uint64_t h=static_cast<std::uint64_t>(r.status)+1;
    for (int n:r.path) h=h*UINT64_C(1099511628211)^static_cast<std::uint64_t>(n+1);
    return h; // Exported aggregate consumes every copied path element, O(path length).
}
void require(bool yes,const std::string& what) { if (!yes) throw std::runtime_error(what); }
int selfTest(bool noTouch) {
    int passed=0,failed=0;std::uint64_t queries=0;
    auto test=[&](const std::string& name,auto run) { try { run();++passed;std::cout<<"PASS "<<name<<'\n'; }
        catch (const std::exception& e) { ++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n'; } };
    test("exhaustive_3x3_oracle_both_corner_policies",[&] {
        for (int cut=0;cut<2;++cut) for (int mask=0;mask<512;++mask) {
            Grid g(3,3);g.cornerCutting=cut!=0;
            for (int n=0;n<9;++n) g.blocked[static_cast<std::size_t>(n)]=static_cast<unsigned char>((mask>>n)&1);
            for (int a=0;a<9;++a) for (int b=0;b<9;++b) {
                Query q{a%3,a/3,b%3,b/3}; if (!validEndpoints(g,q)) continue;
                const double expected=oracle(g,q);
                for (bool heap:{false,true}) { const Result r=search(g,q,heap);
                    require(pathValid(g,q,r)&&agrees(r,expected),"oracle/path mismatch"); }
                ++queries;
            }
        }
    });
    test("heuristic_regression_5x5",[&] { Grid g(5,5);g.blocked[7]=1;g.blocked[13]=1;
        Query q{1,0,4,4};const double expected=oracle(g,q);require(near(expected,3+2*DIAGONAL),"oracle fixture");
        require(agrees(search(g,q,true),expected)&&agrees(search(g,q,false),expected),"heuristic regression"); });
    test("invalid_endpoints",[&] { Grid g(2,2);g.blocked[0]=1;
        for (const Query q:std::vector<Query>{{0,0,1,1},{1,1,0,0},{-1,0,1,1},{1,1,2,0}})
            for (bool heap:{true,false}) require(pathValid(g,q,search(g,q,heap))&&search(g,q,heap).status==Status::Invalid,"endpoint"); });
    test("validator_rejects_wrong_start_first_block_edge_cost",[&] { Grid g(3,1);Query q{0,0,2,0};Result r=search(g,q,true);
        require(pathValid(g,q,r),"valid fixture");Result bad=r;bad.path.erase(bad.path.begin());require(!pathValid(g,q,bad),"wrong start");
        bad=r;bad.path={0,2};require(!pathValid(g,q,bad),"invalid edge");bad=r;bad.cost+=1;require(!pathValid(g,q,bad),"cost");
        g.blocked[0]=1;require(!pathValid(g,q,r),"blocked first node"); });
    const Key a{0,1,0,1},b{0,2,0,1},c{0,3,0,1};Result value;value.status=Status::Found;value.path={0,1};value.cost=1;
    test("capacity_zero",[&] { PathCache cache(0,1);cache.put(a,value);Result out;require(cache.size()==0&&!cache.get(a,out),"disabled cache"); });
    test("capacity_one_and_overwrite",[&] { PathCache cache(1,1);cache.put(a,value);Result next=value;next.path={0,2};cache.put(a,next);
        Result out;require(cache.size()==1&&cache.get(a,out)&&out.path==next.path,"overwrite");cache.put(b,value);require(!cache.get(a,out)&&cache.get(b,out),"eviction"); });
    test("lru_hit_touches_recency",[&] { PathCache cache(2,1,noTouch);cache.put(a,value);cache.put(b,value);Result out;
        require(cache.get(a,out),"hit");cache.put(c,value);require(cache.get(a,out)&&!cache.get(b,out)&&cache.get(c,out),"hit must evict B, not A"); });
    test("overwrite_touches_without_duplicate_nodes",[&] { PathCache cache(2,1);cache.put(a,value);cache.put(b,value);cache.put(a,value);cache.put(c,value);
        Result out;require(cache.size()==2&&cache.get(a,out)&&!cache.get(b,out),"overwrite recency");cache.put(b,value);require(cache.size()==2,"duplicate node"); });
    test("miss_does_not_change_eviction",[&] { PathCache cache(2,1);cache.put(a,value);cache.put(b,value);Result out;
        require(!cache.get(c,out),"miss");cache.put(c,value);require(!cache.get(a,out)&&cache.get(b,out),"miss changed recency"); });
    test("empty_unreachable_is_cached_not_a_miss",[&] { Grid g(3,1);g.blocked[1]=1;Query q{0,0,2,0};PathCache cache(2,1);bool hit;
        const Result first=cachedSearch(g,q,cache,hit);require(!hit&&first.status==Status::Unreachable,"first unreachable");
        const Result second=cachedSearch(g,q,cache,hit);require(hit&&second.path.empty()&&agrees(second,oracle(g,q)),"negative cache"); });
    test("returned_path_is_an_independent_copy",[&] { PathCache cache(2,1);cache.put(a,value);Result out;require(cache.get(a,out),"get");out.path[0]=99;
        require(cache.get(a,out)&&out.path[0]==0,"alias escaped"); });
    test("epoch_invalidation_and_profile_isolation",[&] { PathCache cache(3,1);cache.put(a,value);Result out;Key different=a;different.profile=1;
        require(!cache.get(different,out),"profile collision");cache.setEpoch(2);require(cache.size()==0&&!cache.get(a,out),"stale epoch");
        cache.put(a,value);require(cache.size()==0,"stale insertion");different=a;different.epoch=2;cache.put(different,value);
        require(cache.get(different,out),"new epoch");cache.setEpoch(2);require(cache.size()==1,"same epoch cleared");
        Grid grid(3,1); Query q{0,0,2,0}; PathCache paths(2,1); bool hit;
        require(cachedSearch(grid,q,paths,hit).status==Status::Found&&!hit,"initial path");
        grid.blocked[1]=1; paths.setEpoch(2);
        require(cachedSearch(grid,q,paths,hit,2).status==Status::Unreachable&&!hit,"changed map stale path");
        require(cachedSearch(grid,q,paths,hit,2).status==Status::Unreachable&&hit,"new epoch negative cache"); });
    test("cached_results_match_uncached_on_mixed_sequence",[&] { Grid g(5,5);g.blocked[12]=1;PathCache cache(3,1);Rng rng(123);
        for (int i=0;i<200;++i) { Query q{rng.bounded(5),rng.bounded(5),rng.bounded(5),rng.bounded(5)};bool hit;
            const Result r=cachedSearch(g,q,cache,hit);require(pathValid(g,q,r),"path validity");
            if(validEndpoints(g,q)) require(agrees(r,oracle(g,q)),"oracle mismatch"); } });
    std::cout<<"SELF_TEST passed="<<passed<<" failed="<<failed<<" exhaustive_queries="<<queries
             <<" implementations=2 policies=2 mutant_no_touch="<<noTouch<<'\n';return failed?1:0;
}
Grid generatedGrid(int density,std::uint64_t seed) { Grid g(100,100);Rng rng(seed);
    for(auto& cell:g.blocked) { cell=static_cast<unsigned char>(rng.bounded(100)<density); }
    g.blocked[0]=0;return g; }
std::vector<Query> uniqueQueries(const Grid& g,int count,Rng& rng) {
    std::vector<Query> qs;std::unordered_map<std::uint64_t,bool> seen;
    while(static_cast<int>(qs.size())<count) { Query q{rng.bounded(g.width),rng.bounded(g.height),rng.bounded(g.width),rng.bounded(g.height)};
        if(!validEndpoints(g,q)||g.id(q.sx,q.sy)==g.id(q.gx,q.gy)) continue;
        const auto key=static_cast<std::uint64_t>(g.id(q.sx,q.sy))*static_cast<unsigned>(g.size())+static_cast<unsigned>(g.id(q.gx,q.gy));
        if(seen.emplace(key,true).second) qs.push_back(q);
    } return qs;
}
struct Aggregate { std::uint64_t checksum=0,nodes=0,hits=0,repeats=0,repeatHits=0,pops=0,stale=0,expanded=0,found=0,unreachable=0,invalid=0;double cost=0; };
void add(Aggregate& a,const Result& r,bool hit,bool repeated) { a.checksum=a.checksum*31+consume(r);
    a.nodes+=r.path.size();a.pops+=r.pops;a.stale+=r.stale;a.expanded+=r.expanded;
    if(r.status==Status::Found) { a.cost+=r.cost; ++a.found; }
    else if(r.status==Status::Unreachable) { ++a.unreachable; }
    else { ++a.invalid; }
    a.hits+=hit;a.repeats+=repeated;a.repeatHits+=hit&&repeated; }
void row(std::ostream& out,const std::string& type,const std::string& scenario,int density,std::uint64_t seed,
         int repeat,int order,int first,int operations,const std::string& method,double elapsed,const Aggregate& a,
         const Query& q={-1,-1,-1,-1}) {
    out<<type<<','<<scenario<<','<<density<<','<<seed<<','<<repeat<<','<<order<<','<<first<<','<<operations<<','<<method<<','
       <<elapsed<<','<<(operations?elapsed/operations:0)<<','<<a.hits<<','<<a.repeats<<','<<a.repeatHits<<','<<a.nodes<<','
       <<a.cost<<','<<a.checksum<<','<<q.sx<<','<<q.sy<<','<<q.gx<<','<<q.gy<<",1,"<<a.pops<<','<<a.stale<<','<<a.expanded<<','<<a.found<<','<<a.unreachable<<','<<a.invalid<<'\n';
}
int benchmark(const std::string& filename,int queries,int repeats,std::uint64_t seed) {
    std::ofstream out(filename,std::ios::out|std::ios::trunc);require(out.good(),"cannot open samples file");
    out.imbue(std::locale::classic());out<<std::setprecision(17);
    out<<"row_type,scenario,density,map_seed,repeat,order,first_query,operations,method,total_ns,ns_per_operation,hits,scheduled_repeats,repeat_hits,path_nodes,cost_sum,checksum,sx,sy,gx,gy,nav_epoch,pops,stale_pops,expanded,found,unreachable,invalid\n";
    double minimum=INF; std::vector<double> empty;
    for(int i=0;i<1000;++i) {const auto begin=Clock::now();const auto end=Clock::now();const double ns=std::chrono::duration<double,std::nano>(end-begin).count();
        if(ns>0) { minimum=std::min(minimum,ns); }
        empty.push_back(ns);row(out,"calibration","empty-clock",0,seed,0,0,i,1,"clock-pair",ns,{});}
    require(std::isfinite(minimum),"clock yielded no positive interval");
    std::cout<<"CLOCK steady="<<Clock::is_steady<<" period_num="<<Clock::period::num<<" period_den="<<Clock::period::den<<" min_positive_ns="<<minimum<<'\n';
    std::uint64_t finalChecksum=0;
    for(int density:{25,40}) {
        const std::uint64_t mapSeed=seed+static_cast<unsigned>(density);const Grid g=generatedGrid(density,mapSeed);Rng rng(seed);
        auto qs=uniqueQueries(g,queries,rng);
        for(int i=0;i<queries;++i) row(out,"input","search",density,mapSeed,0,0,i,0,"query",0,{},qs[static_cast<std::size_t>(i)]);
        // Warmup excluded from measurements, same query subset for both OPEN implementations.
        for(int i=0;i<std::min(32,queries);++i) for(bool heap:{true,false}) finalChecksum+=consume(search(g,qs[static_cast<std::size_t>(i)],heap));
        for(int repeat=0;repeat<repeats;++repeat) for(int i=0;i<queries;++i) {
            Result results[2];const Query& q=qs[static_cast<std::size_t>(i)];
            for(int order=0;order<2;++order) {const bool heap=((repeat+i+order)%2)==0;const auto t0=Clock::now();
                Result r=search(g,q,heap);Aggregate a;add(a,r,false,false);const double ns=std::chrono::duration<double,std::nano>(Clock::now()-t0).count();
                require(pathValid(g,q,r),"benchmark path invalid");results[heap?1:0]=r;
                row(out,"timing","search",density,mapSeed,repeat,order,i,1,heap?"heap-copy-consume":"linear-copy-consume",ns,a);finalChecksum+=a.checksum;
            }
            require(results[0].status==results[1].status && (results[0].status!=Status::Found||near(results[0].cost,results[1].cost)),"implementations disagree; not an optimality proof");
        }
        const auto pool=uniqueQueries(g,256,rng);
        for(const std::string phase:{"cold-fill","warm-hit","mixed-pressure"}) {
            std::vector<Query> workload;
            const int count=phase=="cold-fill"?64:queries;
            for(int i=0;i<count;++i) {
                Query q=pool[static_cast<std::size_t>(phase=="mixed-pressure"?rng.bounded(256):i%64)];
                if(phase=="mixed-pressure"&&i>=5&&i%4==3) {q=workload[static_cast<std::size_t>(i-5)];q.scheduledRepeat=true;}
                else q.scheduledRepeat=phase=="warm-hit";
                workload.push_back(q);Aggregate inputInfo;inputInfo.repeats=q.scheduledRepeat;
                row(out,"input",phase,density,mapSeed,0,0,i,0,"query",0,inputInfo,q);
            }
            for(int repeat=0;repeat<repeats;++repeat) {
                PathCache cache(128,1);
                if(phase=="warm-hit") for(int i=0;i<64;++i) cache.put(keyFor(g,pool[static_cast<std::size_t>(i)]),search(g,pool[static_cast<std::size_t>(i)],true));
                const int batch=phase=="cold-fill"?1:32;
                for(int first=0;first<count;first+=batch) {
                    const int n=std::min(batch,count-first);Aggregate ag[2];
                    for(int order=0;order<2;++order) {
                        const bool cached=((repeat+first/batch+order)%2)==0;const auto t0=Clock::now();
                        for(int j=0;j<n;++j) { const Query& q=workload[static_cast<std::size_t>(first+j)];bool hit=false;
                            Result r=cached?cachedSearch(g,q,cache,hit):search(g,q,true);add(ag[cached?1:0],r,hit,q.scheduledRepeat); }
                        const double ns=std::chrono::duration<double,std::nano>(Clock::now()-t0).count();
                        row(out,"timing",phase,density,mapSeed,repeat,order,first,n,cached?"cache-copy-consume":"uncached-copy-consume",ns,ag[cached?1:0]);finalChecksum+=ag[cached?1:0].checksum;
                    }
                    require(ag[0].checksum==ag[1].checksum&&ag[0].nodes==ag[1].nodes&&near(ag[0].cost,ag[1].cost),"cache changed returned results");
                }
            }
        }
        // Separate lookup-only measurement: borrowed metadata, no path copy/consumption.
        PathCache lookupCache(128,1);for(int i=0;i<64;++i) lookupCache.put(keyFor(g,pool[static_cast<std::size_t>(i)]),search(g,pool[static_cast<std::size_t>(i)],true));
        for(int repeat=0;repeat<repeats;++repeat) for(int batch=0;batch<32;++batch) {
            Aggregate a;const auto t0=Clock::now();
            for(int j=0;j<256;++j) {const auto* value=lookupCache.lookup(keyFor(g,pool[static_cast<std::size_t>(j%64)]));
                require(value!=nullptr,"primed lookup missed");a.checksum=a.checksum*31+value->path.size();++a.hits;}
            const double ns=std::chrono::duration<double,std::nano>(Clock::now()-t0).count();
            row(out,"timing","lookup-only",density,mapSeed,repeat,0,batch*256,256,"borrowed-metadata",ns,a);finalChecksum+=a.checksum;
        }
    }
    out.flush();require(out.good(),"sample write failed");
    std::cout<<"BENCHMARK queries="<<queries<<" repeats="<<repeats<<" seed="<<seed<<" cache_capacity=128 grid=100x100 corner_cutting=0 checksum="<<finalChecksum<<'\n';
    std::cout<<"Timing samples are observations, never speed assertions. Batched percentiles are percentiles of batch means.\n";return 0;
}
int main(int argc,char** argv) {
    try {
        bool self=false,noTouch=false;std::string csv;int queries=1000,repeats=5;std::uint64_t seed=20261004;
        for(int i=1;i<argc;++i) { const std::string arg=argv[i];
            if(arg=="--self-test") self=true;else if(arg=="--mutant-no-touch") noTouch=true;
            else if(arg=="--csv"&&i+1<argc) csv=argv[++i];
            else if(arg=="--queries"&&i+1<argc) queries=std::stoi(argv[++i]);
            else if(arg=="--repeats"&&i+1<argc) repeats=std::stoi(argv[++i]);
            else if(arg=="--seed"&&i+1<argc) seed=std::stoull(argv[++i]);
            else throw std::invalid_argument("unknown or incomplete argument: "+arg);
        }
        if(self) return selfTest(noTouch);
        require(!noTouch,"mutant only valid with --self-test");require(!csv.empty(),"use --self-test or --csv FILE");
        require(queries>=64&&queries<=5000&&repeats>=2&&repeats<=20,"queries/repeats outside laboratory limits");
        return benchmark(csv,queries,repeats,seed);
    } catch(const std::exception& e) {std::cerr<<"ERROR: "<<e.what()<<'\n';return 2;}
}
