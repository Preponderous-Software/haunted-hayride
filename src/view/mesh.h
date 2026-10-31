// Builds static triangle meshes for Strata out of flat-shaded parts: quads,
// boxes, cylinders, double-sided cards.
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#pragma once

#include <cstdint>
#include <vector>

#include "ride/math.h"
#include "strata/strata.hpp"

namespace hayride {

struct Rgb {
    float r, g, b;
};

class MeshBuilder {
public:
    // One flat quad, a b c d counter-clockwise seen from the front.
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Rgb color);
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Rgb color, Vec3 normal);
    void tri(Vec3 a, Vec3 b, Vec3 c, Rgb color);
    // A box centred at c with half sizes h along the axes x, y, z (unit vectors).
    void box(Vec3 c, Vec3 h, Rgb color, Vec3 x = {1, 0, 0}, Vec3 y = {0, 1, 0}, Vec3 z = {0, 0, 1});
    // A closed cylinder from a to b.
    void cylinder(Vec3 a, Vec3 b, float radius, int segments, Rgb color);
    // A cone (closed) from base centre a, radius r, to apex b.
    void cone(Vec3 a, Vec3 b, float radius, int segments, Rgb color);

    bool empty() const { return idx.empty(); }
    size_t vertices() const { return pos.size() / 3; }
    // Adds the mesh to the engine (0 when empty).
    uint32_t add(strata::Engine& engine) const;
    // As a textured mesh with flags (STRATA_MESH_UNLIT, _TRANSLUCENT), alpha 0..1.
    uint32_t addFlagged(strata::Engine& engine, uint32_t flags, float alpha) const;
    void clear();

    std::vector<float> pos, nrm, col;
    std::vector<uint32_t> idx;

private:
    void vertex(Vec3 p, Vec3 n, Rgb c);
};

// A column-major model matrix from a frame: right, up, back (= -forward) and an origin.
void frameMatrix(float out[16], Vec3 right, Vec3 up, Vec3 back, Vec3 origin);

}  // namespace hayride
