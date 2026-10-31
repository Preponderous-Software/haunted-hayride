// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#include "ride/ride.h"

#include <algorithm>
#include <cmath>

namespace hayride {

namespace {

// A loop round the origin: radius r(theta) = radius + wobble * sin(lobes * theta + phase),
// starting due south (+z), where the barn is.
std::vector<Vec3> loop(int points, float radius, float wobble, int lobes, float phase) {
    std::vector<Vec3> cp;
    for (int i = 0; i < points; ++i) {
        const float th = 2 * kPi * static_cast<float>(i) / points;
        const float r = radius + wobble * std::sin(lobes * th + phase);
        cp.push_back({r * std::sin(th), 0.0f, r * std::cos(th)});
    }
    return cp;
}

constexpr float kStartGap = 22.0f;  // the first hiding place is this far from the barn
constexpr float kEndGap = 16.0f;    // and the last this far before it
constexpr float kAccelSeconds = 2.5f;

}  // namespace

int rideCount() { return 3; }

RideDef rideDef(int which) {
    RideDef d;
    switch (which) {
    case 0:
        d.name = "Pumpkin Patch";
        d.blurb = "A short, slow ride past the pumpkins. Great for little ones.";
        d.control = loop(12, 24.0f, 3.0f, 2, 0.6f);
        d.speed = 1.8f;
        d.gap = 10.5f;
        d.peekSeconds = 6.5f;
        d.cornWeight = 1.5f;
        d.scarecrowWeight = 0.5f;
        d.pumpkinWeight = 3.0f;
        d.seed = 0x5EED0001;
        break;
    case 1:
        d.name = "Scarecrow Lane";
        d.blurb = "Through the tall corn, where the scarecrows keep watch.";
        d.control = loop(16, 33.0f, 5.0f, 3, 0.4f);
        d.speed = 2.0f;
        d.gap = 11.5f;
        d.peekSeconds = 5.5f;
        d.cornWeight = 2.5f;
        d.scarecrowWeight = 2.0f;
        d.pumpkinWeight = 1.0f;
        d.seed = 0x5EED0002;
        break;
    default:
        d.name = "Moonlight Maze";
        d.blurb = "The long way round, twisting and turning. Lots of spooks!";
        d.control = loop(22, 40.0f, 8.0f, 5, 0.0f);
        d.speed = 2.1f;
        d.gap = 10.5f;
        d.peekSeconds = 5.0f;
        d.cornWeight = 3.0f;
        d.scarecrowWeight = 1.5f;
        d.pumpkinWeight = 1.5f;
        d.seed = 0x5EED0003;
        break;
    }
    return d;
}

Ride::Ride(int which) : Ride(rideDef(which)) {}

Ride::Ride(const RideDef& def) : def_(def), route_(def.control) {
    Rng rng(def.seed);
    const float L = route_.length();
    float s = kStartGap;
    int lastSide = 1;
    while (s < L - kEndGap) {
        Spot sp;
        // Mostly alternate sides, so the light swings back and forth.
        sp.side = rng.unit() < 0.7f ? -lastSide : lastSide;
        lastSide = sp.side;
        const float wsum = def.cornWeight + def.scarecrowWeight + def.pumpkinWeight;
        const float pick = rng.unit() * wsum;
        sp.hide = pick < def.cornWeight ? Hide::Corn
                  : pick < def.cornWeight + def.scarecrowWeight ? Hide::Scarecrow
                                                                : Hide::Pumpkin;
        const float k = rng.unit();
        switch (sp.hide) {
        case Hide::Corn: sp.kind = k < 0.5f ? Kind::Ghost : k < 0.8f ? Kind::Cat : Kind::Bat; break;
        case Hide::Scarecrow: sp.kind = k < 0.45f ? Kind::Ghost : k < 0.75f ? Kind::Bat : Kind::Cat; break;
        case Hide::Pumpkin: sp.kind = k < 0.75f ? Kind::Jack : Kind::Ghost; break;
        }
        sp.s = s;
        const Vec3 road = route_.at(s), t = route_.tangent(s);
        const Vec3 left{-t.z, 0.0f, t.x};
        const Vec3 out = left * static_cast<float>(sp.side);
        sp.outward = out;
        // How high the spook's centre is when out, by kind.
        const float hy = sp.kind == Kind::Cat ? 0.5f : sp.kind == Kind::Bat ? 2.3f : sp.kind == Kind::Jack ? 1.0f : 1.35f;
        sp.radius = sp.kind == Kind::Cat ? 0.45f : sp.kind == Kind::Bat ? 0.45f : 0.55f;
        switch (sp.hide) {
        case Hide::Corn:
            sp.base = road + out * 2.8f;
            sp.shown = road + out * 1.75f;  // out of the stalks, at the road's edge
            sp.shown.y = hy;
            sp.hidden = road + out * 4.3f;
            sp.hidden.y = sp.kind == Kind::Bat ? 1.2f : hy * 0.7f;
            break;
        case Hide::Scarecrow:
            sp.base = road + out * 3.3f;
            sp.hidden = sp.base + out * 0.8f;
            sp.hidden.y = sp.kind == Kind::Cat ? 0.4f : 1.1f;
            sp.shown = sp.base - t * 1.0f - out * 0.9f;
            sp.shown.y = sp.kind == Kind::Bat ? 2.4f : sp.kind == Kind::Cat ? 0.5f : 1.45f;
            break;
        case Hide::Pumpkin:
            sp.base = road + out * 3.0f;
            sp.hidden = sp.base + out * 0.2f;
            sp.hidden.y = 0.2f;
            sp.shown = sp.base;
            sp.shown.y = sp.kind == Kind::Jack ? 1.55f : 1.75f;
            break;
        }
        sp.wiggle = rng.range(0.0f, 2 * kPi);
        spots_.push_back(sp);
        s += def.gap * rng.range(0.8f, 1.25f);
    }
}

float Ride::expectedSeconds() const { return route_.length() / def_.speed + kAccelSeconds; }

int Ride::caught() const {
    int n = 0;
    for (const Spot& sp : spots_) n += sp.caught ? 1 : 0;
    return n;
}

Vec3 Ride::eye() const {
    Vec3 e = route_.at(s_);
    // A gentle bump as the wagon rolls.
    e.y = kEyeHeight + 0.035f * std::sin(s_ * 2.1f) + 0.02f * std::sin(s_ * 5.3f + 1.0f);
    return e;
}

Vec3 Ride::forward() const {
    Vec3 f = route_.at(s_ + 5.0f) - route_.at(s_);
    f.y = 0;
    f = normalize(f);
    f.y = -0.06f;  // a little down, towards the road and the stalks
    return normalize(f);
}

float Ride::outness(int i) const {
    const Spot& sp = spots_[i];
    switch (sp.phase) {
    case Phase::Waiting:
    case Phase::Resting:
    case Phase::Gone: return 0.0f;
    case Phase::Rising: return smooth(sp.phaseTime / kRiseSeconds);
    case Phase::Ducking: return 1.0f - smooth(sp.phaseTime / kDuckSeconds);
    case Phase::Peeking:
    case Phase::Caught: return 1.0f;
    }
    return 0.0f;
}

Vec3 Ride::spookCenter(int i) const {
    const Spot& sp = spots_[i];
    Vec3 c = lerp(sp.hidden, sp.shown, outness(i));
    if (sp.phase == Phase::Peeking || sp.phase == Phase::Caught) {
        // A friendly bob (bats flutter faster).
        const float rate = sp.kind == Kind::Bat ? 5.0f : 2.2f;
        c.y += 0.08f * std::sin(time_ * rate + sp.wiggle);
    }
    if (sp.phase == Phase::Caught) c.y += 0.6f * smooth(sp.phaseTime / kCaughtSeconds);
    return c;
}

bool Ride::catchable(int i) const {
    const Spot& sp = spots_[i];
    if (sp.caught) return false;
    return sp.phase == Phase::Rising || sp.phase == Phase::Peeking ||
           (sp.phase == Phase::Ducking && sp.phaseTime < kDuckSeconds * 0.6f);
}

bool Ride::lit(int i, const Aim& aim) const {
    if (!aim.on) return false;
    const Vec3 to = spookCenter(i) - aim.origin;
    const float d = length(to);
    if (d < 0.5f || d > 32.0f) return false;
    const float c = dot(normalize(aim.dir), to * (1.0f / d));
    const float angle = std::acos(std::max(-1.0f, std::min(1.0f, c)));
    return angle < kBeamHalfAngle + std::atan(spots_[i].radius / d);
}

void Ride::setPhase(Spot& sp, Phase p) {
    sp.phase = p;
    sp.phaseTime = 0;
}

void Ride::step(float dt, const Aim& aim) {
    if (!(dt > 0)) return;
    dt = std::min(dt, 600.0f);
    while (dt > 0) {
        const float h = std::min(dt, 1.0f / 60.0f);
        tick(h, aim);
        dt -= h;
    }
}

void Ride::tick(float dt, const Aim& aim) {
    time_ += dt;
    const float L = route_.length();
    if (!finished_) {
        // Pull away gently, roll at the ride's pace, ease into the barn.
        const float left = L - s_;
        float want = def_.speed * smooth(time_ / kAccelSeconds);
        if (left < 8.0f) want = std::max(0.35f, def_.speed * left / 8.0f);
        speed_ = want;
        s_ = std::min(L, s_ + speed_ * dt);
        if (s_ >= L - 0.05f) {
            s_ = L;
            finished_ = true;
            events_.push_back({Event::Finish, -1, Kind::Ghost, eye()});
        }
    }
    const Vec3 e = eye();
    const Vec3 f = normalize(Vec3{forward().x, 0.0f, forward().z});
    for (int i = 0; i < total(); ++i) {
        Spot& sp = spots_[i];
        sp.phaseTime += dt;
        const float ahead = sp.s - s_;
        switch (sp.phase) {
        case Phase::Waiting: {
            // Out when near, and in front of the rider (on a bend, wait until it is).
            Vec3 to = sp.shown - e;
            to.y = 0;
            const bool inView = dot(normalize(to), f) > 0.87f;  // within 30 degrees
            if (ahead <= kTriggerAhead && (inView || ahead < 6.0f) && !finished_) {
                setPhase(sp, Phase::Rising);
                sp.peeks++;
                events_.push_back({Event::Peek, i, sp.kind, sp.shown});
            }
            break;
        }
        case Phase::Rising:
            if (sp.phaseTime >= kRiseSeconds) setPhase(sp, Phase::Peeking);
            break;
        case Phase::Peeking: {
            // Out of the rider's view (the wagon has gone by): duck.
            Vec3 to = spookCenter(i) - e;
            to.y = 0;
            const bool behind = dot(normalize(to), f) < 0.42f;  // about 65 degrees off the way ahead
            if (sp.phaseTime >= def_.peekSeconds || behind || finished_) {
                setPhase(sp, Phase::Ducking);
                events_.push_back({Event::Duck, i, sp.kind, spookCenter(i)});
            }
            break;
        }
        case Phase::Ducking:
            if (sp.phaseTime >= kDuckSeconds) {
                // A second peek if the wagon is still well short of it.
                if (sp.peeks < 2 && ahead > 7.0f && !finished_) setPhase(sp, Phase::Resting);
                else setPhase(sp, Phase::Gone);
            }
            break;
        case Phase::Resting:
            if (sp.phaseTime >= kRestSeconds) {
                setPhase(sp, Phase::Rising);
                sp.peeks++;
                events_.push_back({Event::Peek, i, sp.kind, sp.shown});
            }
            break;
        case Phase::Caught:
            if (sp.phaseTime >= kCaughtSeconds) setPhase(sp, Phase::Gone);
            break;
        case Phase::Gone: break;
        }
        if (catchable(i)) {
            if (lit(i, aim)) sp.light += dt;
            else sp.light = std::max(0.0f, sp.light - dt * 0.5f);
            if (sp.light >= kCatchSeconds) {
                sp.caught = true;
                sp.caughtAt = time_;
                // Fully out for the celebration, wherever it was.
                const Vec3 at = spookCenter(i);
                setPhase(sp, Phase::Caught);
                events_.push_back({Event::Catch, i, sp.kind, at});
            }
        }
    }
}

std::vector<Event> Ride::takeEvents() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

Aim autopilotAim(const Ride& ride) {
    Aim a;
    a.origin = ride.eye();
    int best = -1;
    float bestScore = -1e9f;
    for (int i = 0; i < ride.total(); ++i) {
        if (!ride.catchable(i)) continue;
        const Spot& sp = ride.spots()[i];
        const float d = length(ride.spookCenter(i) - a.origin);
        const float score = sp.light * 100.0f - d;
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    if (best < 0) {
        a.dir = ride.forward();
        return a;
    }
    a.on = true;
    a.dir = normalize(ride.spookCenter(best) - a.origin);
    return a;
}

}  // namespace hayride
