// The ride's rules: every ride ends at the barn in a couple of minutes; every
// spook peeks out where the rider can see it; a steady light catches it and a
// light pointed elsewhere does not; a rider who catches nothing still gets to
// the end; and the same ride plays out the same way every time.
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "ride/ride.h"

using namespace hayride;

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

namespace {

struct Result {
    int caught = 0, total = 0, finishes = 0, peeked = 0;
    float seconds = 0;
    float worstPeekAngle = 0;  // degrees off the way ahead, at the moment of a peek
    std::vector<float> caughtAt;
};

Result play(int which, bool autopilot, float frame = 1.0f / 30.0f) {
    Ride r(which);
    Result out;
    out.total = r.total();
    std::set<int> peeked;
    for (int n = 0; n < 20000 && !r.finished(); ++n) {
        const Aim a = autopilot ? autopilotAim(r) : Aim{};
        r.step(frame, a);
        for (const Event& e : r.takeEvents()) {
            if (e.type == Event::Finish) out.finishes++;
            if (e.type == Event::Peek) {
                peeked.insert(e.spot);
                Vec3 to = e.at - r.eye();
                to.y = 0;
                Vec3 f = r.forward();
                f.y = 0;
                const float c = dot(normalize(to), normalize(f));
                const float deg = std::acos(std::max(-1.0f, std::min(1.0f, c))) * 180.0f / kPi;
                if (deg > out.worstPeekAngle) out.worstPeekAngle = deg;
            }
        }
    }
    // A few seconds after the end: nothing more happens.
    r.step(3.0f, Aim{});
    for (const Event& e : r.takeEvents()) out.finishes += e.type == Event::Finish ? 1 : 0;
    out.caught = r.caught();
    out.seconds = r.time();
    out.peeked = static_cast<int>(peeked.size());
    for (const Spot& sp : r.spots()) out.caughtAt.push_back(sp.caughtAt);
    return out;
}

void ridesHaveAShape() {
    CHECK(rideCount() == 3);
    float lastSeconds = 0;
    for (int i = 0; i < rideCount(); ++i) {
        Ride r(i);
        std::printf("%-15s %6.1f m  %5.1f s  %2d spooks\n", r.def().name.c_str(), r.route().length(), r.expectedSeconds(),
                    r.total());
        CHECK(!r.def().name.empty() && !r.def().blurb.empty());
        // A couple of minutes, the short ride shortest, the long ride longest.
        CHECK(r.expectedSeconds() > 60.0f && r.expectedSeconds() < 180.0f);
        CHECK(r.expectedSeconds() > lastSeconds);
        lastSeconds = r.expectedSeconds();
        CHECK(r.total() >= 8);
        // The road is a loop: it ends where it starts, at the barn.
        CHECK(length(r.route().at(0) - r.route().at(r.route().length())) < 0.01f);
        // Hiding places sit beside the road, and the spooks come out to its edge, never onto it.
        for (const Spot& sp : r.spots()) {
            CHECK(r.route().distanceTo(sp.shown) > 1.5f);  // clear of the wagon (1.2 m each side)
            CHECK(r.route().distanceTo(sp.base) > 2.5f);
        }
    }
}

void idleRiderStillGetsHome() {
    for (int i = 0; i < rideCount(); ++i) {
        const Result r = play(i, false);
        CHECK(r.caught == 0);
        CHECK(r.finishes == 1);
        CHECK(r.peeked == r.total);  // every spook came out at least once
        CHECK(r.seconds < 190.0f);
        // ...and where the rider could see it.
        std::printf("ride %d worst peek angle %.1f\n", i, r.worstPeekAngle);
        CHECK(r.worstPeekAngle < 40.0f);  // the view is at least 80 degrees wide
    }
}

void autopilotFindsAlmostAll() {
    for (int i = 0; i < rideCount(); ++i) {
        const Result r = play(i, true);
        std::printf("autopilot on ride %d: %d of %d\n", i, r.caught, r.total);
        CHECK(r.caught * 10 >= r.total * 9);
        CHECK(r.finishes == 1);
    }
}

void sameRideSameNight() {
    const Result a = play(2, true), b = play(2, true);
    CHECK(a.caught == b.caught);
    CHECK(a.caughtAt == b.caughtAt);
    // The frame rate does not change who is caught (steps are cut to 1/60 s).
    const Result c = play(1, true, 1.0f / 60.0f), d = play(1, true, 0.25f);
    CHECK(std::abs(c.caught - d.caught) <= 1);
}

void lightCatchesOnlyWhereItPoints() {
    Ride r(1);
    // Ride until the first spook is fully out.
    int target = -1;
    for (int n = 0; n < 4000 && target < 0; ++n) {
        r.step(1.0f / 30.0f, Aim{});
        for (int i = 0; i < r.total(); ++i)
            if (r.spots()[i].phase == Phase::Peeking) target = i;
    }
    CHECK(target >= 0);
    if (target < 0) return;
    // Aim 30 degrees away: not lit, never caught.
    Aim off;
    off.on = true;
    off.origin = r.eye();
    const Vec3 to = normalize(r.spookCenter(target) - off.origin);
    const float c = std::cos(0.52f), s = std::sin(0.52f);
    off.dir = normalize(Vec3{to.x * c - to.z * s, to.y, to.x * s + to.z * c});
    CHECK(!r.lit(target, off));
    Ride kept = r;
    kept.step(1.0f, off);
    CHECK(!kept.spots()[target].caught);
    // Aim at it: lit, and caught after a moment's light (not at once).
    Aim on;
    on.on = true;
    on.origin = r.eye();
    on.dir = to;
    CHECK(r.lit(target, on));
    r.step(0.1f, on);
    CHECK(!r.spots()[target].caught);
    for (int n = 0; n < 20 && !r.spots()[target].caught; ++n) {
        on.origin = r.eye();
        on.dir = normalize(r.spookCenter(target) - on.origin);
        r.step(1.0f / 30.0f, on);
    }
    CHECK(r.spots()[target].caught);
    bool caughtEvent = false;
    for (const Event& e : r.takeEvents()) caughtEvent |= e.type == Event::Catch && e.spot == target;
    CHECK(caughtEvent);
    CHECK(r.caught() == 1);
    // A caught spook can't be caught twice.
    for (int n = 0; n < 30; ++n) r.step(1.0f / 30.0f, on);
    CHECK(r.caught() == 1);
}

}  // namespace

int main() {
    ridesHaveAShape();
    idleRiderStillGetsHome();
    autopilotFindsAlmostAll();
    sameRideSameNight();
    lightCatchesOnlyWhereItPoints();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all ride checks passed\n");
    return 0;
}
