// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#include "ride/route.h"

#include <algorithm>
#include <cmath>

namespace hayride {

namespace {

Vec3 catmull(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    auto f = [&](float a, float b, float c, float d) {
        return 0.5f * ((2 * b) + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3);
    };
    return {f(p0.x, p1.x, p2.x, p3.x), 0.0f, f(p0.z, p1.z, p2.z, p3.z)};
}

}  // namespace

Route::Route(const std::vector<Vec3>& cp) {
    const int n = static_cast<int>(cp.size());
    std::vector<Vec3> dense;
    const int perSeg = 64;
    for (int i = 0; i < n; ++i) {
        const Vec3 p0 = cp[(i + n - 1) % n], p1 = cp[i], p2 = cp[(i + 1) % n], p3 = cp[(i + 2) % n];
        for (int k = 0; k < perSeg; ++k) dense.push_back(catmull(p0, p1, p2, p3, static_cast<float>(k) / perSeg));
    }
    dense.push_back(dense.front());
    // Re-sample evenly by distance.
    std::vector<float> dcum(dense.size(), 0.0f);
    for (size_t i = 1; i < dense.size(); ++i) dcum[i] = dcum[i - 1] + hayride::length(dense[i] - dense[i - 1]);
    const float total = dcum.back();
    const int count = std::max(8, static_cast<int>(std::ceil(total)));
    const float step = total / count;
    size_t j = 0;
    for (int i = 0; i <= count; ++i) {
        const float want = std::min(total, i * step);
        while (j + 1 < dense.size() - 1 && dcum[j + 1] < want) ++j;
        const float span = dcum[j + 1] - dcum[j];
        const float t = span > 0 ? (want - dcum[j]) / span : 0.0f;
        pts_.push_back(lerp(dense[j], dense[j + 1], t));
        cum_.push_back(want);
    }
    length_ = total;
}

Vec3 Route::at(float s) const {
    s = std::fmod(s, length_);
    if (s < 0) s += length_;
    const float step = length_ / static_cast<float>(pts_.size() - 1);
    const size_t i = std::min(pts_.size() - 2, static_cast<size_t>(s / step));
    const float t = clamp01((s - cum_[i]) / step);
    return lerp(pts_[i], pts_[i + 1], t);
}

Vec3 Route::tangent(float s) const { return normalize(at(s + 1.5f) - at(s - 1.5f)); }

float Route::distanceTo(Vec3 p) const {
    float best = 1e9f;
    for (size_t i = 0; i + 1 < pts_.size(); ++i) {
        const Vec3 a = pts_[i], b = pts_[i + 1];
        const float abx = b.x - a.x, abz = b.z - a.z;
        const float l2 = abx * abx + abz * abz;
        float t = l2 > 0 ? ((p.x - a.x) * abx + (p.z - a.z) * abz) / l2 : 0.0f;
        t = clamp01(t);
        const float dx = p.x - (a.x + abx * t), dz = p.z - (a.z + abz * t);
        best = std::min(best, dx * dx + dz * dz);
    }
    return std::sqrt(best);
}

}  // namespace hayride
