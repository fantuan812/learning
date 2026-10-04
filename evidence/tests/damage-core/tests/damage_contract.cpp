// Independent constants/state traces. Never use the SUT to compute expected values.
#ifndef DAMAGE_SOURCE
#define DAMAGE_SOURCE "../src/damage_pipeline.cpp"
#endif
#include DAMAGE_SOURCE
#include <cstring>
#include <iostream>
#include <string>
#include <climits>
using namespace damage;
namespace {
int passed = 0, failed = 0;
void Check(bool ok, const std::string& name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (ok) ++passed; else ++failed;
}
bool Near(double a, double b, double rel = 1e-12) {
    return std::isfinite(a) && (b == 0 ? a == 0 : std::fabs((a-b)/b) <= rel);
}
bool SameDouble(double a, double b) {
    uint64_t x = 0, y = 0;
    static_assert(sizeof(x) == sizeof(a), "binary64 required");
    std::memcpy(&x, &a, sizeof(x)); std::memcpy(&y, &b, sizeof(y)); return x == y;
}
bool SameAttr(const Attribute& a, const Attribute& b) {
    if (!SameDouble(a.base,b.base) || a.mods.size()!=b.mods.size()) return false;
    for (size_t i=0;i<a.mods.size();++i)
        if (a.mods[i].op!=b.mods[i].op || !SameDouble(a.mods[i].value,b.mods[i].value) ||
            a.mods[i].source!=b.mods[i].source) return false;
    return true;
}
bool Same(const Combatant& a,const Combatant& b) {
    if (!SameAttr(a.attack,b.attack) || !SameAttr(a.damageBonus,b.damageBonus) ||
        !SameAttr(a.damageTaken,b.damageTaken) || !SameDouble(a.critChance,b.critChance) ||
        !SameDouble(a.critMultiplier,b.critMultiplier) || !SameDouble(a.armor,b.armor) ||
        !SameDouble(a.hp,b.hp) || !SameDouble(a.hpMax,b.hpMax) || a.alive!=b.alive || a.immune!=b.immune) return false;
    for (int i=0;i<3;++i) if (!SameDouble(a.shield[i],b.shield[i]) || !SameDouble(a.resistance[i],b.resistance[i])) return false;
    return true;
}
bool SameHit(const HitResult& a,const HitResult& b) {
    return a.status==b.status && a.crit==b.crit && a.blocked==b.blocked &&
        SameDouble(a.raw,b.raw) && SameDouble(a.mitigated,b.mitigated) && SameDouble(a.capped,b.capped) &&
        SameDouble(a.absorbed,b.absorbed) && SameDouble(a.requestedHpDamage,b.requestedHpDamage) &&
        SameDouble(a.toHp,b.toHp) && SameDouble(a.overkill,b.overkill) &&
        SameDouble(a.actualShieldLoss,b.actualShieldLoss) && SameDouble(a.shieldRoundingResidual,b.shieldRoundingResidual) &&
        SameDouble(a.roundingResidual,b.roundingResidual);
}
Combatant Attacker() { Combatant a; a.attack.base=1; return a; }
Combatant Target() { Combatant t; t.hp=t.hpMax=1000000; return t; }
DamageSpec Spec(double amount=100) { DamageSpec s; s.amount=amount; return s; }
const double max = std::numeric_limits<double>::max();
const double nan = std::numeric_limits<double>::quiet_NaN();
const double inf = std::numeric_limits<double>::infinity();
void Rejection(Combatant a,Combatant t,DamageSpec s,Status expected,const std::string& id) {
    const auto a0=a,t0=t; XorShift rng(717),control(717);
    const auto r=Settle(a,t,s,rng);
    const bool unchanged=Same(a,a0)&&Same(t,t0)&&rng.s==control.s;
    auto cleanA=Attacker(),cleanT=Target(),controlT=cleanT; cleanA.critChance=.4;
    const auto next=Settle(cleanA,cleanT,Spec(),rng), reference=Settle(cleanA,controlT,Spec(),control);
    Check(r.status==expected && r.blocked && unchanged && SameHit(next,reference) &&
          Same(cleanT,controlT)&&rng.s==control.s,id+"_atomic_and_following_hit");
}
void FieldsAndPolicies() {
    auto a=Attacker(),t=Target(); XorShift rng(1); auto s=Spec(); t.hp=40;
    auto r=Settle(a,t,s,rng);
    Check(r.status==Status::accepted&&r.raw==100&&r.mitigated==100&&r.capped==100&&
        r.absorbed==0&&r.requestedHpDamage==100&&r.toHp==40&&r.overkill==60&&t.hp==0&&!t.alive,"HP_actual40_overkill60");
    t=Target();t.hp=40;t.shield[0]=30;r=Settle(a,t,s,rng);
    Check(r.absorbed==30&&r.requestedHpDamage==70&&r.toHp==40&&r.overkill==30&&r.roundingResidual==0&&t.shield[0]==0,"HP_shield30_loss40_overkill30");
    t=Target();t.hp=40;r=Settle(a,t,Spec(40),rng);auto dead=t;auto seed=rng.s;
    auto again=Settle(a,t,s,rng);
    Check(r.toHp==40&&r.overkill==0&&!t.alive&&again.status==Status::dead&&Same(t,dead)&&rng.s==seed,"HP_exact_lethal_then_dead");
    t=Target();t.shield[0]=500;r=Settle(a,t,Spec(80),rng);
    Check(r.absorbed==80&&r.actualShieldLoss==80&&r.toHp==0&&t.shield[0]==420,"shield500_hit80");
    a.damageBonus.base=.2;t=Target();t.armor=100;t.resistance[0]=.25;t.damageTaken.base=.1;s.perHitCap=30;r=Settle(a,t,s,rng);
    Check(Near(r.raw,120)&&Near(r.mitigated,49.5)&&r.capped==30&&r.toHp==30,"stages_raw120_mitigated49_5_cap30");
    a=Attacker();t=Target();t.armor=100;t.shield[0]=50;s=Spec(1000);s.perHitCap=150;r=Settle(a,t,s,rng);
    Check(r.raw==1000&&r.mitigated==500&&r.capped==150&&r.absorbed==50&&r.toHp==100,"cap_position_armor_then_cap_then_shield");
    t=Target();t.armor=900;t.resistance[2]=.5;t.damageTaken.base=.25;t.shield[2]=20;t.shield[0]=7;t.shield[1]=9;s.type=2;r=Settle(a,t,s,rng);
    Check(r.mitigated==625&&r.capped==150&&r.toHp==130&&r.absorbed==20&&t.shield[0]==7&&t.shield[1]==9,"true_only_bypasses_armor");
    a.critChance=1;t=Target();t.hp=0;t.alive=false;t.shield[0]=50;dead=t;seed=rng.s;
    r=Settle(a,t,Spec(),rng);Check(r.status==Status::dead&&Same(t,dead)&&rng.s==seed,"dead_no_rng_or_shield");
    t=Target();t.immune=true;t.shield[0]=50;auto saved=t;r=Settle(a,t,Spec(),rng);
    Check(r.status==Status::immune&&Same(t,saved)&&rng.s==seed,"immune_no_rng_or_shield");
    t=Target();t.immune=true;t.shield[0]=50;s=Spec(nan);r=Settle(a,t,s,rng);
    Check(r.status==Status::invalid&&Same(t,saved)&&rng.s==seed,"invalid_precedes_immune");
    a=Attacker();t=Target();s=Spec();const auto first=Settle(a,t,s,rng),second=Settle(a,t,s,rng);
    Check(first.toHp==100&&second.toHp==100&&t.hp==999800,"serial_equal_calls_are_two_hits");
    for(int type=0;type<3;++type) {
        t=Target();t.shield[0]=t.shield[1]=t.shield[2]=100;s=Spec();s.type=type;r=Settle(a,t,s,rng);
        Check(r.absorbed==100&&r.toHp==0&&t.shield[type]==0&&t.shield[(type+1)%3]==100&&t.shield[(type+2)%3]==100,"typed_exact_break_"+std::to_string(type));
    }
    a=Attacker();t=Target();t.resistance[0]=1;seed=rng.s;r=Settle(a,t,Spec(),rng);
    Check(r.toHp==0&&!r.crit&&rng.s==seed,"crit0_zero_rng_full_resist");
    a.critChance=1;XorShift expected=rng;expected.next();r=Settle(a,t,Spec(),rng);
    Check(r.toHp==0&&r.crit&&rng.s==expected.s,"crit1_one_rng_even_full_resist");
    a=Attacker();t=Target();t.resistance[0]=-10;r=Settle(a,t,Spec(),rng);Check(r.toHp==100,"finite_resistance_clamp_low");
    t=Target();t.resistance[0]=10;r=Settle(a,t,Spec(),rng);Check(r.toHp==0,"finite_resistance_clamp_high");
    t=Target();t.armor=-5;r=Settle(a,t,Spec(),rng);Check(r.toHp==100,"negative_armor_no_reduction");
    a.attack.mods={{kAdd,-.5,1}};a.damageBonus.base=-.5;t=Target();t.damageTaken.base=-.5;
    r=Settle(a,t,Spec(),rng);Check(r.toHp==12.5,"legal_negative_debuffs");
    a=Attacker();a.attack.base=0;t=Target();r=Settle(a,t,Spec(),rng);Check(r.status==Status::accepted&&r.toHp==0,"zero_attack_valid");
    a.critChance=1;a.critMultiplier=0;a.attack.base=1;r=Settle(a,t,Spec(),rng);Check(r.status==Status::accepted&&r.crit&&r.toHp==0,"zero_crit_multiplier_valid");
}
void NumericAndWorkedExample() {
    auto a=Attacker(),t=Target(); XorShift rng(2); auto s=Spec();
    struct Case { double armor,k,expected; const char* id; };
    const double tiny=std::numeric_limits<double>::denorm_min();
    for(const auto& c: {Case{0,100,100,"zero"},Case{100,100,50,"equal"},Case{900,100,10,"900"},
        Case{1e9,100,9.9999990000001e-6,"1e9"},Case{1e20,100,1e-16,"1e20"},
        Case{max,max,50,"max_equal"},Case{50,100,66.6666666666666667,"less_branch"},
        Case{tiny,tiny,50,"denorm_equal"},Case{max,tiny,0,"factor_underflow"},Case{tiny,max,100,"small_armor"}}) {
        t=Target();t.armor=c.armor;s.armorConstantK=c.k;const auto r=Settle(a,t,s,rng);
        Check(r.status==Status::accepted&&Near(r.mitigated,c.expected),std::string("armor_")+c.id);
    }
    t=Target();t.hp=t.hpMax=1e20;s=Spec(1);auto r=Settle(a,t,s,rng);
    Check(r.requestedHpDamage==1&&r.toHp==0&&r.roundingResidual==1&&t.hp==1e20,"hp_rounding_positive_residual");
    t=Target();t.hp=t.hpMax=1;s=Spec(3*std::ldexp(1.0,-54));r=Settle(a,t,s,rng);
    Check(r.toHp==std::ldexp(1.0,-52)&&r.toHp>s.amount&&r.roundingResidual==-std::ldexp(1.0,-54),"hp_rounding_negative_residual_tie_even");
    t=Target();t.shield[0]=1e20;r=Settle(a,t,Spec(1),rng);
    Check(r.absorbed==1&&r.actualShieldLoss==0&&r.shieldRoundingResidual==1&&r.toHp==0,"shield_storage_rounding_reported");
    a.attack.base=1.8;a.critChance=1;a.critMultiplier=1.8;a.damageBonus.base=.2;
    t=Target();t.hp=800;t.armor=300;t.resistance[1]=.25;t.damageTaken.base=.1;t.shield[1]=500;
    s=Spec(240);s.type=1;s.perHitCap=400;r=Settle(a,t,s,rng);
    Check(Near(r.raw,933.12)&&Near(r.mitigated,192.456)&&Near(r.capped,192.456)&&Near(r.absorbed,192.456)&&
        Near(t.shield[1],307.544)&&r.toHp==0&&t.hp==800,"worked_table_shield500");
    t.shield[1]=0;t.hp=150;r=Settle(a,t,s,rng);
    Check(r.toHp==150&&Near(r.overkill,42.456)&&t.hp==0&&!t.alive,"worked_table_hp150");
}
void InvalidAndRanges() {
    for(double v: {0.0,-1.0,nan,inf,-inf}) Rejection(Attacker(),Target(),Spec(v),Status::invalid,"invalid_amount_"+std::to_string(v));
    for(int type: {-1,3,INT_MIN,INT_MAX}) {auto s=Spec();s.type=type;Rejection(Attacker(),Target(),s,Status::invalid,"invalid_type_"+std::to_string(type));}
    for(double v: {0.0,-1.0,nan,inf,-inf}) {auto s=Spec();s.armorConstantK=v;Rejection(Attacker(),Target(),s,Status::invalid,"invalid_K_"+std::to_string(v));}
    for(double v: {-1.0,nan,inf,-inf}) {auto s=Spec();s.perHitCap=v;Rejection(Attacker(),Target(),s,Status::invalid,"invalid_cap_"+std::to_string(v));}
    for(double v: {-1e-12,1+1e-12,nan,inf,-inf}) {auto a=Attacker();a.critChance=v;Rejection(a,Target(),Spec(),Status::invalid,"invalid_chance_"+std::to_string(v));}
    for(double v: {-1.0,nan,inf,-inf}) {auto a=Attacker();a.critMultiplier=v;Rejection(a,Target(),Spec(),Status::invalid,"invalid_crit_multiplier_"+std::to_string(v));}
    for(double v: {nan,inf,-inf}) {
        for(int field=0;field<3;++field) {auto a=Attacker();(field==0?a.attack:field==1?a.damageBonus:a.damageTaken).base=v;
            Rejection(a,Target(),Spec(),Status::invalid,"nonfinite_attribute_"+std::to_string(field)+"_"+std::to_string(v));}
        for(auto op: {kAdd,kMul,kOverride}) {auto a=Attacker();a.attack.mods={{op,v,1}};Rejection(a,Target(),Spec(),Status::invalid,"nonfinite_modifier_"+std::to_string(op)+"_"+std::to_string(v));}
        for(int i=0;i<3;++i) {auto t=Target();t.resistance[i]=v;Rejection(Attacker(),t,Spec(),Status::invalid,"invalid_resistance_"+std::to_string(i)+"_"+std::to_string(v));}
        auto t=Target();t.armor=v;Rejection(Attacker(),t,Spec(),Status::invalid,"nonfinite_armor_"+std::to_string(v));
    }
    for(int op: {-1,3,INT_MIN,INT_MAX}) {auto a=Attacker();a.attack.mods={{static_cast<ModOp>(op),1,1}};Rejection(a,Target(),Spec(),Status::invalid,"invalid_modifier_op_"+std::to_string(op));}
    for(int f=0;f<3;++f) {auto a=Attacker();(f==0?a.attack:f==1?a.damageBonus:a.damageTaken).base=-2;Rejection(a,Target(),Spec(),Status::invalid,"negative_evaluated_factor_"+std::to_string(f));}
    for(double v: {-1.0,nan,inf,-inf}) {
        auto t=Target();t.hp=v;Rejection(Attacker(),t,Spec(),Status::invalid,"invalid_hp_"+std::to_string(v));
        t=Target();t.hpMax=v;Rejection(Attacker(),t,Spec(),Status::invalid,"invalid_hpMax_"+std::to_string(v));
        for(int i=0;i<3;++i) {t=Target();t.shield[i]=v;Rejection(Attacker(),t,Spec(),Status::invalid,"invalid_shield_"+std::to_string(i)+"_"+std::to_string(v));}
    }
    auto t=Target();t.hpMax=0;Rejection(Attacker(),t,Spec(),Status::invalid,"hpMax_zero");
    t=Target();t.hp=t.hpMax+1;Rejection(Attacker(),t,Spec(),Status::invalid,"hp_above_max");
    t=Target();t.alive=false;Rejection(Attacker(),t,Spec(),Status::invalid,"dead_positive_hp");
    t=Target();t.hp=0;Rejection(Attacker(),t,Spec(),Status::invalid,"alive_zero_hp");
    auto a=Attacker();a.attack.base=max;Rejection(a,Target(),Spec(2),Status::range,"amount_attack_overflow");
    t=Target();t.resistance[0]=1;Rejection(a,t,Spec(2),Status::range,"overflow_before_full_resistance");
    a=Attacker();a.attack.mods={{kAdd,max,1},{kAdd,max,2}};Rejection(a,Target(),Spec(),Status::range,"modifier_add_overflow");
    a=Attacker();a.attack.mods={{kMul,max,1},{kMul,2,2}};Rejection(a,Target(),Spec(),Status::range,"modifier_mul_overflow");
    a=Attacker();a.attack.base=max;a.attack.mods={{kAdd,max,1}};Rejection(a,Target(),Spec(),Status::range,"base_add_overflow");
    a=Attacker();a.critChance=1;a.critMultiplier=max;Rejection(a,Target(),Spec(2),Status::range,"crit_overflow_rng_rollback");
    a=Attacker();a.critChance=1;a.damageBonus.base=max;Rejection(a,Target(),Spec(2),Status::range,"bonus_overflow_rng_rollback");
    a=Attacker();a.critChance=1;t=Target();t.damageTaken.base=max;Rejection(a,t,Spec(2),Status::range,"taken_overflow_rng_rollback");
    // Validation scans all fields, including unused target attack and attacker shield.
    a=Attacker();a.shield[2]=-1;Rejection(a,Target(),Spec(),Status::invalid,"attacker_state_validated");
    t=Target();t.attack.mods={{kAdd,nan,1}};Rejection(Attacker(),t,Spec(),Status::invalid,"unused_target_attribute_validated");
}
void AttributesAndReplay() {
    Attribute x;x.base=100;x.mods={{kAdd,50,1},{kMul,2,2},{kOverride,10,3}};
    Check(x.Value()==120,"attribute_grouped_120");std::reverse(x.mods.begin(),x.mods.end());Check(x.Value()==120,"attribute_categories_interleaved_120");
    x.mods={{kOverride,10,1},{kOverride,20,2}};Check(x.Value()==20,"last_override20");std::reverse(x.mods.begin(),x.mods.end());Check(x.Value()==10,"last_override10");
    x.base=0;x.mods={{kAdd,1e16,1},{kAdd,-1e16,2},{kAdd,1,3}};const double first=x.Value();std::reverse(x.mods.begin(),x.mods.end());
    Check(first==1&&x.Value()==0,"same_category_float_order_matters");
    auto a=Attacker(),a2=a,t=Target(),t2=t;a.critChance=a2.critChance=.35;a.damageBonus.base=a2.damageBonus.base=.15;
    t.armor=t2.armor=220;t.resistance[1]=t2.resistance[1]=.2;t.shield[1]=t2.shield[1]=500;
    XorShift rng(777),rng2(777);bool same=true;
    for(int i=0;i<5000;++i) {auto s=Spec(40+(i%60));s.type=i%3;auto r=Settle(a,t,s,rng),r2=Settle(a2,t2,s,rng2);
        same=same&&SameHit(r,r2)&&Same(a,a2)&&Same(t,t2)&&rng.s==rng2.s;}
    Check(same,"replay_5000_per_field_same_build_seed_order");
}
bool SameDots(const std::vector<DotTicks>& a,const std::vector<DotTicks>& b) {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)if(!SameDouble(a[i].perTick,b[i].perTick)||a[i].intervalUs!=b[i].intervalUs||
        a[i].remainingUs!=b[i].remainingUs||a[i].type!=b[i].type||a[i].phaseUs!=b[i].phaseUs)return false;
    return true;
}
void DotReject(std::vector<DotTicks> d,uint64_t dt,Status status,const std::string& id) {
    const auto before=d;auto r=AdvanceDotUnits(d,dt);Check(r.status==status&&r.ticks==0&&r.total==0&&SameDots(d,before),id+"_atomic");
}
void Dots() {
    for(const auto& parts: {std::vector<uint64_t>{1000000},std::vector<uint64_t>{500000,500000},
        std::vector<uint64_t>{250000,250000,250000,250000},std::vector<uint64_t>{600000,400000}}) {
        std::vector<DotTicks>d={{10,1000000,1000000,0}};double total=0;uint64_t count=0;
        for(auto dt:parts){auto r=AdvanceDotUnits(d,dt);total+=r.total;count+=r.ticks;}
        Check(total==10&&count==1&&d[0].remainingUs==0&&d[0].phaseUs==0,"dot_integer_partition_"+std::to_string(parts.size())+"_"+std::to_string(parts[0]));
    }
    std::vector<DotTicks>d={{10,1000000,1500000,0}};auto r=AdvanceDotUnits(d,1500000);auto again=AdvanceDotUnits(d,1000000);
    Check(r.total==10&&r.ticks==1&&again.total==0&&again.ticks==0&&d[0].phaseUs==0,"dot_terminal_half_discarded");
    d={{10,100000,300000,0}};r=AdvanceDotUnits(d,300000);Check(r.total==30&&r.ticks==3,"dot_decimal_units_three");
    for(uint64_t delta: {uint64_t(99),uint64_t(100),uint64_t(101)}) {
        d={{10,100,500,0}};r=AdvanceDotUnits(d,delta);Check(r.ticks==(delta<100?0U:1U)&&d[0].remainingUs==500-delta&&d[0].phaseUs==delta%100,"dot_boundary_"+std::to_string(delta));
    }
    d={{10,100,500,0,40}};auto before=d;r=AdvanceDotUnits(d,0);Check(r.total==0&&r.ticks==0&&SameDots(d,before),"dot_dt_zero_preserves_phase");
    r=AdvanceDotUnits(d,10000);Check(r.total==50&&r.ticks==5&&d[0].remainingUs==0&&d[0].phaseUs==0,"dot_overshoot_clamps_lifetime");
    d={{10,1000000,5000000,1},{3,500000,2000000,0}};r=AdvanceDotUnits(d,1000000);again=AdvanceDotUnits(d,10000000);
    Check(r.total==16&&r.total+again.total==62&&r.ticks==3&&again.ticks==6,"dot_original_two_first16_total62");
    // Independent event-timeline oracle: enumerate event times, not SUT divmod.
    for(uint64_t step: {uint64_t(1),uint64_t(7),uint64_t(19),uint64_t(1000)}) {
        d={{2,13,997,1}};uint64_t elapsed=0,next=13,totalCount=0;bool ok=true;
        while(elapsed<997){uint64_t end=std::min(uint64_t(997),elapsed+step),expected=0;
            while(next<=end){++expected;next+=13;}
            r=AdvanceDotUnits(d,step);ok=ok&&r.status==Status::accepted&&r.ticks==expected&&r.total==2*expected;
            totalCount+=r.ticks;elapsed=end;}
        Check(ok&&totalCount==76&&d[0].remainingUs==0,"dot_event_timeline_step_"+std::to_string(step));
    }
    d={{1,1,uint64_t(2147483648),0}};r=AdvanceDotUnits(d,2147483648ULL);Check(r.ticks==2147483648ULL&&r.total==2147483648.0,"dot_count_2pow31_wide");
    const uint64_t umax=std::numeric_limits<uint64_t>::max();
    d={{1,umax,umax,0,umax-2}};r=AdvanceDotUnits(d,umax-1);
    Check(r.ticks==1&&d[0].phaseUs==umax-3&&d[0].remainingUs==1,"dot_phase_plus_active_no_uint_overflow");
    DotReject({{1,1,umax,0},{1,1,1,1}},umax,Status::range,"dot_total_count_overflow");
    DotReject({{max,1,2,0}},2,Status::range,"dot_amount_multiply_overflow");
    DotReject({{max,1,1,0},{max,1,1,1}},1,Status::range,"dot_amount_sum_overflow");
    for(auto bad: {DotTicks{1,0,2,0},DotTicks{1,2,2,0,2},DotTicks{1,2,0,0,1},DotTicks{nan,1,2,0},
        DotTicks{inf,1,2,0},DotTicks{-1,1,2,0},DotTicks{0,1,2,0},DotTicks{1,1,2,-1},DotTicks{1,1,2,3}})
        DotReject({{1,1,10,0},bad},1,Status::invalid,"dot_invalid_state_"+std::to_string(bad.intervalUs)+"_"+std::to_string(bad.phaseUs)+"_"+std::to_string(bad.perTick)+"_"+std::to_string(bad.type));
    std::vector<Dot> legacy={{10,1,1,0}};int count=0;double sum=0;
    for(int i=0;i<4;++i)sum+=AdvanceDots(legacy,.25,&count);
    Check(sum==10&&legacy[0].remainingSec==0,"legacy_quarter_partition");
    legacy={{10,1,1,0}};sum=AdvanceDots(legacy,.6,nullptr);sum+=AdvanceDots(legacy,.4,nullptr);Check(sum==10,"legacy_point6_point4_null_count");
    legacy={{10,.1,.3,0}};sum=AdvanceDots(legacy,.3,&count);Check(sum==30&&count==3,"legacy_decimal_quantization");
    for(double dt: {-1.0,nan,inf,-inf}) {
        legacy={{10,1,1,0}};count=77;bool caught=false;try {AdvanceDots(legacy,dt,&count);}catch(const std::invalid_argument&){caught=true;}
        Check(caught&&count==77&&legacy[0].remainingSec==1&&legacy[0].clock.phaseUs==0,"legacy_invalid_dt_"+std::to_string(dt));
    }
    legacy={{1,.000001,2147.483648,0}};count=77;bool caught=false;
    try {AdvanceDots(legacy,2147.483648,&count);}catch(const std::overflow_error&){caught=true;}
    Check(caught&&count==77&&legacy[0].clock.remainingUs==2147483648ULL,"legacy_int_count_overflow_atomic");
    uint64_t units=77;auto status=SecondsToUnits(std::nextafter(18446744073709.552,inf),units);
    Check(status==Status::range&&units==77,"seconds_2pow64_boundary_rejected_before_cast");
    status=SecondsToUnits(0x1.0c6f7a0b5ed8ep+33,units);
    Check(status==Status::accepted&&units==9007199254740993ULL,"seconds_exact_binary64_oracle_above_2pow53");
    status=SecondsToUnits(.0000015,units);Check(status==Status::accepted&&units==2,"seconds_half_up_quantization");
    status=SecondsToUnits(.00000049,units);Check(status==Status::accepted&&units==0,"seconds_subunit_rounds_zero");
    // Counterexample: independently quantized deltas do not conserve decimal total.
    std::vector<Dot> one={{1,.000001,.000001,0}},split=one;
    double merged=AdvanceDots(one,.0000012,nullptr),divided=AdvanceDots(split,.0000004,nullptr);
    divided+=AdvanceDots(split,.0000004,nullptr);divided+=AdvanceDots(split,.0000004,nullptr);
    Check(merged==1&&divided==0&&split[0].clock.remainingUs==1,"seconds_partition_not_invariant_counterexample");
    auto a=Attacker(),t=Target(),aggregate=t;XorShift rng(3),rng2(3);auto s=Spec(100);s.perHitCap=150;
    const auto h1=Settle(a,t,s,rng),h2=Settle(a,t,s,rng);s.amount=200;const auto h=Settle(a,aggregate,s,rng2);
    Check(h1.toHp+h2.toHp==200&&h.toHp==150,"dot_raw_batch_not_per_tick_pipeline");
}
} // namespace
int main() {
    FieldsAndPolicies(); NumericAndWorkedExample(); InvalidAndRanges(); AttributesAndReplay(); Dots();
    std::cout<<"RESULT pass="<<passed<<" fail="<<failed<<'\n'; return failed?1:0;
}
