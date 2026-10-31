// The wagon's road: a closed, smooth loop through the cornfield that starts
// and ends at the barn, measured by distance travelled.
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#pragma once

#include <vector>

#include "ride/math.h"

namespace hayride {

class Route {
public:
    // A closed Catmull-Rom loop through control points on the ground (y is
    // ignored), sampled every metre or so.
    explicit Route(const std::vector<Vec3>& controlPoints);

    float length() const { return length_; }
    // The point on the road `s` metres from the start (wraps around).
    Vec3 at(float s) const;
    // The unit direction of travel there.
    Vec3 tangent(float s) const;
    // The shortest distance from p to the road, on the ground plane.
    float distanceTo(Vec3 p) const;
    // The samples (for building the field around the road).
    const std::vector<Vec3>& samples() const { return pts_; }

private:
    std::vector<Vec3> pts_;   // evenly spaced samples, first == start
    std::vector<float> cum_;  // distance from the start to each sample
    float length_ = 0;
};

}  // namespace hayride
