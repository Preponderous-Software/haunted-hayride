// One hayride: the wagon rolls round its loop at a steady pace, and at each
// hiding place along the way a friendly spook peeks out for a few seconds.
// Holding the flashlight on a peeking spook for a moment catches it (it
// giggles and pops). The ride ends back at the barn with a count of the
// spooks found. No engine and no GPU: everything here is plain, testable
// arithmetic, the same on every compiler.
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ride/math.h"
#include "ride/route.h"

namespace hayride {

// Where a spook hides.
enum class Hide { Corn, Scarecrow, Pumpkin };
// What it looks like.
enum class Kind { Ghost, Cat, Jack, Bat };

enum class Phase {
    Waiting,  // still hidden, the wagon not yet near
    Rising,   // coming out
    Peeking,  // out and waving: catch it now
    Ducking,  // going back in (still catchable for a moment)
    Resting,  // hidden for a beat before a second peek
    Caught,   // giggling and popping
    Gone      // done, caught or not
};

struct RideDef {
    std::string name;
    std::string blurb;
    std::vector<Vec3> control;  // the loop's control points (x, z)
    float speed = 2.0f;         // metres a second
    float gap = 12.0f;          // metres between hiding places, on average
    float peekSeconds = 5.0f;   // how long a spook stays out
    float cornWeight = 3, scarecrowWeight = 1, pumpkinWeight = 1;
    uint64_t seed = 1;
};

// The rides on the menu: 0 Pumpkin Patch (short), 1 Scarecrow Lane, 2 Moonlight Maze (long).
int rideCount();
RideDef rideDef(int which);

struct Spot {
    Hide hide = Hide::Corn;
    Kind kind = Kind::Ghost;
    float s = 0;        // where along the road it is
    int side = 1;       // +1 left of the road, -1 right
    Vec3 base;          // the prop's foot (scarecrow, pumpkin) or the corn edge
    Vec3 hidden, shown; // the spook's centre when hidden and when out
    Vec3 outward;       // unit vector from the road towards the spot
    float radius = 0.5f;
    Phase phase = Phase::Waiting;
    float phaseTime = 0;  // seconds in this phase
    int peeks = 0;
    float light = 0;      // seconds of flashlight gathered
    bool caught = false;
    float caughtAt = -1;  // ride time of the catch
    float wiggle = 0;     // a per-spook phase for its waving
};

struct Aim {
    bool on = false;  // false: the light rests (nothing is aimed)
    Vec3 origin;
    Vec3 dir;
};

struct Event {
    enum Type { Peek, Catch, Duck, Finish } type;
    int spot;
    Kind kind;
    Vec3 at;
};

constexpr float kTriggerAhead = 19.0f;   // metres ahead of the wagon a spook starts to peek
constexpr float kCatchSeconds = 0.3f;    // light needed to catch one
constexpr float kBeamHalfAngle = 0.13f;  // radians (about 7.5 degrees), plus the spook's own size
constexpr float kRiseSeconds = 0.6f, kDuckSeconds = 0.5f, kRestSeconds = 1.2f, kCaughtSeconds = 1.2f;
constexpr float kEyeHeight = 2.35f;

class Ride {
public:
    explicit Ride(int which);
    explicit Ride(const RideDef& def);

    // Moves the ride on by dt seconds (any dt; long ones are cut into steps).
    void step(float dt, const Aim& aim);

    const RideDef& def() const { return def_; }
    const Route& route() const { return route_; }
    const std::vector<Spot>& spots() const { return spots_; }
    float time() const { return time_; }
    float distance() const { return s_; }
    float progress() const { return clamp01(s_ / route_.length()); }
    bool finished() const { return finished_; }
    int caught() const;
    int total() const { return static_cast<int>(spots_.size()); }
    // Roughly how long the whole ride takes, in seconds.
    float expectedSeconds() const;

    // The rider's eye and where the wagon faces.
    Vec3 eye() const;
    Vec3 forward() const;
    // A spook's centre now (hidden ones are inside their hiding place).
    Vec3 spookCenter(int i) const;
    // How far out a spook is, 0 hidden .. 1 fully out.
    float outness(int i) const;
    // Is the light on this spook?
    bool lit(int i, const Aim& aim) const;
    bool catchable(int i) const;

    // Events since the last call (peeks, catches, the end).
    std::vector<Event> takeEvents();

private:
    void tick(float dt, const Aim& aim);
    void setPhase(Spot& sp, Phase p);

    RideDef def_;
    Route route_;
    std::vector<Spot> spots_;
    std::vector<Event> events_;
    float time_ = 0, s_ = 0, speed_ = 0;
    bool finished_ = false;
};

// Points the light at the most promising spook out right now (the one
// nearest to being caught, else the nearest): what the attract mode, the
// tests and the smoke test use. Off when nothing is out.
Aim autopilotAim(const Ride& ride);

}  // namespace hayride
