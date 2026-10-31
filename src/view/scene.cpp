// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#include "view/scene.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace hayride {

namespace {

constexpr Vec3 kUp{0, 1, 0};
constexpr float kRoadHalf = 2.0f;    // the dirt road's half width
constexpr float kCornNear = 2.35f;   // corn starts this far from the road's middle
constexpr float kCornFar = 12.5f;    // and ends here (fog does the rest)
constexpr float kTractorAhead = 11.0f;  // the tractor pulls from this far ahead, on a long hitch

StrataRenderItem item(int kind, Vec3 p, Vec3 s, Rgb c) {
    StrataRenderItem it{};
    it.kind = kind;
    it.position[0] = p.x, it.position[1] = p.y, it.position[2] = p.z;
    it.scale[0] = s.x, it.scale[1] = s.y, it.scale[2] = s.z;
    it.color[0] = c.r, it.color[1] = c.g, it.color[2] = c.b, it.color[3] = 1;
    return it;
}

StrataPoint glowPoint(Vec3 p, float px, float world, Rgb c, float a, uint32_t flags = STRATA_POINT_SOFT) {
    StrataPoint g{};
    g.position[0] = p.x, g.position[1] = p.y, g.position[2] = p.z;
    g.size_pixels = px;
    g.world_diameter = world;
    g.color[0] = c.r, g.color[1] = c.g, g.color[2] = c.b, g.color[3] = a;
    g.flags = flags;
    return g;
}

StrataPointLight light(Vec3 p, float range, Rgb c, float intensity) {
    StrataPointLight l{};
    l.position[0] = p.x, l.position[1] = p.y, l.position[2] = p.z;
    l.range = range;
    l.color[0] = c.r, l.color[1] = c.g, l.color[2] = c.b;
    l.intensity = intensity;
    return l;
}

Rgb mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Rgb scale(Rgb a, float k) { return {a.r * k, a.g * k, a.b * k}; }

// Cheap hash noise for the ground.
float hash2(int x, int z) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

const Rgb kGhost{0.80f, 0.86f, 0.95f}, kGhostGlow{0.55f, 0.85f, 1.0f};
const Rgb kPumpkin{0.85f, 0.30f, 0.03f}, kStem{0.18f, 0.22f, 0.05f}, kLeaf{0.10f, 0.32f, 0.07f};
const Rgb kCandle{1.0f, 0.72f, 0.25f}, kBeam{1.0f, 0.93f, 0.66f};
const Rgb kInk{0.02f, 0.02f, 0.03f}, kCheek{0.95f, 0.45f, 0.55f};
const Rgb kCat{0.07f, 0.055f, 0.09f}, kCatEye{0.75f, 1.0f, 0.3f};
const Rgb kBat{0.16f, 0.10f, 0.20f}, kBatEye{1.0f, 0.55f, 0.8f};
const Rgb kStraw{0.62f, 0.48f, 0.20f}, kBurlap{0.52f, 0.42f, 0.26f}, kHat{0.16f, 0.10f, 0.06f};

}  // namespace

void Scene::setNight(int mode, float x) {
    nightMode_ = mode;
    if (mode == 2) {
        StrataLighting l = strata::lightingDefaults(STRATA_LIGHTING_SPACE);
        l.mode = STRATA_LIGHTING_SPACE;
        l.legacy_ambient = x;
        engine_.setLighting(l);
        return;
    }
    StrataLighting l = strata::lightingDefaults(STRATA_LIGHTING_SUN);
    l.cascade_count = 0;  // no sun shadows at night: cheaper on phones
    l.fog = 1;
    engine_.setLighting(l);
    const Vec3 toSun = normalize(Vec3{0.3f, x, 0.9f});
    const float d[3] = {toSun.x, toSun.y, toSun.z};
    engine_.setSunDirection(d);
}

Scene::Scene(strata::Engine& engine) : engine_(engine) {
    setNight(2, 0.42f);  // a storybook night: everything readable, lit warmer near lights
    // A sky full of stars, and the moon, far off (they never move).
    Rng rng(77);
    for (int i = 0; i < 220; ++i) {
        const float az = rng.range(0, 2 * kPi), el = std::asin(rng.range(0.05f, 1.0f));
        const Vec3 d{std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az)};
        const float b = rng.range(0.4f, 1.0f);
        stars_.push_back(glowPoint(d * 350.0f, rng.range(1.5f, 3.2f), 0, {b, b, b * 0.95f + 0.05f}, 0.9f, 0));
    }
    // A last glow of dusk along the horizon, purple above orange.
    for (int i = 0; i < 36; ++i) {
        const float az = 2 * kPi * i / 36;
        const Vec3 d{std::cos(az), 0.0f, std::sin(az)};
        horizon_.push_back(glowPoint(d * 330.0f + Vec3{0, 10.0f, 0}, 0, 150.0f, {0.42f, 0.16f, 0.36f}, 0.30f));
        horizon_.push_back(glowPoint(d * 330.0f + Vec3{0, -8.0f, 0}, 0, 90.0f, {0.75f, 0.32f, 0.18f}, 0.22f));
    }
    buildVehicles();
}

// ------------------------------------------------------------------ building

void Scene::build(const Ride& ride) {
    engine_.clearMeshes();
    wagon_ = tractor_ = beam_ = 0;
    decor_.clear();
    buildGround(ride);
    buildCorn(ride);
    buildBarn(ride);
    buildProps(ride);
    buildVehicles();
}

void Scene::buildGround(const Ride& ride) {
    float maxR = 0;
    for (const Vec3& p : ride.route().samples()) maxR = std::max(maxR, std::sqrt(p.x * p.x + p.z * p.z));
    const float half = std::ceil(maxR + 40.0f);
    const float cell = half > 100 ? 1.5f : 1.0f;
    const int n = static_cast<int>(2 * half / cell) + 1;
    std::vector<float> h(static_cast<size_t>(n) * n), c(static_cast<size_t>(n) * n * 3);
    const Rgb dirt{0.20f, 0.14f, 0.08f}, soil{0.07f, 0.065f, 0.035f}, grass{0.05f, 0.09f, 0.035f};
    for (int iz = 0; iz < n; ++iz) {
        for (int ix = 0; ix < n; ++ix) {
            const float x = -half + ix * cell, z = -half + iz * cell;
            const float r = std::sqrt(x * x + z * z);
            const float d = r < maxR + 16.0f ? ride.route().distanceTo({x, 0, z}) : 99.0f;
            const float jitter = hash2(ix, iz);
            // Low hills beyond the field, for a skyline.
            float y = 0.04f * jitter;
            const float hill = smooth((r - (maxR + 18.0f)) / 22.0f);
            y += hill * (4.0f + 3.0f * std::sin(std::atan2(z, x) * 3.0f + 1.3f));
            Rgb col = mix(soil, grass, hill);
            const float road = 1.0f - smooth((d - kRoadHalf) / 0.6f);
            col = mix(col, dirt, road);
            col = scale(col, 0.85f + 0.3f * jitter);
            if (d < kRoadHalf) y = 0.0f;
            const size_t k = static_cast<size_t>(iz) * n + ix;
            h[k] = y;
            c[k * 3] = col.r, c[k * 3 + 1] = col.g, c[k * 3 + 2] = col.b;
        }
    }
    StrataTerrainTile t{};
    t.origin[0] = -half, t.origin[1] = 0, t.origin[2] = -half;
    t.cell_size = cell;
    t.samples_x = t.samples_z = n;
    t.heights = h.data();
    t.colors = c.data();
    t.color[3] = 1;
    engine_.setTerrain(&t);
}

void Scene::buildCorn(const Ride& ride) {
    const Vec3 start = ride.route().at(0);
    const Vec3 out0 = normalize(Vec3{start.x, 0, start.z});
    barn_ = start + out0 * 12.0f;
    barnFace_ = out0 * -1.0f;
    // Clearings: the barn yard, and room round each scarecrow and big pumpkin.
    std::vector<std::pair<Vec3, float>> clear;
    clear.push_back({barn_, 13.0f});
    clear.push_back({start, 6.0f});
    for (const Spot& sp : ride.spots())
        if (sp.hide != Hide::Corn) clear.push_back({sp.base, 1.7f});
    float maxR = 0;
    for (const Vec3& p : ride.route().samples()) maxR = std::max(maxR, std::sqrt(p.x * p.x + p.z * p.z));
    const float half = maxR + kCornFar + 1;
    const float step = 1.0f;
    Rng rng(ride.def().seed ^ 0xC0A1);
    std::map<std::pair<int, int>, MeshBuilder> chunks;
    const Rgb low{0.09f, 0.075f, 0.03f}, high{0.42f, 0.34f, 0.11f}, green{0.16f, 0.24f, 0.06f};
    for (float z = -half; z <= half; z += step) {
        for (float x = -half; x <= half; x += step) {
            const Vec3 p{x + rng.range(-0.3f, 0.3f), 0, z + rng.range(-0.3f, 0.3f)};
            const float ang = rng.range(0, kPi);
            const float tall = rng.range(2.0f, 2.6f), wide = rng.range(0.8f, 1.05f), tint = rng.unit();
            const float r = std::sqrt(p.x * p.x + p.z * p.z);
            if (r > maxR + kCornFar + 0.5f) continue;
            const float d = ride.route().distanceTo(p);
            if (d < kCornNear || d > kCornFar) continue;
            bool skip = false;
            for (const auto& c : clear)
                if (length(p - c.first) < c.second) skip = true;
            if (skip) continue;
            MeshBuilder& mb = chunks[{static_cast<int>(std::floor(p.x / 24)), static_cast<int>(std::floor(p.z / 24))}];
            const Rgb top = mix(high, green, tint * 0.6f);
            // Two crossed, double-sided leafy cards: wide at the middle, a tassel at the top.
            static const float prof[4][2] = {{0.05f, 0.0f}, {0.36f, 0.42f}, {0.20f, 0.82f}, {0.03f, 1.0f}};
            for (int card = 0; card < 2; ++card) {
                const float a = ang + card * kPi * 0.5f;
                const Vec3 dir{std::cos(a), 0, std::sin(a)};
                const Vec3 n = cross(dir, kUp);
                for (int side = 0; side < 2; ++side) {
                    const Vec3 nn = normalize((side ? n * -1.0f : n) * 0.6f + kUp * 0.8f);
                    const uint32_t base = static_cast<uint32_t>(mb.vertices());
                    for (int k = 0; k < 4; ++k) {
                        const float hw = prof[k][0] * wide, y = prof[k][1] * tall;
                        const Rgb col = mix(low, top, prof[k][1]);
                        const Vec3 L = p - dir * hw + Vec3{0, y, 0}, R = p + dir * hw + Vec3{0, y, 0};
                        mb.pos.insert(mb.pos.end(), {L.x, L.y, L.z, R.x, R.y, R.z});
                        mb.nrm.insert(mb.nrm.end(), {nn.x, nn.y, nn.z, nn.x, nn.y, nn.z});
                        mb.col.insert(mb.col.end(), {col.r, col.g, col.b, col.r, col.g, col.b});
                    }
                    for (uint32_t k = 0; k < 3; ++k) {
                        const uint32_t l0 = base + 2 * k, r0 = l0 + 1, l1 = l0 + 2, r1 = l0 + 3;
                        if (side == 0) mb.idx.insert(mb.idx.end(), {l0, r0, r1, l0, r1, l1});
                        else mb.idx.insert(mb.idx.end(), {l0, r1, r0, l0, l1, r1});
                    }
                }
            }
        }
    }
    for (auto& kv : chunks) kv.second.add(engine_);
}

void Scene::buildBarn(const Ride& ride) {
    MeshBuilder mb;
    const Vec3 Z = barnFace_ * -1.0f;  // the barn's back
    const Vec3 X = normalize(cross(kUp, Z));
    const Vec3 c = barn_;
    const Rgb red{0.34f, 0.045f, 0.035f}, white{0.70f, 0.68f, 0.62f}, roof{0.10f, 0.09f, 0.10f}, door{0.20f, 0.05f, 0.04f};
    const float w = 4.5f, d = 4.0f, wall = 4.2f, peak = 6.8f;
    mb.box(c + Vec3{0, wall / 2, 0}, {w, wall / 2, d}, red, X, kUp, Z);
    // The roof: two slopes and two gables.
    const Vec3 fl = c - X * (w + 0.3f) - Z * (d + 0.3f) + Vec3{0, wall, 0}, fr = c + X * (w + 0.3f) - Z * (d + 0.3f) + Vec3{0, wall, 0};
    const Vec3 bl = c - X * (w + 0.3f) + Z * (d + 0.3f) + Vec3{0, wall, 0}, br = c + X * (w + 0.3f) + Z * (d + 0.3f) + Vec3{0, wall, 0};
    const Vec3 tf = c - Z * (d + 0.3f) + Vec3{0, peak, 0}, tb = c + Z * (d + 0.3f) + Vec3{0, peak, 0};
    mb.quad(fl, tf, tb, bl, roof);
    mb.quad(fr, br, tb, tf, roof);
    const Vec3 gl = c - X * w - Z * d + Vec3{0, wall, 0}, gr = c + X * w - Z * d + Vec3{0, wall, 0};
    mb.tri(gl, gr, c - Z * d + Vec3{0, peak - 0.2f, 0}, red);
    mb.tri(c + X * w + Z * d + Vec3{0, wall, 0}, c - X * w + Z * d + Vec3{0, wall, 0}, c + Z * d + Vec3{0, peak - 0.2f, 0}, red);
    // The big door with its white X, and the trim.
    const Vec3 front = c - Z * (d + 0.03f);
    mb.box(front + Vec3{0, 1.6f, 0}, {1.6f, 1.6f, 0.05f}, door, X, kUp, Z);
    const float diag = std::sqrt(1.6f * 1.6f + 1.6f * 1.6f);
    const Vec3 dx1 = normalize(X + kUp), dx2 = normalize(X * -1.0f + kUp);
    mb.box(front - Z * 0.06f + Vec3{0, 1.6f, 0}, {diag, 0.09f, 0.04f}, white, dx1, cross(Z, dx1) * -1.0f, Z);
    mb.box(front - Z * 0.06f + Vec3{0, 1.6f, 0}, {diag, 0.09f, 0.04f}, white, dx2, cross(Z, dx2) * -1.0f, Z);
    mb.box(front - Z * 0.07f + Vec3{0, 3.25f, 0}, {1.75f, 0.1f, 0.04f}, white, X, kUp, Z);
    mb.box(front - Z * 0.07f + X * 1.7f + Vec3{0, 1.6f, 0}, {0.1f, 1.65f, 0.04f}, white, X, kUp, Z);
    mb.box(front - Z * 0.07f - X * 1.7f + Vec3{0, 1.6f, 0}, {0.1f, 1.65f, 0.04f}, white, X, kUp, Z);
    // The hayloft window (lit from inside, in draw()).
    mb.box(front - Z * 0.02f + Vec3{0, 5.0f, 0}, {0.65f, 0.55f, 0.05f}, {0.9f, 0.6f, 0.2f}, X, kUp, Z);
    // Hay bales by the door, and two lantern posts at the road.
    const Rgb hay{0.50f, 0.38f, 0.13f};
    mb.box(front - Z * 1.0f + X * 2.8f + Vec3{0, 0.35f, 0}, {0.6f, 0.35f, 0.4f}, hay, X, kUp, Z);
    mb.box(front - Z * 1.0f + X * 3.3f + Vec3{0, 1.05f, 0}, {0.6f, 0.35f, 0.4f}, hay, X, kUp, Z);
    mb.box(front - Z * 1.1f - X * 3.0f + Vec3{0, 0.35f, 0}, {0.6f, 0.35f, 0.4f}, hay, X, kUp, Z);
    const Vec3 start = ride.route().at(0);
    for (int s = -1; s <= 1; s += 2) {
        const Vec3 post = start + Z * (s * 2.6f);
        mb.cylinder(post, post + Vec3{0, 2.4f, 0}, 0.07f, 6, {0.18f, 0.11f, 0.06f});
        decor_.push_back({Decor::Lantern, post + Vec3{0, 2.5f, 0}, barnFace_, 1.0f});
    }
    mb.add(engine_);
}

void Scene::buildProps(const Ride& ride) {
    MeshBuilder mb;
    Rng rng(ride.def().seed ^ 0xD3C0);
    for (const Spot& sp : ride.spots()) {
        if (sp.hide != Hide::Scarecrow) continue;
        const Vec3 Z = sp.outward;  // behind the scarecrow
        const Vec3 X = normalize(cross(kUp, Z));
        const Vec3 b = sp.base;
        const Rgb wood{0.22f, 0.14f, 0.07f};
        const Rgb shirt = rng.unit() < 0.5f ? Rgb{0.42f, 0.07f, 0.05f} : Rgb{0.08f, 0.14f, 0.36f};
        const Rgb denim{0.09f, 0.11f, 0.24f};
        mb.cylinder(b, b + Vec3{0, 2.25f, 0}, 0.07f, 6, wood);
        mb.box(b + Vec3{0, 1.68f, 0}, {0.9f, 0.05f, 0.05f}, wood, X, kUp, Z);
        mb.box(b + Vec3{0, 1.45f, 0} - Z * 0.02f, {0.32f, 0.36f, 0.15f}, shirt, X, kUp, Z);
        mb.box(b + Vec3{0, 1.66f, 0} + X * 0.6f, {0.3f, 0.11f, 0.12f}, shirt, X, kUp, Z);
        mb.box(b + Vec3{0, 1.66f, 0} - X * 0.6f, {0.3f, 0.11f, 0.12f}, shirt, X, kUp, Z);
        mb.box(b + Vec3{0, 1.66f, 0} + X * 0.96f, {0.07f, 0.07f, 0.07f}, kStraw, X, kUp, Z);
        mb.box(b + Vec3{0, 1.66f, 0} - X * 0.96f, {0.07f, 0.07f, 0.07f}, kStraw, X, kUp, Z);
        mb.box(b + Vec3{0, 0.78f, 0} + X * 0.15f, {0.12f, 0.32f, 0.12f}, denim, X, kUp, Z);
        mb.box(b + Vec3{0, 0.78f, 0} - X * 0.15f, {0.12f, 0.32f, 0.12f}, denim, X, kUp, Z);
        // A patch on the shirt.
        mb.box(b + Vec3{0, 1.35f, 0} - Z * 0.17f + X * 0.12f, {0.09f, 0.09f, 0.01f}, {0.55f, 0.45f, 0.1f}, X, kUp, Z);
    }
    mb.add(engine_);
    // Little things along the road: pumpkins (some carved and glowing), hay bales.
    const float L = ride.route().length();
    for (float s = 10.0f; s < L - 6.0f; s += rng.range(6.0f, 10.0f)) {
        bool nearSpot = false;
        for (const Spot& sp : ride.spots())
            if (std::fabs(sp.s - s) < 3.0f) nearSpot = true;
        const float pick = rng.unit();
        const int side = rng.unit() < 0.5f ? -1 : 1;
        if (nearSpot) continue;
        const Vec3 t = ride.route().tangent(s);
        const Vec3 out = Vec3{-t.z, 0, t.x} * static_cast<float>(side);
        const Vec3 at = ride.route().at(s) + out * rng.range(2.2f, 2.6f);
        if (pick < 0.62f) decor_.push_back({Decor::Pumpkin, at, out * -1.0f, rng.range(0.45f, 0.7f) * (rng.unit() < 0.55f ? -1.0f : 1.0f)});
        else decor_.push_back({Decor::Bale, at + out * 0.2f, out * -1.0f, rng.range(0.9f, 1.1f)});
    }
}

void Scene::buildVehicles() {
    // The wagon, in its own frame: x right, y up, z back (it drives towards -z).
    MeshBuilder w;
    const Rgb wood{0.30f, 0.17f, 0.08f}, rail{0.36f, 0.22f, 0.11f}, hay{0.55f, 0.42f, 0.15f}, wheel{0.12f, 0.07f, 0.04f};
    w.box({0, 0.9f, 0}, {1.15f, 0.08f, 2.0f}, wood);
    w.box({1.1f, 1.2f, 0}, {0.05f, 0.22f, 2.0f}, rail);
    w.box({-1.1f, 1.2f, 0}, {0.05f, 0.22f, 2.0f}, rail);
    w.box({0, 1.2f, -2.0f}, {1.15f, 0.22f, 0.05f}, rail);
    w.box({0.78f, 1.18f, 0.6f}, {0.28f, 0.2f, 0.7f}, scale(hay, 0.9f));
    w.box({-0.78f, 1.18f, 0.6f}, {0.28f, 0.2f, 0.7f}, scale(hay, 0.9f));
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            w.cylinder({sx * 1.2f, 0.45f, sz * 1.3f}, {sx * 1.38f, 0.45f, sz * 1.3f}, 0.45f, 10, wheel);
    wagon_ = w.add(engine_);
    // The tractor that pulls it, with the farmer up top.
    MeshBuilder t;
    const Rgb red{0.55f, 0.06f, 0.03f}, black{0.03f, 0.03f, 0.03f}, hub{0.75f, 0.6f, 0.1f};
    t.box({0, 1.05f, -0.4f}, {0.5f, 0.38f, 1.05f}, red);
    t.box({0, 1.5f, 0.55f}, {0.42f, 0.12f, 0.3f}, black);  // seat
    t.cylinder({0.3f, 1.4f, -0.9f}, {0.3f, 2.2f, -0.9f}, 0.05f, 6, black);
    for (int sx = -1; sx <= 1; sx += 2) {
        t.cylinder({sx * 0.62f, 0.75f, 0.55f}, {sx * 1.0f, 0.75f, 0.55f}, 0.75f, 14, black);
        t.cylinder({sx * 1.0f, 0.75f, 0.55f}, {sx * 1.03f, 0.75f, 0.55f}, 0.3f, 10, hub);
        t.cylinder({sx * 0.45f, 0.42f, -1.2f}, {sx * 0.7f, 0.42f, -1.2f}, 0.42f, 12, black);
    }
    const Rgb flannel{0.35f, 0.08f, 0.06f}, skin{0.55f, 0.38f, 0.28f};
    t.box({0, 1.95f, 0.55f}, {0.26f, 0.33f, 0.17f}, flannel);
    t.cylinder({0, 2.28f, 0.55f}, {0, 2.55f, 0.55f}, 0.14f, 10, skin);
    t.cylinder({0, 2.55f, 0.55f}, {0, 2.58f, 0.55f}, 0.36f, 12, kStraw);
    t.cylinder({0, 2.58f, 0.55f}, {0, 2.74f, 0.55f}, 0.17f, 10, kStraw);
    tractor_ = t.add(engine_);
    // The flashlight's beam: a unit cone from its tip at the origin to a base of radius 1 at z = -1.
    MeshBuilder b;
    const int seg = 20;
    for (int i = 0; i < seg; ++i) {
        const float a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
        b.tri({0, 0, 0}, {std::cos(a1), std::sin(a1), -1}, {std::cos(a0), std::sin(a0), -1}, kBeam);
    }
    beam_ = b.addFlagged(engine_, STRATA_MESH_UNLIT | STRATA_MESH_TRANSLUCENT, 0.055f);
}

// ------------------------------------------------------------------ camera

void Scene::setCamera(const Ride& ride, int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    const float aspect = static_cast<float>(width_) / height_;
    // At least 80 degrees across, so the spooks at the roadside stay in view on a tall phone.
    float vfov = 2 * std::atan(std::tan(40.0f * kPi / 180) / aspect) * 180 / kPi;
    vfov = std::max(58.0f, std::min(100.0f, vfov));
    tanY_ = std::tan(vfov * 0.5f * kPi / 180);
    tanX_ = tanY_ * aspect;
    eye_ = ride.eye();
    fwd_ = ride.forward();
    right_ = normalize(cross(fwd_, kUp));
    up_ = cross(right_, fwd_);
    const Vec3 target = eye_ + fwd_;
    const float e[3] = {eye_.x, eye_.y, eye_.z}, t[3] = {target.x, target.y, target.z}, u[3] = {0, 1, 0};
    camera_ = strata::cameraLookAt(e, t, u, vfov, aspect, 0.1f, 420.0f);
    engine_.setCamera(camera_);
}

Vec3 Scene::rayThrough(float nx, float ny) const {
    const float x = (2 * nx - 1) * tanX_, y = (1 - 2 * ny) * tanY_;
    return normalize(fwd_ + right_ * x + up_ * y);
}

bool Scene::project(Vec3 p, float& nx, float& ny) const {
    const Vec3 d = p - eye_;
    const float z = dot(d, fwd_);
    if (z < 0.05f) return false;
    nx = (dot(d, right_) / z / tanX_ + 1) * 0.5f;
    ny = (1 - dot(d, up_) / z / tanY_) * 0.5f;
    return true;
}

// ------------------------------------------------------------------ drawing

void Scene::placeVehicles(const Ride& ride) {
    const float s = ride.distance();
    auto place = [&](uint32_t id, float at, float back) {
        Vec3 f = ride.route().at(at + 1.0f) - ride.route().at(at - 1.0f);
        f.y = 0;
        f = normalize(f);
        const Vec3 r = normalize(cross(f, kUp));
        Vec3 o = ride.route().at(at) - f * back;
        o.y = 0.02f * std::sin(s * 2.1f);
        float m[16];
        frameMatrix(m, r, kUp, f * -1.0f, o);
        engine_.setMeshTransform(id, m);
    };
    // The rider sits up at the front of the wagon.
    if (wagon_) place(wagon_, s, 1.3f);
    if (tractor_) place(tractor_, s + kTractorAhead, 0.0f);
}

void Scene::spooks(const Ride& ride, std::vector<StrataRenderItem>& items, std::vector<StrataPoint>& glow,
                   std::vector<StrataPointLight>& lights, float clock) {
    for (int i = 0; i < ride.total(); ++i) {
        const Spot& sp = ride.spots()[i];
        if (sp.phase == Phase::Waiting || sp.phase == Phase::Resting || sp.phase == Phase::Gone) continue;
        Vec3 c = ride.spookCenter(i);
        Vec3 face = eye_ - c;
        face.y = 0;
        face = normalize(face);
        const Vec3 side = normalize(cross(kUp, face));
        float k = 1.3f;  // size: a little bigger than life, for small eyes
        float caughtT = 0;
        if (sp.phase == Phase::Caught) {
            caughtT = sp.phaseTime / kCaughtSeconds;
            k *= caughtT < 0.6f ? 1.0f + 0.35f * std::sin(caughtT / 0.6f * kPi * 0.5f)
                                : 1.35f * (1.0f - smooth((caughtT - 0.6f) / 0.35f));
            // Confetti and sparkles from where it was found.
            Rng rng(static_cast<uint64_t>(i) * 7919u + 13u);
            const float tt = sp.phaseTime;
            for (int p = 0; p < 22; ++p) {
                const Vec3 v{rng.range(-1.6f, 1.6f), rng.range(1.2f, 3.2f), rng.range(-1.6f, 1.6f)};
                const Vec3 at = c + v * tt + Vec3{0, -2.6f * tt * tt, 0};
                static const Rgb conf[5] = {{1.0f, 0.55f, 0.1f}, {0.7f, 0.35f, 1.0f}, {0.4f, 1.0f, 0.45f}, {1.0f, 0.95f, 0.4f}, {0.4f, 0.85f, 1.0f}};
                glow.push_back(glowPoint(at, 9.0f, 0.2f, conf[p % 5], 1.0f - clamp01(caughtT - 0.2f), 0));
            }
            glow.push_back(glowPoint(c, 0, 2.6f * (0.5f + caughtT), {1.0f, 0.95f, 0.7f}, 0.5f * (1.0f - caughtT)));
        }
        if (k <= 0.01f) continue;
        const bool lit = sp.light > 0.02f && sp.phase != Phase::Caught;
        const bool happy = lit || sp.phase == Phase::Caught;  // eyes squeeze shut in a grin
        const float wave = std::sin(clock * 9.0f + sp.wiggle);
        const Rgb glowColor = sp.kind == Kind::Jack ? kCandle : sp.kind == Kind::Cat ? kCatEye : sp.kind == Kind::Bat ? kBatEye : kGhostGlow;
        // Each spook carries a little light of its own, so it shows in the dark.
        lights.push_back(light(c + face * 0.9f + Vec3{0, 0.2f, 0}, 3.6f, glowColor, sp.kind == Kind::Cat ? 0.6f : 0.9f));
        auto eyes = [&](Vec3 head, float fwd, float apart, float size, Rgb col, bool glowing) {
            for (int s = -1; s <= 1; s += 2) {
                const Vec3 e = head + face * fwd + side * (apart * s * k);
                if (glowing) glow.push_back(glowPoint(e, 0, size * k * (happy ? 0.6f : 1.0f), col, 1.0f, 0));
                else items.push_back(item(STRATA_PRIMITIVE_SPHERE, e, Vec3{size, happy ? size * 0.25f : size, size} * k, col));
            }
        };
        switch (sp.kind) {
        case Kind::Ghost: {
            const Vec3 head = c + Vec3{0, 0.22f, 0} * k;
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, head, Vec3{0.72f, 0.72f, 0.72f} * k, kGhost));
            items.push_back(item(STRATA_PRIMITIVE_CYLINDER, c - Vec3{0, 0.18f, 0} * k, Vec3{0.7f, 0.36f, 0.7f} * k, kGhost));
            for (int b = 0; b < 4; ++b) {
                const float a = b * kPi * 0.5f + clock * 1.5f;
                const Vec3 at = c + (side * std::cos(a) + face * std::sin(a)) * (0.26f * k) - Vec3{0, 0.55f, 0} * k;
                items.push_back(item(STRATA_PRIMITIVE_SPHERE, at, Vec3{0.3f, 0.3f, 0.3f} * k, kGhost));
            }
            eyes(head + Vec3{0, 0.05f, 0} * k, 0.3f * k, 0.12f, 0.12f, kInk, false);
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, head + face * (0.33f * k) - Vec3{0, 0.1f, 0} * k,
                                 Vec3{0.1f, happy ? 0.12f : 0.07f, 0.1f} * k, kInk));
            for (int s = -1; s <= 1; s += 2)
                items.push_back(item(STRATA_PRIMITIVE_SPHERE, head + face * (0.28f * k) + side * (0.21f * s * k) - Vec3{0, 0.06f, 0} * k,
                                     Vec3{0.08f, 0.05f, 0.08f} * k, kCheek));
            // Hands: one waves.
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, c + side * (0.45f * k) + Vec3{0, 0.05f, 0} * k, Vec3{0.2f, 0.2f, 0.2f} * k, kGhost));
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, c - side * ((0.5f + 0.06f * wave) * k) + Vec3{0, 0.32f + 0.1f * wave, 0} * k,
                                 Vec3{0.2f, 0.2f, 0.2f} * k, kGhost));
            glow.push_back(glowPoint(c, 0, 2.0f * k, kGhostGlow, 0.22f));
            break;
        }
        case Kind::Cat: {
            items.push_back(item(STRATA_PRIMITIVE_CAPSULE, c, Vec3{0.42f, 0.28f, 0.42f} * k, kCat));
            const Vec3 head = c + Vec3{0, 0.42f, 0} * k + face * (0.06f * k);
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, head, Vec3{0.44f, 0.4f, 0.44f} * k, kCat));
            for (int s = -1; s <= 1; s += 2)
                items.push_back(item(STRATA_PRIMITIVE_CUBE, head + side * (0.13f * s * k) + Vec3{0, 0.2f, 0} * k,
                                     Vec3{0.1f, 0.14f, 0.1f} * k, kCat));
            eyes(head + Vec3{0, 0.03f, 0} * k, 0.22f * k, 0.09f, 0.11f, kCatEye, true);
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, head + face * (0.21f * k) - Vec3{0, 0.06f, 0} * k, Vec3{0.05f, 0.04f, 0.05f} * k, kCheek));
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, c + Vec3{0, 0.24f, 0} * k + face * (0.17f * k), Vec3{0.12f, 0.1f, 0.12f} * k, {0.9f, 0.4f, 0.05f}));
            // The tail swishes.
            for (int t = 1; t <= 5; ++t) {
                const float u = t / 5.0f;
                const Vec3 at = c - face * (0.25f * k) + side * (0.22f * u * std::sin(clock * 3.0f + sp.wiggle) * k) +
                                Vec3{0, (-0.15f + 0.55f * u), 0} * k;
                items.push_back(item(STRATA_PRIMITIVE_SPHERE, at, Vec3{0.1f, 0.1f, 0.1f} * k, kCat));
            }
            glow.push_back(glowPoint(c + Vec3{0, 0.3f, 0}, 0, 1.4f * k, {0.6f, 0.5f, 1.0f}, 0.12f));
            break;
        }
        case Kind::Jack: {
            const Vec3 head = c + Vec3{0, 0.3f, 0} * k;
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, head, Vec3{0.78f, 0.62f, 0.78f} * k, kPumpkin));
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, head + side * (0.22f * k), Vec3{0.45f, 0.6f, 0.5f} * k, scale(kPumpkin, 0.9f)));
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, head - side * (0.22f * k), Vec3{0.45f, 0.6f, 0.5f} * k, scale(kPumpkin, 0.9f)));
            items.push_back(item(STRATA_PRIMITIVE_CYLINDER, head + Vec3{0, 0.36f, 0} * k, Vec3{0.08f, 0.08f, 0.08f} * k, kStem));
            items.push_back(item(STRATA_PRIMITIVE_CAPSULE, c - Vec3{0, 0.32f, 0} * k, Vec3{0.32f, 0.22f, 0.32f} * k, kLeaf));
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, c + side * (0.36f * k) - Vec3{0, 0.25f, 0} * k, Vec3{0.16f, 0.16f, 0.16f} * k, kLeaf));
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, c - side * (0.4f * k) + Vec3{0, 0.02f + 0.1f * wave, 0} * k,
                                 Vec3{0.16f, 0.16f, 0.16f} * k, kLeaf));
            // A carved, glowing face.
            const Vec3 f = head + face * (0.4f * k);
            eyes(head + Vec3{0, 0.1f, 0} * k, 0.37f * k, 0.14f, 0.13f, {1.0f, 0.85f, 0.3f}, true);
            for (int m = -2; m <= 2; ++m) {
                const float y = -0.1f - 0.05f * (1.0f - std::fabs(static_cast<float>(m)) / 2.0f) * (happy ? 2.0f : 1.0f);
                glow.push_back(glowPoint(f + side * (0.065f * m * k) + Vec3{0, y, 0} * k - face * (0.04f * k * std::fabs(static_cast<float>(m)) / 2), 0,
                                         0.075f * k, {1.0f, 0.8f, 0.25f}, 1.0f, 0));
            }
            glow.push_back(glowPoint(head, 0, 1.8f * k, kCandle, 0.16f));
            break;
        }
        case Kind::Bat: {
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, c, Vec3{0.36f, 0.34f, 0.36f} * k, kBat));
            const float flap = std::sin(clock * 16.0f + sp.wiggle);
            for (int s = -1; s <= 1; s += 2) {
                items.push_back(item(STRATA_PRIMITIVE_CUBE, c + side * (0.36f * s * k) + Vec3{0, 0.06f + 0.1f * flap, 0} * k,
                                     Vec3{0.34f, 0.05f, 0.24f} * k, kBat));
                items.push_back(item(STRATA_PRIMITIVE_CUBE, c + side * (0.62f * s * k) + Vec3{0, 0.1f + 0.2f * flap, 0} * k,
                                     Vec3{0.22f, 0.04f, 0.18f} * k, kBat));
                items.push_back(item(STRATA_PRIMITIVE_CUBE, c + side * (0.1f * s * k) + Vec3{0, 0.2f, 0} * k, Vec3{0.07f, 0.12f, 0.07f} * k, kBat));
            }
            eyes(c + Vec3{0, 0.05f, 0} * k, 0.17f * k, 0.07f, 0.08f, kBatEye, true);
            items.push_back(item(STRATA_PRIMITIVE_CUBE, c + face * (0.17f * k) - Vec3{0, 0.08f, 0} * k, Vec3{0.025f, 0.05f, 0.02f} * k, {0.95f, 0.95f, 0.9f}));
            glow.push_back(glowPoint(c, 0, 1.4f * k, {0.8f, 0.5f, 1.0f}, 0.14f));
            break;
        }
        }
        // A sparkle ring while the light is on it: almost caught!
        if (lit) {
            const float f = clamp01(sp.light / kCatchSeconds);
            for (int p = 0; p < 6; ++p) {
                const float a = clock * 4.0f + p * kPi / 3;
                const Vec3 at = c + (side * std::cos(a) + kUp * std::sin(a)) * (0.75f * k) + face * 0.3f;
                glow.push_back(glowPoint(at, 4.0f, 0.1f, {1.0f, 0.95f, 0.5f}, 0.4f + 0.6f * f, 0));
            }
        }
    }
}

void Scene::draw(const Ride& ride, Vec3 beamDir, bool beamOn, float clock) {
    placeVehicles(ride);
    std::vector<StrataRenderItem> items;
    std::vector<StrataPoint> glow;
    std::vector<StrataPointLight> lights;
    items.reserve(512);
    glow.reserve(512);

    // Scarecrows' heads and hats; the big pumpkins.
    for (const Spot& sp : ride.spots()) {
        if (length(sp.base - eye_) > 60.0f) continue;
        const Vec3 face = sp.outward * -1.0f, side = normalize(cross(kUp, face));
        if (sp.hide == Hide::Scarecrow) {
            const Vec3 head = sp.base + Vec3{0, 2.08f, 0};
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, head, {0.44f, 0.46f, 0.44f}, kBurlap));
            items.push_back(item(STRATA_PRIMITIVE_CYLINDER, head + Vec3{0, 0.2f, 0}, {0.8f, 0.015f, 0.8f}, kHat));
            items.push_back(item(STRATA_PRIMITIVE_CYLINDER, head + Vec3{0, 0.32f, 0}, {0.36f, 0.12f, 0.36f}, kHat));
            items.push_back(item(STRATA_PRIMITIVE_CYLINDER, head + Vec3{0, 0.23f, 0}, {0.37f, 0.025f, 0.37f}, {0.6f, 0.1f, 0.05f}));
            for (int s = -1; s <= 1; s += 2)
                items.push_back(item(STRATA_PRIMITIVE_CUBE, head + face * 0.2f + side * (0.09f * s) + Vec3{0, 0.04f, 0}, {0.06f, 0.06f, 0.04f}, kInk));
            for (int m = -2; m <= 2; ++m)
                items.push_back(item(STRATA_PRIMITIVE_CUBE, head + face * 0.21f + side * (0.05f * m) + Vec3{0, -0.09f + 0.012f * std::abs(m), 0},
                                     {0.035f, 0.02f, 0.03f}, kInk));
            items.push_back(item(STRATA_PRIMITIVE_CUBE, head + face * 0.22f, {0.05f, 0.05f, 0.04f}, {0.75f, 0.3f, 0.05f}));
        } else if (sp.hide == Hide::Pumpkin) {
            const Vec3 b = sp.base;
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, b + Vec3{0, 0.5f, 0}, {1.3f, 0.95f, 1.3f}, kPumpkin));
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, b + Vec3{0, 0.5f, 0} + side * 0.32f, {0.75f, 0.92f, 0.95f}, scale(kPumpkin, 0.88f)));
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, b + Vec3{0, 0.5f, 0} - side * 0.32f, {0.75f, 0.92f, 0.95f}, scale(kPumpkin, 0.88f)));
            items.push_back(item(STRATA_PRIMITIVE_CYLINDER, b + Vec3{0, 1.02f, 0}, {0.12f, 0.12f, 0.12f}, kStem));
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, b + side * 0.95f + face * 0.3f + Vec3{0, 0.22f, 0}, {0.5f, 0.4f, 0.5f}, kPumpkin));
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, b - side * 0.9f + face * 0.4f + Vec3{0, 0.17f, 0}, {0.38f, 0.32f, 0.38f}, kPumpkin));
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, b - side * 0.6f + face * 0.7f + Vec3{0, 0.05f, 0}, {0.5f, 0.08f, 0.3f}, kLeaf));
        }
    }
    // Decorations, near ones only.
    struct Glowing {
        Vec3 at;
        float d;
        bool lantern;
    };
    std::vector<Glowing> candles;
    for (const Decor& d : decor_) {
        const float dist = length(d.at - eye_);
        if (dist > 55.0f) continue;
        const Vec3 side = normalize(cross(kUp, d.face));
        if (d.type == Decor::Pumpkin) {
            const float s = std::fabs(d.size);
            const Vec3 c = d.at + Vec3{0, s * 0.42f, 0};
            items.push_back(item(STRATA_PRIMITIVE_SPHERE, c, {s * 1.25f, s * 0.85f, s * 1.25f}, kPumpkin));
            items.push_back(item(STRATA_PRIMITIVE_CYLINDER, c + Vec3{0, s * 0.45f, 0}, {s * 0.12f, s * 0.1f, s * 0.12f}, kStem));
            if (d.size < 0) {  // carved: a candle inside
                const Vec3 f = c + d.face * (s * 0.6f);
                for (int e = -1; e <= 1; e += 2) glow.push_back(glowPoint(f + side * (0.18f * s * e) + Vec3{0, 0.1f * s, 0}, 0, 0.16f * s, {1.0f, 0.8f, 0.3f}, 1, 0));
                for (int m = -2; m <= 2; ++m)
                    glow.push_back(glowPoint(f + side * (0.09f * s * m) + Vec3{0, -0.13f * s - 0.04f * s * (1 - std::fabs(static_cast<float>(m)) / 2), 0}, 0, 0.09f * s,
                                             {1.0f, 0.75f, 0.25f}, 1, 0));
                glow.push_back(glowPoint(c + d.face * (s * 0.4f), 0, 1.6f * s, kCandle, 0.18f));
                candles.push_back({c + d.face * 0.8f + Vec3{0, 0.3f, 0}, dist, false});
            }
        } else if (d.type == Decor::Bale) {
            items.push_back(item(STRATA_PRIMITIVE_CUBE, d.at + Vec3{0, 0.36f, 0}, Vec3{1.1f, 0.7f, 1.1f} * d.size, {0.48f, 0.37f, 0.13f}));
        } else {
            items.push_back(item(STRATA_PRIMITIVE_CUBE, d.at, {0.22f, 0.3f, 0.22f}, {0.12f, 0.1f, 0.08f}));
            glow.push_back(glowPoint(d.at, 0, 0.22f, {1.0f, 0.85f, 0.45f}, 1.0f, 0));
            glow.push_back(glowPoint(d.at, 0, 2.2f, kCandle, 0.25f));
            candles.push_back({d.at - d.face * 0.3f, dist, true});
        }
    }
    spooks(ride, items, glow, lights, clock);

    // The flashlight: a beam from the rider's hand, a pool of light where it lands.
    const Vec3 hand = eye_ + right_ * 0.45f - up_ * 0.5f + fwd_ * 0.6f;
    float reach = 26.0f;
    if (beamDir.y < -0.02f) reach = std::min(reach, (eye_.y - 0.05f) / -beamDir.y);
    reach = std::max(reach, 2.5f);
    // Stop at a spook the light is on.
    for (int i = 0; i < ride.total(); ++i) {
        if (!ride.catchable(i)) continue;
        const Vec3 to = ride.spookCenter(i) - eye_;
        const float along = dot(to, beamDir);
        if (along > 0 && along < reach && length(to - beamDir * along) < ride.spots()[i].radius + 0.3f) reach = along;
    }
    const Vec3 spot = eye_ + beamDir * reach;
    if (beamOn && beam_) {
        Vec3 dir = normalize(spot - hand);
        const Vec3 r = normalize(cross(dir, kUp));
        const Vec3 u = cross(r, dir);
        const float len = length(spot - hand), rad = len * 0.075f;
        float m[16];
        frameMatrix(m, r * rad, u * rad, dir * -len, hand);
        engine_.setMeshTransform(beam_, m);
        lights.push_back(light(spot - beamDir * 0.8f + Vec3{0, 0.3f, 0}, 7.0f, {1.0f, 0.92f, 0.7f}, 2.4f));
        glow.push_back(glowPoint(spot, 0, 1.6f + reach * 0.12f, kBeam, 0.22f));
        glow.push_back(glowPoint(hand + dir * 0.05f, 0, 0.09f, {1, 1, 0.9f}, 1.0f, 0));
    } else if (beam_) {
        float m[16];
        frameMatrix(m, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, -50, 0});
        engine_.setMeshTransform(beam_, m);
    }

    // Lights of the farm: the barn's hayloft, the tractor's headlamps, the wagon's lantern,
    // and the nearest carved pumpkins and lantern posts.
    const Vec3 loft = barn_ + barnFace_ * 4.2f + Vec3{0, 5.0f, 0};
    if (length(loft - eye_) < 70) {
        lights.push_back(light(loft + barnFace_ * 1.5f, 10.0f, {1.0f, 0.7f, 0.35f}, 1.3f));
        glow.push_back(glowPoint(loft, 0, 3.0f, {1.0f, 0.7f, 0.3f}, 0.25f));
    }
    {
        const float s = ride.distance() + kTractorAhead;
        Vec3 f = ride.route().at(s + 1) - ride.route().at(s - 1);
        f.y = 0;
        f = normalize(f);
        const Vec3 nose = ride.route().at(s) + f * 1.7f + Vec3{0, 1.1f, 0};
        lights.push_back(light(nose + f * 4.0f, 12.0f, {1.0f, 0.95f, 0.8f}, 1.3f));
        const Vec3 r = normalize(cross(f, kUp));
        glow.push_back(glowPoint(nose + r * 0.35f, 0, 0.18f, {1, 1, 0.9f}, 1.0f, 0));
        glow.push_back(glowPoint(nose - r * 0.35f, 0, 0.18f, {1, 1, 0.9f}, 1.0f, 0));
        const Vec3 tail = ride.route().at(s) + f * 0.75f + Vec3{0, 1.2f, 0};
        glow.push_back(glowPoint(tail + r * 0.4f, 0, 0.12f, {1.0f, 0.15f, 0.1f}, 1.0f, 0));
        glow.push_back(glowPoint(tail - r * 0.4f, 0, 0.12f, {1.0f, 0.15f, 0.1f}, 1.0f, 0));
        // A lantern hung on the wagon's front rail lights the corn close by.
        const Vec3 lamp = eye_ + fwd_ * 2.2f - up_ * 0.6f - right_ * 0.9f;
        lights.push_back(light(lamp, 7.0f, {1.0f, 0.75f, 0.4f}, 0.9f));
    }
    std::sort(candles.begin(), candles.end(), [](const Glowing& a, const Glowing& b) { return a.d < b.d; });
    for (size_t i = 0; i < candles.size() && i < 6; ++i)
        lights.push_back(light(candles[i].at, candles[i].lantern ? 7.0f : 4.0f, {1.0f, 0.62f, 0.25f}, candles[i].lantern ? 1.2f : 0.9f));
    if (lights.size() > 24) lights.resize(24);
    engine_.setPointLights(lights.data(), lights.size());

    // Moonlight and stars; fireflies drifting about the wagon.
    const Vec3 moonDir = normalize(Vec3{-0.35f, 0.45f, -0.8f});
    glow.push_back(glowPoint(eye_ + moonDir * 300.0f, 0, 26.0f, {1.0f, 0.97f, 0.85f}, 1.0f, 0));
    glow.push_back(glowPoint(eye_ + moonDir * 300.0f, 0, 70.0f, {0.7f, 0.75f, 1.0f}, 0.18f));
    for (int f = 0; f < 26; ++f) {
        const float a = f * 2.399f + clock * (0.15f + 0.02f * (f % 5)), r = 3.5f + (f * 37 % 11);
        const Vec3 at = eye_ + Vec3{std::cos(a) * r, -1.0f + 0.6f * std::sin(clock * 0.7f + f), std::sin(a) * r};
        const float blink = 0.5f + 0.5f * std::sin(clock * (1.3f + 0.1f * (f % 7)) + f);
        glow.push_back(glowPoint(at, 3.0f, 0.06f, {0.85f, 1.0f, 0.4f}, 0.25f + 0.65f * blink));
    }

    StrataFrameDesc fd{};
    fd.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH | (nightMode_ == 1 ? STRATA_CLEAR_SKY : 0u);
    fd.clear_color[0] = 0.025f, fd.clear_color[1] = 0.02f, fd.clear_color[2] = 0.075f, fd.clear_color[3] = 1;
    engine_.beginFrame(fd);
    if (!items.empty()) engine_.submitPrimitives(items.data(), items.size());
    std::vector<StrataPoint> sky(horizon_);
    if (nightMode_ == 1) sky.clear();
    sky.insert(sky.end(), stars_.begin(), stars_.end());
    for (StrataPoint& p : sky) {
        p.position[0] += eye_.x;
        p.position[1] += eye_.y;
        p.position[2] += eye_.z;
    }
    engine_.submitPoints(sky.data(), sky.size());
    if (!glow.empty()) engine_.submitPoints(glow.data(), glow.size());

    // "+1" over each spook found in the last second or so.
    std::vector<StrataText> texts;
    const float px = std::max(16.0f, std::round(height_ / 22.0f / 8.0f) * 8.0f);
    for (int i = 0; i < ride.total(); ++i) {
        const Spot& sp = ride.spots()[i];
        if (sp.phase != Phase::Caught) continue;
        float nx, ny;
        if (!project(ride.spookCenter(i) + Vec3{0, 0.9f + sp.phaseTime * 0.5f, 0}, nx, ny)) continue;
        StrataText t{};
        t.x = nx * width_ - px;
        t.y = ny * height_ - px * 0.5f;
        t.pixel_size = px;
        t.flags = STRATA_TEXT_SHADOW;
        t.color[0] = 1.0f, t.color[1] = 0.85f, t.color[2] = 0.3f, t.color[3] = 1.0f - clamp01(sp.phaseTime / kCaughtSeconds - 0.3f);
        t.utf8 = "+1";
        texts.push_back(t);
    }
    if (!texts.empty()) engine_.submitText(texts.data(), texts.size());
    engine_.endFrame();
}

}  // namespace hayride
