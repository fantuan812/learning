// Independent state/effective-set oracles for the documented single-target model.
// Potency is test-only: base 100 + sum(effective potency * stacks), NOT GAS or
// the repository attribute calculator. No assert(): checks survive NDEBUG.
#ifndef BUFF_SOURCE
#define BUFF_SOURCE "../src/buff_conflict.cpp"
#endif
#define BUFF_CONTRACT_TEST
#include BUFF_SOURCE
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace {
struct Expected { int id, stacks; double remaining; int source; bool suppressed; };
struct Rule { int group, level, potency; };
const std::map<int, Rule> oracle = {
    {100,{1,1,1}}, {101,{1,1,2}}, {110,{2,1,3}}, {111,{2,1,4}},
    {120,{3,1,10}}, {121,{3,2,20}}, {122,{3,3,30}}, {124,{3,1,11}},
    {130,{4,1,4}}, {131,{5,1,5}}, {140,{6,1,6}}, {150,{7,1,7}},
    {160,{8,1,8}}, {201,{20,1,2}}, {202,{20,2,20}},
    {301,{30,1,1}}, {302,{30,2,2}}, {303,{31,1,3}},
    {400,{40,1,10}}, {401,{40,2,20}}, {402,{40,3,30}},
    {501,{50,1,1}}, {502,{50,2,2}}, {503,{51,1,3}},
    {601,{60,1,1}}, {602,{60,2,2}}, {603,{61,1,3}}
};
BuffSystem System() {
    BuffSystem b;
    // These fixtures are definitions, not production Build() or derived expected states.
    b.Register({100,1,1,false,1,10,1,{},0}); b.Register({101,1,1,true,5,10,1,{},0});
    b.Register({110,2,1,true,3,8,2,{},0}); b.Register({111,2,1,false,1,8,2,{},0});
    b.Register({120,3,1,false,1,12,1,{},0}); b.Register({121,3,2,false,1,6,1,{},0});
    b.Register({122,3,3,false,1,3,1,{},0}); b.Register({124,3,1,false,1,12,1,{},0});
    b.Register({130,4,1,false,1,5,1,{5},0}); b.Register({131,5,1,false,1,20,1,{},0});
    b.Register({140,6,1,false,1,4,1,{},150}); b.Register({150,7,1,false,1,4,1,{},0});
    b.Register({160,8,1,false,1,0,1,{5},0});
    b.Register({201,20,1,true,5,10,2,{},0}); b.Register({202,20,2,false,1,6,1,{},0});
    b.Register({301,30,1,false,1,10,1,{31},0}); b.Register({302,30,2,false,1,10,1,{},0});
    b.Register({303,31,1,false,1,10,2,{},0});
    b.Register({400,40,1,false,1,20,2,{},0}); b.Register({401,40,2,false,1,10,2,{},0});
    b.Register({402,40,3,false,1,4,1,{},0});
    b.Register({501,50,1,false,1,20,2,{},0}); b.Register({502,50,2,false,1,10,1,{},0});
    b.Register({503,51,1,false,1,5,1,{50},0});
    b.Register({601,60,1,false,1,12,2,{61},0}); b.Register({602,60,2,false,1,6,1,{},0});
    b.Register({603,61,1,false,1,20,2,{},0});
    return b;
}
int checks=0, failures=0, cases=0;
std::string current;
void Expect(bool value, const std::string& label) {
    ++checks;
    if (!value) { ++failures; std::cout << "FAIL " << current << " " << label << '\n'; }
}
bool Near(double a,double b) { return std::isfinite(a) && std::fabs(a-b)<1e-9; }
std::string Show(const std::vector<ActiveBuff>& rows) {
    std::ostringstream s; s<<std::setprecision(17)<<'[';
    for (const auto& a:rows) s<<"(id="<<a.id<<",stacks="<<a.stacks<<",remaining="<<a.remaining
        <<",source="<<a.sourceId<<",suppressed="<<a.suppressed<<")";
    return s.str()+']';
}
std::string ShowIds(const std::vector<int>& ids) {
    std::ostringstream s; s<<'['; for(int id:ids) s<<id<<','; return s.str()+']';
}
void State(const BuffSystem& b, std::vector<Expected> expected,
           std::vector<int> effective, int projection, const std::string& step) {
    auto observed=b.Snapshot();
    std::sort(observed.begin(),observed.end(),[](const auto&a,const auto&c){return a.id<c.id;});
    std::sort(expected.begin(),expected.end(),[](const auto&a,const auto&c){return a.id<c.id;});
    std::vector<ActiveBuff> literal;
    for (auto e:expected) literal.push_back({e.id,e.stacks,e.remaining,e.source,e.suppressed});
    bool equal=observed.size()==expected.size();
    for(size_t i=0;i<std::min(observed.size(),expected.size());++i) {
        const auto&a=observed[i]; const auto&e=expected[i];
        equal=equal && a.id==e.id && a.stacks==e.stacks && Near(a.remaining,e.remaining)
                    && a.sourceId==e.source && a.suppressed==e.suppressed;
    }
    Expect(equal,step+" full state observed="+Show(observed)+" expected="+Show(literal));
    std::vector<int> actualEffective;
    int value=100; std::set<int> seen; std::map<int,int> maximum, activeCounts;
    bool domain=true;
    for (const auto& a:observed) {
        auto it=oracle.find(a.id);
        domain=domain && it!=oracle.end() && seen.insert(a.id).second && a.stacks>0 && a.remaining>0 && std::isfinite(a.remaining);
        if(it==oracle.end()) continue;
        const Rule r=it->second;
        auto m=maximum.find(r.group);
        if(m==maximum.end() || m->second<r.level) maximum[r.group]=r.level;
        if(!a.suppressed) {actualEffective.push_back(a.id); value+=r.potency*a.stacks; ++activeCounts[r.group];}
    }
    std::sort(effective.begin(),effective.end());
    Expect(actualEffective==effective,step+" effective observed="+ShowIds(actualEffective)+" expected="+ShowIds(effective));
    Expect(value==projection,step+" synthetic projection observed="+std::to_string(value)+" expected="+std::to_string(projection));
    Expect(domain,step+" live ID uniqueness and finite positive state domain");
    bool invariant=true;
    for(const auto&a:observed) {
        auto it=oracle.find(a.id); if(it==oracle.end()) {invariant=false;continue;}
        const Rule r=it->second;
        invariant=invariant && a.suppressed==(r.level<maximum.at(r.group)) && activeCounts[r.group]==1;
    }
    Expect(invariant,step+" independent highest-survivor invariant");
    bool views=b.Size()==static_cast<int>(observed.size());
    for(const auto& [id,r]:oracle) {
        views=views && b.Count(id)==static_cast<int>(seen.count(id));
        const auto* a=b.FindById(id);
        views=views && (a!=nullptr)==(seen.count(id)>0) && b.GroupCount(r.group)==activeCounts[r.group];
    }
    Expect(views,step+" public lookup/count views agree with complete snapshot");
    std::cout<<"STATE "<<current<<" "<<step<<" observed="<<Show(observed)<<" expected="<<Show(literal)
             <<" effective="<<ShowIds(actualEffective)<<" expected_effective="<<ShowIds(effective)
             <<" projection="<<value<<" expected_projection="<<projection<<'\n';
}
template<class F> void Case(const std::string& name,F body) {
    current=name; ++cases; int before=failures; body();
    std::cout<<(failures==before?"PASS_CASE ":"FAIL_CASE ")<<name<<'\n';
}
void Tests() {
    Case("refresh_same_id",[]{auto b=System(); b.Apply(120,7);b.Apply(121,7);
        Expect(b.Apply(120,7)=="refreshed","accepted existing lower");
        State(b,{{120,1,12,7,true},{121,1,6,7,false}},{121},120,"immediate");
        b.Tick(1);State(b,{{120,1,11,7,true},{121,1,5,7,false}},{121},120,"after-tick");});
    Case("refresh_source_rebind",[]{auto b=System();b.Apply(120,7);b.Apply(121,7);b.Tick(2);b.Apply(120,9);
        State(b,{{120,1,12,9,true},{121,1,4,7,false}},{121},120,"same-logical-entry");
        b.Tick(4);State(b,{{120,1,8,9,false}},{120},110,"survives-high-expiry");});
    Case("suppressed_stack_cap",[]{auto b=System();b.Apply(201,7);b.Apply(201,7);b.Apply(202,7);b.Tick(1);b.Apply(201,9);
        State(b,{{201,3,10,9,true},{202,1,5,7,false}},{202},120,"third-stack");
        for(int i=0;i<7;++i)b.Apply(201,11);
        State(b,{{201,5,10,11,true},{202,1,5,7,false}},{202},120,"cap");b.Tick(5);
        State(b,{{201,5,5,11,false}},{201},110,"cap-restored");});
    Case("three_tier_recovery",[]{auto b=System();b.Apply(400,7);b.Apply(401,8);b.Apply(402,9);
        State(b,{{400,1,20,7,true},{401,1,10,8,true},{402,1,4,9,false}},{402},130,"natural-apply-chain");
        b.Tick(4);State(b,{{400,1,16,7,true},{401,1,6,8,false}},{401},120,"middle-restores");
        b.Tick(6);State(b,{{400,1,10,7,false}},{400},110,"lowest-restores");
        b.Tick(10);State(b,{}, {},100,"all-expired");});
    // Each row is a literal expected history. A new lower ID is rejected when
    // stronger already exists, so the six final storage sets are NOT identical.
    const int orders[6][3]={{120,121,122},{120,122,121},{121,120,122},{121,122,120},{122,120,121},{122,121,120}};
    const std::vector<std::vector<Expected>> histories={
        {{120,1,12,7,true},{121,1,6,7,true},{122,1,3,7,false}},
        {{120,1,12,7,true},{122,1,3,7,false}},
        {{121,1,6,7,true},{122,1,3,7,false}},
        {{121,1,6,7,true},{122,1,3,7,false}},
        {{122,1,3,7,false}},{{122,1,3,7,false}}};
    const std::string results[6][3]={{"applied","suppressed-lower","suppressed-lower"},
        {"applied","suppressed-lower","blocked-by-higher"},{"applied","blocked-by-higher","suppressed-lower"},
        {"applied","suppressed-lower","blocked-by-higher"},{"applied","blocked-by-higher","blocked-by-higher"},
        {"applied","blocked-by-higher","blocked-by-higher"}};
    for(int i=0;i<6;++i)Case("rank_order_"+std::to_string(i+1),[&,i]{auto b=System();
        for(int k=0;k<3;++k)Expect(b.Apply(orders[i][k],7)==results[i][k],"admission step "+std::to_string(k));
        State(b,histories[i],{122},130,"history-specific-final");});
    Case("same_rank_replace",[]{auto b=System();b.Apply(110,7);b.Apply(110,7);b.Tick(3);
        Expect(b.Apply(111,9)=="replaced","replace result");State(b,{{111,1,8,9,false}},{111},104,"reset-new-entry");});
    Case("lower_rank_replace_blocked",[]{auto b=System();b.Apply(120,7);b.Apply(121,8);
        Expect(b.Apply(124,9)=="blocked-by-higher","new equal-low ID still blocked");
        State(b,{{120,1,12,7,true},{121,1,6,8,false}},{121},120,"unchanged");});
    Case("continue_time_overshoot",[]{auto b=System();b.Apply(120,7);b.Tick(2);b.Apply(121,7);b.Tick(6.5);
        State(b,{{120,1,3.5,7,false}},{120},110,"remaining-not-restarted");});
    Case("time_partition",[]{auto b=System();b.Apply(120,7);b.Tick(2);b.Apply(121,7);b.Tick(3);b.Tick(3.5);
        State(b,{{120,1,3.5,7,false}},{120},110,"same-endpoint-clock-only");});
    Case("simultaneous_expiry",[]{auto b=System();b.Apply(120,7);b.Tick(6);b.Apply(121,7);b.Tick(6);
        State(b,{}, {},100,"exact-boundary");b.Tick(0);State(b,{}, {},100,"no-resurrection");});
    Case("suppressed_self_expiry",[]{auto b=System();b.Apply(120,7);b.Tick(10);b.Apply(121,7);b.Tick(2);
        State(b,{{121,1,4,7,false}},{121},120,"loser-expired-first");b.Tick(4);State(b,{}, {},100,"winner-expired");});
    Case("dispel_all_matching",[]{auto b=System();b.Apply(100,7);b.Apply(110,8);b.Apply(120,7);b.Apply(121,7);
        Expect(b.Dispel(1)==3,"exact count includes suppressed");State(b,{{110,1,8,8,false}},{110},103,"curse-remains");
        b.Tick(1);State(b,{{110,1,7,8,false}},{110},103,"nothing-resurrects");});
    Case("dispel_highest_immediate",[]{auto b=System();b.Apply(400,7);b.Apply(401,8);b.Apply(402,9);b.Tick(1);
        Expect(b.Dispel(1)==1,"only highest magic removed");
        State(b,{{400,1,19,7,true},{401,1,9,8,false}},{401},120,"before-any-next-tick");
        Expect(b.Dispel(99)==0,"unknown school no deletion");
        State(b,{{400,1,19,7,true},{401,1,9,8,false}},{401},120,"no-op-dispel");});
    Case("directional_exclusion",[]{auto b=System();b.Apply(131,7);b.Apply(130,8);
        State(b,{{130,1,5,8,false}},{130},104,"deletion");b.Tick(5);State(b,{}, {},100,"deleted-never-restores");
        b.Apply(130,8);b.Apply(131,9);State(b,{{130,1,5,8,false},{131,1,20,9,false}},{130,131},109,"reverse-not-configured");});
    Case("exclusion_removes_suppressed",[]{auto b=System();b.Apply(501,7);b.Apply(502,8);b.Apply(503,9);
        State(b,{{503,1,5,9,false}},{503},103,"whole-excluded-group-deleted");});
    Case("rejected_no_exclusion",[]{auto b=System();b.Apply(302,7);b.Apply(303,8);
        Expect(b.Apply(301,9)=="blocked-by-higher","reject lower with exclusion");
        State(b,{{302,1,10,7,false},{303,1,10,8,false}},{302,303},105,"other-group-untouched");});
    Case("ordinary_refresh_exclusion",[]{auto b=System();b.Apply(130,7);b.Apply(131,8);b.Tick(1);
        Expect(b.Apply(130,9)=="refreshed","accepted ordinary refresh");State(b,{{130,1,5,9,false}},{130},104,"on-apply-repeated");});
    Case("suppressed_refresh_exclusion",[]{auto b=System();b.Apply(601,7);b.Apply(602,8);b.Apply(603,9);
        Expect(b.Apply(601,10)=="refreshed","existing loser admitted");
        State(b,{{601,1,12,10,true},{602,1,6,8,false}},{602},102,"accepted-loser-still-excludes");});
    Case("periodic_preserves_stacks",[]{auto b=System();b.Apply(101,7);b.Apply(101,7);b.Tick(3);
        Expect(b.PeriodicReapply(101,9)=="refreshed-no-extra-stack","refresh-only result");
        State(b,{{101,2,10,9,false}},{101},104,"two-not-three");
        for(int i=0;i<6;++i)b.PeriodicReapply(101,11);
        State(b,{{101,2,10,11,false}},{101},104,"repeated-still-two");});
    Case("periodic_at_cap",[]{auto b=System();for(int i=0;i<9;++i)b.Apply(101,7);b.PeriodicReapply(101,9);
        State(b,{{101,5,10,9,false}},{101},110,"cap-preserved");});
    Case("periodic_suppressed_child",[]{auto b=System();b.Apply(201,7);b.Apply(201,7);b.Apply(202,8);b.Tick(2);b.PeriodicReapply(201,9);
        State(b,{{201,2,10,9,true},{202,1,4,8,false}},{202},120,"suppressed-stack-two");});
    Case("periodic_nonstack_child",[]{auto b=System();b.Apply(120,7);b.Apply(121,8);b.PeriodicReapply(120,9);
        State(b,{{120,1,12,9,true},{121,1,6,8,false}},{121},120,"nonstack-suppressed");});
    Case("periodic_missing_child",[]{auto b=System();Expect(b.PeriodicReapply(101,7)=="applied","first-child-full-apply");
        b.PeriodicReapply(101,9);State(b,{{101,1,10,9,false}},{101},102,"one-layer");
        b.Apply(121,8);Expect(b.PeriodicReapply(120,9)=="blocked-by-higher","first-lower-child-rejected");
        State(b,{{101,1,10,9,false},{121,1,6,8,false}},{101,121},122,"full-admission");});
    Case("periodic_existing_no_exclusion",[]{auto b=System();b.Apply(130,7);b.Apply(131,8);b.Tick(1);b.PeriodicReapply(130,9);
        State(b,{{130,1,5,9,false},{131,1,19,8,false}},{130,131},109,"one-shot-not-repeated");});
    Case("periodic_missing_exclusion",[]{auto b=System();b.Apply(131,8);Expect(b.PeriodicReapply(130,9)=="applied","new-child-apply");
        State(b,{{130,1,5,9,false}},{130},104,"first-child-excludes");});
    Case("rejected_unknown_zero",[]{auto b=System();b.Apply(131,7);
        Expect(b.Apply(999,9)=="unknown-buff","unknown rejected");Expect(b.PeriodicReapply(999,9)=="unknown-buff","unknown child rejected");
        Expect(b.Apply(160,9)=="expired-on-apply","zero duration rejects before exclusion");
        Expect(b.PeriodicReapply(160,9)=="expired-on-apply","missing zero child rejects");
        State(b,{{131,1,20,7,false}},{131},105,"all-rejections-no-op");});
    Case("periodic_metadata_only",[]{auto b=System();b.Apply(140,7);b.Tick(2);
        State(b,{{140,1,2,7,false}},{140},106,"no-automatic-child");});
    Case("zero_tick_stable",[]{auto b=System();b.Apply(400,7);b.Apply(401,8);b.Apply(402,9);b.Tick(0);
        State(b,{{400,1,20,7,true},{401,1,10,8,true},{402,1,4,9,false}},{402},130,"zero-time-no-reset");});
}
}
int main() {
    std::cout<<"BUFF_CONTRACT v1 test-only additive projection; no engine/clock scheduler integration\n";
    Tests();
    std::cout<<"RESULT cases="<<cases<<" checks="<<checks<<" fail="<<failures
             <<" (explicit checks remain active with NDEBUG)\n";
    return failures==0?0:1;
}
