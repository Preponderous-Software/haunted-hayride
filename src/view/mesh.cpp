// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#include "view/mesh.h"

#include <cmath>

namespace hayride {

void MeshBuilder::vertex(Vec3 p, Vec3 n, Rgb c) {
    pos.insert(pos.end(), {p.x, p.y, p.z});
    nrm.insert(nrm.end(), {n.x, n.y, n.z});
    col.insert(col.end(), {c.r, c.g, c.b});
}

void MeshBuilder::quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Rgb color) {
    quad(a, b, c, d, color, normalize(cross(b - a, c - a)));
}

void MeshBuilder::quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Rgb color, Vec3 n) {
    const uint32_t base = static_cast<uint32_t>(vertices());
    vertex(a, n, color);
    vertex(b, n, color);
    vertex(c, n, color);
    vertex(d, n, color);
    idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void MeshBuilder::tri(Vec3 a, Vec3 b, Vec3 c, Rgb color) {
    const Vec3 n = normalize(cross(b - a, c - a));
    const uint32_t base = static_cast<uint32_t>(vertices());
    vertex(a, n, color);
    vertex(b, n, color);
    vertex(c, n, color);
    idx.insert(idx.end(), {base, base + 1, base + 2});
}

void MeshBuilder::box(Vec3 c, Vec3 h, Rgb color, Vec3 x, Vec3 y, Vec3 z) {
    const Vec3 X = x * h.x, Y = y * h.y, Z = z * h.z;
    auto P = [&](float sx, float sy, float sz) { return c + X * sx + Y * sy + Z * sz; };
    quad(P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), color);      // +z
    quad(P(1, -1, -1), P(-1, -1, -1), P(-1, 1, -1), P(1, 1, -1), color);  // -z
    quad(P(1, -1, 1), P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), color);      // +x
    quad(P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1), color);  // -x
    quad(P(-1, 1, 1), P(1, 1, 1), P(1, 1, -1), P(-1, 1, -1), color);      // +y
    quad(P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), color);  // -y
}

namespace {

void basisFor(Vec3 axis, Vec3& u, Vec3& v) {
    const Vec3 ref = std::fabs(axis.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    u = normalize(cross(axis, ref));
    v = cross(axis, u);
}

}  // namespace

void MeshBuilder::cylinder(Vec3 a, Vec3 b, float r, int seg, Rgb color) {
    const Vec3 axis = normalize(b - a);
    Vec3 u, v;
    basisFor(axis, u, v);
    for (int i = 0; i < seg; ++i) {
        const float t0 = 2 * kPi * i / seg, t1 = 2 * kPi * (i + 1) / seg;
        const Vec3 d0 = u * std::cos(t0) + v * std::sin(t0), d1 = u * std::cos(t1) + v * std::sin(t1);
        // Side: smooth-ish normals per facet.
        const Vec3 n = normalize(d0 + d1);
        quad(a + d0 * r, a + d1 * r, b + d1 * r, b + d0 * r, color, n);
        tri(b, b + d0 * r, b + d1 * r, color);
        tri(a, a + d1 * r, a + d0 * r, color);
    }
}

void MeshBuilder::cone(Vec3 a, Vec3 b, float r, int seg, Rgb color) {
    const Vec3 axis = normalize(b - a);
    Vec3 u, v;
    basisFor(axis, u, v);
    for (int i = 0; i < seg; ++i) {
        const float t0 = 2 * kPi * i / seg, t1 = 2 * kPi * (i + 1) / seg;
        const Vec3 d0 = u * std::cos(t0) + v * std::sin(t0), d1 = u * std::cos(t1) + v * std::sin(t1);
        tri(a + d0 * r, a + d1 * r, b, color);
        tri(a, a + d1 * r, a + d0 * r, color);
    }
}

uint32_t MeshBuilder::add(strata::Engine& engine) const {
    if (idx.empty()) return 0;
    StrataMesh m{};
    m.positions = pos.data();
    m.normals = nrm.data();
    m.colors = col.data();
    m.indices = idx.data();
    m.vertex_count = static_cast<int32_t>(vertices());
    m.index_count = static_cast<int32_t>(idx.size());
    m.color[0] = m.color[1] = m.color[2] = m.color[3] = 1.0f;
    return engine.addMesh(m);
}

uint32_t MeshBuilder::addFlagged(strata::Engine& engine, uint32_t flags, float alpha) const {
    if (idx.empty()) return 0;
    StrataTexturedMesh t{};
    t.mesh.positions = pos.data();
    t.mesh.normals = nrm.data();
    t.mesh.colors = col.data();
    t.mesh.indices = idx.data();
    t.mesh.vertex_count = static_cast<int32_t>(vertices());
    t.mesh.index_count = static_cast<int32_t>(idx.size());
    t.mesh.color[0] = t.mesh.color[1] = t.mesh.color[2] = 1.0f;
    t.mesh.color[3] = alpha;
    t.flags = flags;
    return engine.addTexturedMesh(t);
}

void MeshBuilder::clear() {
    pos.clear();
    nrm.clear();
    col.clear();
    idx.clear();
}

void frameMatrix(float m[16], Vec3 r, Vec3 u, Vec3 b, Vec3 o) {
    m[0] = r.x, m[1] = r.y, m[2] = r.z, m[3] = 0;
    m[4] = u.x, m[5] = u.y, m[6] = u.z, m[7] = 0;
    m[8] = b.x, m[9] = b.y, m[10] = b.z, m[11] = 0;
    m[12] = o.x, m[13] = o.y, m[14] = o.z, m[15] = 1;
}

}  // namespace hayride
