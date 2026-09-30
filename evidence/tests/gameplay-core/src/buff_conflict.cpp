// Gameplay core evidence: buff stacking / conflict / expiry / dispel semantics.
// Build: g++ -std=c++17 -O2 -o buff_conflict.exe buff_conflict.cpp
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>

namespace {

enum School { kSchoolNone = 0, kSchoolMagic = 1, kSchoolCurse = 2 };

struct BuffDef {
    int id;
    int group;          // same group == mutually exclusive by level
    int level;          // higher wins inside a group
    bool stackable;
    int maxStacks;
    double duration;
    int school;
    std::vector<int> excludesGroups;  // applying this buff removes those groups (mutual exclusion)
    int periodicChild = 0;            // re-applied on each periodic tick
};

struct ActiveBuff {
    int id = 0;
    int stacks = 0;
    double remaining = 0.0;
    int sourceId = 0;
    bool suppressed = false;
};

class BuffSystem {
public:
    void Register(const BuffDef& d) { defs_.push_back(d); }

    const BuffDef* Def(int id) const {
        for (const BuffDef& d : defs_) if (d.id == id) return &d;
        return nullptr;
    }

    std::string Apply(int id, int sourceId) {
        const BuffDef* d = Def(id);
        if (!d) return "unknown-buff";
        if (d->duration <= 0.0) return "expired-on-apply";

        // Mutual exclusion: remove excluded groups first.
        for (int g : d->excludesGroups) RemoveGroup(g);

        ActiveBuff* same = FindById(id);
        if (same) {
            same->remaining = d->duration;          // refresh duration (R1 / R8)
            same->sourceId = sourceId;              // source change keeps stacks (R8)
            if (d->stackable) {                     // stacking caps at maxStacks (R2)
                same->stacks = std::min(same->stacks + 1, d->maxStacks);
            }
            same->suppressed = false;
            return "refreshed";
        }

        // Same group, same level, different id -> replace (R3).
        for (ActiveBuff& a : active_) {
            const BuffDef* other = Def(a.id);
            if (!other) continue;
            if (other->group != d->group) continue;
            if (other->level == d->level) { a.id = id; a.stacks = 1; a.remaining = d->duration;
                                            a.sourceId = sourceId; a.suppressed = false;
                                            return "replaced"; }
            if (other->level < d->level) {          // higher suppresses lower (R5)
                a.suppressed = true;
                active_.push_back(ActiveBuff{id, 1, d->duration, sourceId, false});
                return "suppressed-lower";
            }
            return "blocked-by-higher";             // lower cannot override higher
        }

        active_.push_back(ActiveBuff{id, 1, d->duration, sourceId, false});
        return "applied";
    }

    // Periodic re-apply of a child buff: never stacks beyond cap, only refreshes (R9).
    std::string PeriodicReapply(int childId, int sourceId) {
        const BuffDef* d = Def(childId);
        if (!d) return "unknown-buff";
        ActiveBuff* a = FindById(childId);
        if (!a) return Apply(childId, sourceId);
        a->remaining = d->duration;
        a->sourceId = sourceId;
        if (d->stackable) a->stacks = std::min(a->stacks + 1, d->maxStacks);
        return "refreshed-no-extra-stack";
    }

    // Advance time, expire finished buffs, then resume suppressed lower buffs (R6).
    void Tick(double dt) {
        for (ActiveBuff& a : active_) {
            if (a.suppressed) continue;             // suppressed timers keep running elsewhere
            a.remaining -= dt;
        }
        // Suppressed buffs keep consuming their stored remaining time.
        for (ActiveBuff& a : active_) {
            if (a.suppressed) a.remaining -= dt;
        }
        active_.erase(std::remove_if(active_.begin(), active_.end(),
                                     [](const ActiveBuff& a) { return a.remaining <= 0.0; }),
                      active_.end());
        // Resume a suppressed buff only if its group has no active (non-suppressed) winner.
        for (ActiveBuff& a : active_) {
            if (!a.suppressed) continue;
            const BuffDef* d = Def(a.id);
            bool winner = false;
            for (const ActiveBuff& b : active_) {
                if (b.suppressed) continue;
                const BuffDef* od = Def(b.id);
                if (od && od->group == d->group) { winner = true; break; }
            }
            if (!winner) a.suppressed = false;      // remaining > 0 already guaranteed by erase
        }
    }

    int Dispel(int school) {
        const size_t before = active_.size();
        active_.erase(std::remove_if(active_.begin(), active_.end(),
                                     [&](const ActiveBuff& a) {
                                         const BuffDef* d = Def(a.id);
                                         return d && d->school == school;
                                     }),
                      active_.end());
        return static_cast<int>(before - active_.size());
    }

    ActiveBuff* FindById(int id) {
        for (ActiveBuff& a : active_) if (a.id == id) return &a;
        return nullptr;
    }
    int Count(int id) const {
        int n = 0; for (const ActiveBuff& a : active_) if (a.id == id) ++n; return n;
    }
    int GroupCount(int group) const {
        int n = 0;
        for (const ActiveBuff& a : active_) {
            const BuffDef* d = Def(a.id);
            if (d && d->group == group && !a.suppressed) ++n;
        }
        return n;
    }
    int Size() const { return static_cast<int>(active_.size()); }

private:
    void RemoveGroup(int group) {
        active_.erase(std::remove_if(active_.begin(), active_.end(),
                                     [&](const ActiveBuff& a) {
                                         const BuffDef* d = Def(a.id);
                                         return d && d->group == group;
                                     }),
                      active_.end());
    }
    std::vector<BuffDef> defs_;
    std::vector<ActiveBuff> active_;
};

int gPass = 0;
int gFail = 0;
void Check(bool ok, const char* name, const std::string& detail) {
    if (ok) { ++gPass; std::printf("PASS  %-52s %s\n", name, detail.c_str()); }
    else    { ++gFail; std::printf("FAIL  %-52s %s\n", name, detail.c_str()); }
}

std::string S(const std::string& v) { return v; }

void Build(BuffSystem& bs) {
    // id, group, level, stackable, maxStacks, duration, school, excludes, child
    bs.Register(BuffDef{100, 1, 1, false, 1, 10.0, kSchoolMagic, {}, 0});   // plain, non-stackable
    bs.Register(BuffDef{101, 1, 1, true,  5, 10.0, kSchoolMagic, {}, 0});   // stackable up to 5
    bs.Register(BuffDef{110, 2, 1, true,  3, 8.0,  kSchoolCurse, {}, 0});   // stackable, curse
    bs.Register(BuffDef{111, 2, 1, false, 1, 8.0,  kSchoolCurse, {}, 0});   // replaces 110 (same group+level)
    bs.Register(BuffDef{120, 3, 1, false, 1, 12.0, kSchoolMagic, {}, 0});   // low level of group 3
    bs.Register(BuffDef{121, 3, 2, false, 1, 6.0,  kSchoolMagic, {}, 0});   // high level of group 3
    bs.Register(BuffDef{130, 4, 1, false, 1, 5.0,  kSchoolMagic, {5}, 0});  // applies -> removes group 5
    bs.Register(BuffDef{131, 5, 1, false, 1, 20.0, kSchoolMagic, {}, 0});   // long buff in excluded group
    bs.Register(BuffDef{140, 6, 1, false, 1, 4.0,  kSchoolMagic, {}, 150}); // periodic parent
    bs.Register(BuffDef{150, 7, 1, false, 1, 4.0,  kSchoolMagic, {}, 0});   // periodic child
    bs.Register(BuffDef{160, 8, 1, false, 1, 0.0,  kSchoolMagic, {}, 0});   // zero duration
}

void TestRefresh() {
    BuffSystem bs; Build(bs);
    bs.Apply(100, 7);
    bs.Tick(4.0);
    bs.Apply(100, 7);
    ActiveBuff* a = bs.FindById(100);
    Check(a && a->remaining == 10.0 && a->stacks == 1,
          "R1 same buff refreshes duration, no extra stack",
          "remaining=" + std::to_string(a ? a->remaining : -1.0));
}

void TestStacking() {
    BuffSystem bs; Build(bs);
    for (int i = 0; i < 9; ++i) bs.Apply(101, 7);
    ActiveBuff* a = bs.FindById(101);
    Check(a && a->stacks == 5 && a->remaining == 10.0,
          "R2 stacking increments then caps at maxStacks",
          "stacks=" + std::to_string(a ? a->stacks : -1));
}

void TestReplace() {
    BuffSystem bs; Build(bs);
    bs.Apply(110, 7);
    bs.Apply(111, 7);
    Check(bs.Count(110) == 0 && bs.Count(111) == 1 && bs.GroupCount(2) == 1,
          "R3 same group+level different id replaces",
          "old=" + std::to_string(bs.Count(110)) + " new=" + std::to_string(bs.Count(111)));
}

void TestHigherSuppressesLower() {
    BuffSystem bs; Build(bs);
    bs.Apply(120, 7);          // low, 12s
    bs.Tick(4.0);              // remaining 8s
    bs.Apply(121, 7);          // high, 6s
    ActiveBuff* low = bs.FindById(120);
    ActiveBuff* high = bs.FindById(121);
    Check(low && low->suppressed && high && !high->suppressed,
          "R5 higher level suppresses lower in same group",
          "lowSuppressed=" + std::to_string(low ? low->suppressed : -1) +
          " highActive=" + std::to_string(high ? !high->suppressed : -1));
}

void TestResumeAfterExpiry() {
    BuffSystem bs; Build(bs);
    bs.Apply(120, 7);          // low 12s
    bs.Tick(2.0);              // low remaining 10s
    bs.Apply(121, 7);          // high 6s
    bs.Tick(6.5);              // high expires (low remaining 3.5s)
    ActiveBuff* low = bs.FindById(120);
    Check(low && !low->suppressed && low->remaining > 0.0 && bs.Count(121) == 0,
          "R6 lower resumes when higher expires and time remains",
          "resumed=" + std::to_string(low ? !low->suppressed : -1) +
          " remaining=" + std::to_string(low ? low->remaining : -1.0));
}

void TestNoResumeWhenExpired() {
    BuffSystem bs; Build(bs);
    bs.Apply(120, 7);          // low 12s
    bs.Tick(6.0);              // low remaining 6s
    bs.Apply(121, 7);          // high 6s
    bs.Tick(7.0);              // both run out
    Check(bs.Count(120) == 0 && bs.Count(121) == 0,
          "E2 suppressed buff expires instead of resuming",
          "low=" + std::to_string(bs.Count(120)) + " high=" + std::to_string(bs.Count(121)));
}

void TestMutualExclusion() {
    BuffSystem bs; Build(bs);
    bs.Apply(131, 7);          // group 5, 20s
    bs.Apply(130, 7);          // group 4, removes group 5
    Check(bs.Count(131) == 0 && bs.Count(130) == 1,
          "R4 exclusion group removes the other group on apply",
          "excluded=" + std::to_string(bs.Count(131)));
}

void TestDispel() {
    BuffSystem bs; Build(bs);
    bs.Apply(100, 7);          // magic
    bs.Apply(110, 7);          // curse
    bs.Apply(120, 7);          // magic
    bs.Apply(121, 7);          // magic (suppresses 120)
    const int removed = bs.Dispel(kSchoolMagic);
    Check(removed == 3 && bs.Count(110) == 1 && bs.Count(100) == 0 && bs.Count(121) == 0 && bs.Count(120) == 0,
          "R7 dispel by school removes magic incl. suppressed",
          "removed=" + std::to_string(removed) + " curseLeft=" + std::to_string(bs.Count(110)));
}

void TestSourceChange() {
    BuffSystem bs; Build(bs);
    bs.Apply(100, 7);
    bs.Tick(5.0);
    bs.Apply(100, 9);          // same buff, different caster
    ActiveBuff* a = bs.FindById(100);
    Check(a && a->sourceId == 9 && a->remaining == 10.0 && a->stacks == 1,
          "R8 re-apply from new source refreshes and rebinds source",
          "source=" + std::to_string(a ? a->sourceId : -1));
}

void TestPeriodicReapply() {
    BuffSystem bs; Build(bs);
    for (int i = 0; i < 6; ++i) bs.PeriodicReapply(150, 7);
    Check(bs.Count(150) == 1 && bs.FindById(150)->stacks == 1,
          "R9 periodic re-apply refreshes without stacking",
          "instances=" + std::to_string(bs.Count(150)) +
          " stacks=" + std::to_string(bs.FindById(150)->stacks));
}

void TestZeroDuration() {
    BuffSystem bs; Build(bs);
    const std::string r = bs.Apply(160, 7);
    Check(r == "expired-on-apply" && bs.Size() == 0,
          "E1 zero-duration buff is never inserted",
          "reason=" + r);
}

void TestLowerBlockedByHigher() {
    BuffSystem bs; Build(bs);
    bs.Apply(121, 7);          // high level
    const std::string r = bs.Apply(120, 7);  // low level
    Check(r == "blocked-by-higher" && bs.Count(120) == 0,
          "E3 lower level cannot overwrite an active higher level",
          "reason=" + r);
}

}  // namespace

int main() {
    std::printf("gameplay-core | buff conflict matrix test suite\n");
    std::printf("compiler=%s c++17 O2\n", __VERSION__);
    std::printf("----------------------------------------------------------------------\n");
    TestRefresh();
    TestStacking();
    TestReplace();
    TestHigherSuppressesLower();
    TestResumeAfterExpiry();
    TestNoResumeWhenExpired();
    TestMutualExclusion();
    TestDispel();
    TestSourceChange();
    TestPeriodicReapply();
    TestZeroDuration();
    TestLowerBlockedByHigher();
    std::printf("----------------------------------------------------------------------\n");
    std::printf("RESULT pass=%d fail=%d rules=R1-R9+E1-E3\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
