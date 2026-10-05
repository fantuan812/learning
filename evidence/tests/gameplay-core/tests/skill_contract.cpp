// Independent literal/full-field state oracles. No assert or legacy hash as oracle.
#ifndef SKILL_SOURCE
#define SKILL_SOURCE "../src/skill_pipeline.cpp"
#endif
#define SKILL_MODEL_ONLY
#include SKILL_SOURCE
#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace {
int checks = 0, failures = 0, cases = 0;
struct Case {
    const char* name;
    int count = 0, failed = 0, ledger = 0;
    explicit Case(const char* n) : name(n) { ++cases; }
    void Check(bool ok) {
        ++count; ++checks;
        if (!ok) { ++failed; ++failures; }
        std::cout << "CHECK " << name << ' ' << count << ' ' << (ok ? "PASS" : "FAIL") << '\n';
    }
    ~Case() { std::cout << "CASE " << name << " checks=" << count << " fail=" << failed << " ledger=" << ledger << '\n'; }
};
// Independent representation includes EVERY Actor field. Containers have canonical
// order; no memory/padding hashing and no iteration-order assumption.
struct State {
    bool alive;
    int mana, maxMana;
    int64_t gcd, castUntil;
    int castSkill;
    bool interruptible;
    std::map<int, int64_t> cooldown;
    std::set<uint64_t> ids;
    uint64_t hash;
    int accepted, rejected;
    auto Fields() const { return std::tie(alive, mana, maxMana, gcd, castUntil, castSkill,
        interruptible, cooldown, ids, hash, accepted, rejected); }
    bool operator==(const State& b) const { return Fields() == b.Fields(); }
    bool operator!=(const State& b) const { return !(*this == b); }
};
State Capture(const Actor& a) {
    return {a.alive, a.mana, a.maxMana, a.gcdUntilMs, a.castingUntilMs, a.castingSkill,
        a.castingInterruptible, {a.cooldownEnd.begin(), a.cooldownEnd.end()},
        {a.applied.begin(), a.applied.end()}, a.acceptedHash, a.accepted, a.rejected};
}
State Initial() { return {true,100,100,0,0,0,true,{}, {},1469598103934665603ULL,0,0}; }
State RichLiteral() {
    return {true,80,100,1000,800,1,true,{{1,6500},{3,3000}},
        {11,22},17072111852652606902ULL,2,0};
}
std::string Json(const State& a) {
    std::ostringstream o;
    o << "{\"alive\":" << a.alive << ",\"mana\":" << a.mana << ",\"maxMana\":" << a.maxMana
      << ",\"gcdUntilMs\":" << a.gcd << ",\"castingUntilMs\":" << a.castUntil
      << ",\"castingSkill\":" << a.castSkill << ",\"castingInterruptible\":" << a.interruptible << ",\"cooldownEnd\":[";
    bool comma = false;
    for (const auto& x : a.cooldown) { if (comma) o << ','; comma = true; o << '[' << x.first << ',' << x.second << ']'; }
    o << "],\"applied\":["; comma = false;
    for (uint64_t x : a.ids) { if (comma) o << ','; comma = true; o << x; }
    o << "],\"acceptedHash\":" << a.hash << ",\"accepted\":" << a.accepted << ",\"rejected\":" << a.rejected << '}';
    return o.str();
}
uint64_t Bits(double d) { uint64_t b = 0; static_assert(sizeof b == sizeof d, "binary64 required"); std::memcpy(&b,&d,sizeof b); return b; }
std::string RequestJson(const Request& r) {
    std::ostringstream o;
    o << "{\"requestId\":" << r.requestId << ",\"skillId\":" << r.skillId << ",\"targetId\":" << r.targetId
      << ",\"nowMs\":" << r.nowMs << ",\"distance_binary64_bits\":" << Bits(r.targetDistance) << '}'; return o.str();
}
std::string DefinitionJson(const SkillDef* d) {
    if (!d) return "null";
    std::ostringstream o;
    o << "{\"id\":" << d->id << ",\"cooldownMs\":" << d->cooldownMs << ",\"manaCost\":" << d->manaCost
      << ",\"castMs\":" << d->castMs << ",\"interruptible\":" << d->interruptible
      << ",\"minRange_bits\":" << Bits(d->minRange) << ",\"maxRange_bits\":" << Bits(d->maxRange) << ",\"hostile\":" << d->hostile << '}'; return o.str();
}
std::string Step(Case& c, SkillServer& s, Actor& a, const Request& r, const char* reason, const State& expected) {
    const State before = Capture(a);
    const std::string result = s.Handle(a,r);
    ++c.ledger;
    std::cout << "LEDGER " << c.name << ' ' << c.ledger << " {\"operation\":\"Handle\",\"input\":" << RequestJson(r)
      << ",\"selected_definition\":" << DefinitionJson(s.Def(r.skillId)) << ",\"before\":" << Json(before)
      << ",\"expected_reason\":\"" << reason << "\",\"actual_reason\":\"" << result << "\",\"expected\":" << Json(expected)
      << ",\"actual\":" << Json(Capture(a)) << "}\n";
    c.Check(result == reason); c.Check(Capture(a) == expected); return result;
}
void Reject(Case& c, SkillServer& s, Actor& a, const Request& r, const char* reason) {
    State expected = Capture(a); ++expected.rejected;
    Step(c,s,a,r,reason,expected);
}
void InterruptStep(Case& c, SkillServer& s, Actor& a, const State& expected) {
    const State before = Capture(a); s.Interrupt(a); ++c.ledger;
    std::cout << "LEDGER " << c.name << ' ' << c.ledger << " {\"operation\":\"Interrupt\",\"before\":" << Json(before)
      << ",\"expected\":" << Json(expected) << ",\"actual\":" << Json(Capture(a)) << "}\n";
    c.Check(Capture(a) == expected);
}
Actor Rich(SkillServer& s, Case& c) {
    Actor a;
    Step(c,s,a,{11,3,0,0,0},"accepted",{true,100,100,500,0,3,true,{{3,3000}},{11},10395539904131214889ULL,1,0});
    Step(c,s,a,{22,1,9,500,3},"accepted",RichLiteral()); return a;
}
void LiteralProgression() {
    Case c("literal_progression"); auto s = MakeServer(); Actor a; c.Check(Capture(a)==Initial());
    Step(c,s,a,{11,3,0,0,0},"accepted",{true,100,100,500,0,3,true,{{3,3000}},{11},10395539904131214889ULL,1,0});
    Step(c,s,a,{22,1,9,500,3},"accepted",RichLiteral());
    State interrupted = RichLiteral(); interrupted.castUntil=0; interrupted.castSkill=0;
    InterruptStep(c,s,a,interrupted); // no refund, cooldown/GCD/ID retained
    Step(c,s,a,{33,2,9,1500,10},"accepted",{true,40,100,2000,3000,2,false,{{1,6500},{2,13500},{3,3000}},
        {11,22,33},1026762647771550989ULL,3,0});
    const State hardCast = Capture(a); InterruptStep(c,s,a,hardCast);
    Step(c,s,a,{44,1,9,6500,3},"accepted",{true,20,100,7000,6800,1,true,{{1,12500},{2,13500},{3,3000}},
        {11,22,33,44},18201316792960711592ULL,4,0});
}
void RejectionGates() {
    const std::vector<std::string> names = {"duplicate","unknown","dead","busy","gcd","cooldown","mana","target","nan","posinf","neginf","near","far"};
    for (const auto& n : names) {
        const std::string name = "reject_"+n; Case c(name.c_str()); auto s = MakeServer(); Actor a = Rich(s,c);
        Request r{91,1,9,6500,3}; const char* expected="out-of-range";
        if(n=="duplicate") { r.requestId=22; r.skillId=999; a.alive=false; expected="duplicate"; }
        if(n=="unknown") { r.skillId=999; a.alive=false; expected="unknown-skill"; }
        if(n=="dead") { a.alive=false; expected="dead"; }
        if(n=="busy") { a.castingUntilMs=7000; a.castingInterruptible=false; a.gcdUntilMs=7000; expected="busy"; }
        if(n=="gcd") { r.nowMs=800; expected="gcd"; }
        if(n=="cooldown") { r.nowMs=1000; expected="cooldown"; }
        if(n=="mana") { a.mana=10; r.targetId=0; expected="no-mana"; }
        if(n=="target") { r.targetId=0; r.targetDistance=std::numeric_limits<double>::quiet_NaN(); expected="invalid-target"; }
        if(n=="nan") { r.targetDistance=std::numeric_limits<double>::quiet_NaN(); expected="invalid-distance"; }
        if(n=="posinf") { r.targetDistance=std::numeric_limits<double>::infinity(); expected="invalid-distance"; }
        if(n=="neginf") { r.targetDistance=-std::numeric_limits<double>::infinity(); expected="invalid-distance"; }
        if(n=="near") r.targetDistance=-1;
        if(n=="far") r.targetDistance=5.01;
        Reject(c,s,a,r,expected);
    }
}
void Identity() {
    {
        Case c("accepted_id_scope"); auto s=MakeServer(); Actor a,b;
        const State once{true,80,100,500,300,1,true,{{1,6000}},{77},6016062864314178957ULL,1,0};
        Step(c,s,a,{77,1,9,0,3},"accepted",once);
        Reject(c,s,a,{77,1,9,6000,3},"duplicate");
        Reject(c,s,a,{77,2,88,18000,10},"duplicate");
        Step(c,s,b,{77,1,9,0,3},"accepted",once);
    }
    {
        Case c("rejection_retry"); auto s=MakeServer(); Actor a; a.mana=10;
        Reject(c,s,a,{9,1,9,0,3},"no-mana"); a.mana=100; // separate external refill
        Step(c,s,a,{9,1,9,0,3},"accepted",{true,80,100,500,300,1,true,{{1,6000}},{9},16191020811003236409ULL,1,1});
    }
    {
        Case c("lower_id_arrives_later"); auto s=MakeServer(); Actor a;
        Step(c,s,a,{2,1,9,0,3},"accepted",{true,80,100,500,300,1,true,{{1,6000}},{2},18034794066470282404ULL,1,0});
        Step(c,s,a,{1,1,9,6000,3},"accepted",{true,60,100,6500,6300,1,true,{{1,12000}},{1,2},8683492701414595172ULL,2,0});
        Reject(c,s,a,{2,1,9,12000,3},"duplicate");
    }
}
void TimeBoundaries() {
    const int64_t max=std::numeric_limits<int64_t>::max();
    for(int which=0;which<3;++which) {
        const std::string field=which==0?"cooldown":which==1?"cast":"gcd";
        const int64_t duration=which==2?500:6000;
        for(int excess=0;excess<=1;++excess) {
            const std::string name="time_"+field+(excess?"_overflow":"_exact_max"); Case c(name.c_str());
            auto s=MakeServer(); Actor a=Rich(s,c);
            s.Register({4,which==0?duration:0,20,which==1?duration:0,true,0,5,true});
            const int64_t now=(max-duration)+excess; // subtraction first; expression is representable
            if(excess) Reject(c,s,a,{91,4,9,now,3},"time-overflow");
            else {
                State expected=RichLiteral(); expected.mana=60; expected.gcd=which==2?max: max-(duration-500);
                expected.castUntil=which==1?max:now; expected.castSkill=4;
                expected.cooldown[4]=which==0?max:now; expected.ids.insert(91);
                // Independent literal hash for the fixed prefix (11,3),(22,1),(91,4).
                expected.hash=10464815150381655381ULL; expected.accepted=3;
                Step(c,s,a,{91,4,9,now,3},"accepted",expected);
            }
        }
    }
    {
        Case c("unused_long_definition"); auto s=MakeServer(); Actor a;
        s.Register({4,0,20,0,true,0,5,true}); s.Register({5,max,20,max,true,0,5,true});
        Step(c,s,a,{91,4,9,max-500,3},"accepted",{true,80,100,max,max-500,4,true,{{4,max-500}},
            {91},2763102097341151750ULL,1,0});
    }
    for(int which=0;which<2;++which) {
        const std::string name=which==0?"max_duration_cooldown":"max_duration_cast"; Case c(name.c_str());
        auto s=MakeServer(); Actor a;
        s.Register({4,which==0?max:0,20,which==1?max:0,true,0,5,true});
        Step(c,s,a,{91,4,9,0,3},"accepted",{true,80,100,500,which==1?max:0,4,true,{{4,which==0?max:0}},
            {91},2763102097341151750ULL,1,0});
    }
    for(const char* gate:{"duplicate","unknown","dead","mana","target","distance","range"}) {
        const std::string name=std::string("early_gate_before_time_")+gate; Case c(name.c_str()); auto s=MakeServer(); Actor a=Rich(s,c);
        Request r{91,1,9,max,3}; const char* reason="out-of-range";
        if(std::string(gate)=="duplicate") { r.requestId=22; reason="duplicate"; }
        if(std::string(gate)=="unknown") { r.skillId=999; reason="unknown-skill"; }
        if(std::string(gate)=="dead") { a.alive=false; reason="dead"; }
        if(std::string(gate)=="mana") { a.mana=10; reason="no-mana"; }
        if(std::string(gate)=="target") { r.targetId=0; reason="invalid-target"; }
        if(std::string(gate)=="distance") { r.targetDistance=std::numeric_limits<double>::infinity(); reason="invalid-distance"; }
        if(std::string(gate)=="range") r.targetDistance=6;
        Reject(c,s,a,r,reason);
    }
    // All three deadlines are still computed for a zero-duration selected skill;
    // its GCD can overflow. Earlier timed gates also retain their priority.
    for(const char* gate:{"busy","gcd","cooldown"}) {
        const std::string name=std::string("timed_gate_before_time_")+gate; Case c(name.c_str()); auto s=MakeServer(); Actor a=Rich(s,c);
        s.Register({4,max,20,max,true,0,5,true}); Request r{91,4,9,6500,3};
        if(std::string(gate)=="busy") { a.castingUntilMs=7000; a.castingInterruptible=false; }
        if(std::string(gate)=="gcd") a.gcdUntilMs=7000;
        if(std::string(gate)=="cooldown") a.cooldownEnd[4]=7000;
        Reject(c,s,a,r,gate);
    }
    {
        Case c("checked_add_edges"); int64_t out=17;
        c.Check(CheckedTimeAdd(max,0,out)&&out==max);
        out=17; c.Check(!CheckedTimeAdd(max,1,out)&&out==17);
        const int64_t min=std::numeric_limits<int64_t>::min();
        c.Check(CheckedTimeAdd(min,0,out)&&out==min);
        out=17; c.Check(!CheckedTimeAdd(min,-1,out)&&out==17);
        c.Check(CheckedTimeAdd(0,min,out)&&out==min);
        c.Check(CheckedTimeAdd(max,min,out)&&out==-1);
        // Helper arithmetic checks, NOT newly supported negative gameplay time/config.
    }
}
uint64_t LegacyProjection(const Actor& a) {
    return a.acceptedHash ^ (static_cast<uint64_t>(a.mana)*2654435761u) ^
        (static_cast<uint64_t>(a.accepted)*40503u) ^ (static_cast<uint64_t>(a.rejected)*2246822519u);
}
void OracleSensitivity() {
    Case c("full_state_and_intent"); auto s=MakeServer(); Actor a,b;
    const Request left{1,1,9,0,3}, right{1,1,88,100,3};
    Step(c,s,a,left,"accepted",{true,80,100,500,300,1,true,{{1,6000}},{1},11709028599215228945ULL,1,0});
    Step(c,s,b,right,"accepted",{true,80,100,600,400,1,true,{{1,6100}},{1},11709028599215228945ULL,1,0});
    c.Check(LegacyProjection(a)==LegacyProjection(b)); c.Check(Capture(a)!=Capture(b));
    b=a; b.cooldownEnd[1]=6001; c.Check(Capture(a)!=Capture(b)); c.Check(LegacyProjection(a)==LegacyProjection(b));
    // A changed target can legitimately leave identical toy state; intent is a separate ledger.
    b=Actor{}; Step(c,s,b,{1,1,88,0,3},"accepted",{true,80,100,500,300,1,true,{{1,6000}},{1},11709028599215228945ULL,1,0});
    c.Check(Capture(a)==Capture(b));
    c.Check(RequestJson(left)!=RequestJson({1,1,88,0,3}));
    const State before=Capture(a);
    const std::vector<int> fields={0,1,2,3,4,5,6,7,8,9,10,11};
    for(int field:fields) {
        b=a;
        switch(field) {
        case 0:b.alive=false;break; case 1:--b.mana;break; case 2:++b.maxMana;break;
        case 3:++b.gcdUntilMs;break; case 4:++b.castingUntilMs;break; case 5:++b.castingSkill;break;
        case 6:b.castingInterruptible=false;break; case 7:b.cooldownEnd[2]=1;break;
        case 8:b.applied.insert(99);break; case 9:++b.acceptedHash;break;
        case 10:++b.accepted;break; case 11:++b.rejected;break;
        }
        c.Check(Capture(b)!=before);
    }
}
void RepeatedStream() {
    Case c("bounded_stream_literals");
    const std::vector<Request> inputs={{11,3,0,0,0},{22,1,9,500,3},{33,2,9,1500,10}};
    const std::vector<State> expected={
        {true,100,100,500,0,3,true,{{3,3000}},{11},10395539904131214889ULL,1,0}, RichLiteral(),
        {true,40,100,2000,3000,2,false,{{1,6500},{2,13500},{3,3000}},{11,22,33},1026762647771550989ULL,3,0}};
    std::vector<State> prior;
    for(int repeat=0;repeat<2;++repeat) { auto s=MakeServer(); Actor a;
        for(size_t i=0;i<inputs.size();++i) {
            Step(c,s,a,inputs[i],"accepted",expected[i]);
            if(repeat==0) prior.push_back(Capture(a)); else c.Check(Capture(a)==prior[i]);
        }
    }
}
void TeachingCountermodels() {
    {
        Case c("tutorial_prediction_copy"); auto s=MakeServer(); Actor authority=Rich(s,c);
        const State before=Capture(authority); Actor prediction=authority; // MUTATION_SITE prediction_copy
        prediction.mana-=20; prediction.cooldownEnd[1]=9999; prediction.applied.insert(55);
        c.Check(Capture(authority)==before); c.Check(Capture(prediction)!=before);
        std::cout << "NOTE tutorial_prediction_copy value-copy exercise only; S9 owns one independent int\n";
    }
    {
        Case c("tutorial_pending_counterexample");
        const int base=100, pendingA=-20, pendingB=-40;
        c.Check(base+pendingA+pendingB==40); c.Check(base+pendingB==60); c.Check(base!=base+pendingB);
        std::cout << "NOTE tutorial_pending_counterexample blind assignment 100 drops pending B; expected display 60; no replay implementation\n";
    }
    {
        Case c("tutorial_lastseq_counterexample");
        uint64_t last=0; std::string saved;
        auto wrong=[&](uint64_t id,const char* result) { if(id<=last)return saved;last=id;saved=result;return saved; };
        c.Check(wrong(2,"result2")=="result2"); c.Check(wrong(1,"result1")=="result2");
        c.Check(wrong(3,"result3")=="result3"); c.Check(wrong(2,"result2")=="result3");
        std::cout << "NOTE tutorial_lastseq_counterexample first-late and earlier-retry both get another request result; not DUT behavior\n";
    }
}
} // namespace
int main() {
    std::cout << "SKILL_CONTRACT version=1\n";
    LiteralProgression(); RejectionGates(); Identity(); TimeBoundaries();
    OracleSensitivity(); RepeatedStream(); TeachingCountermodels();
    std::cout << "RESULT version=1 cases=" << cases << " checks=" << checks << " fail=" << failures << '\n';
    return failures ? 1 : 0;
}
