// What the rider sees: the night farm (ground, corn, barn, props), the wagon
// and the tractor pulling it, the spooks, the flashlight beam, the moon and
// stars, and the little pops and sparkles when a spook is found.
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#pragma once

#include <cstdint>
#include <vector>

#include "ride/ride.h"
#include "view/mesh.h"
#include "strata/strata.hpp"

namespace hayride {

// Decorations along the road that do nothing: jack-o'-lanterns, hay bales.
struct Decor {
    enum Type { Pumpkin, Lantern, Bale } type;
    Vec3 at;
    Vec3 face;  // towards the road
    float size;
};

class Scene {
public:
    explicit Scene(strata::Engine& engine);
    // The night's light (mode 1: sun below the horizon at elevation x; mode 2: flat ambient x).
    void setNight(int mode, float x);

    // Builds the static farm around this ride's road.
    void build(const Ride& ride);

    // Sets the camera for the drawable size; picking and projection below use it.
    void setCamera(const Ride& ride, int width, int height);
    // The world direction through a point on the screen (0..1 across and down).
    Vec3 rayThrough(float nx, float ny) const;
    // A world point's place on the screen (0..1); false when behind the eye.
    bool project(Vec3 p, float& nx, float& ny) const;
    Vec3 eye() const { return eye_; }
    Vec3 right() const { return right_; }
    Vec3 up() const { return up_; }
    Vec3 look() const { return fwd_; }

    // Draws one frame. beamDir is where the flashlight points (from the eye).
    void draw(const Ride& ride, Vec3 beamDir, bool beamOn, float clock);

private:
    void buildGround(const Ride& ride);
    void buildCorn(const Ride& ride);
    void buildBarn(const Ride& ride);
    void buildProps(const Ride& ride);
    void buildVehicles();
    void placeVehicles(const Ride& ride);
    void spooks(const Ride& ride, std::vector<StrataRenderItem>& items, std::vector<StrataPoint>& glow,
                std::vector<StrataPointLight>& lights, float clock);

    strata::Engine& engine_;
    StrataCamera camera_{};
    Vec3 eye_, fwd_{0, 0, -1}, right_{1, 0, 0}, up_{0, 1, 0};
    float tanY_ = 0.6f, tanX_ = 1.0f;
    int width_ = 1, height_ = 1;
    int nightMode_ = 1;

    uint32_t wagon_ = 0, tractor_ = 0, beam_ = 0;
    Vec3 barn_, barnFace_;
    std::vector<Decor> decor_;
    std::vector<StrataPoint> stars_, horizon_;
};

}  // namespace hayride
