// Haunted Hayride in a browser: Strata's WebGL 2 backend (Emscripten). The
// page (web/shell.html) owns the menus, the score card, the sound and the
// pointer; this file owns the ride and the picture.
//
// The page tells the game where the pointer is as a fraction of the canvas
// (0..1 across and down), so the device pixel ratio never matters here.
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include "ride/ride.h"
#include "strata/strata.hpp"
#include "view/scene.h"

using namespace hayride;

// Calls into the page (web/shell.html).
EM_JS(void, js_sound, (int type, const char* kind, float pan), {
    if (Module.hayrideSound) Module.hayrideSound(type, UTF8ToString(kind), pan);
});
EM_JS(void, js_renderer_failed, (const char* why), {
    if (Module.onRendererFailed) Module.onRendererFailed(UTF8ToString(why));
});

namespace {

enum class Mode { Title, Riding, Done };

struct Game {
    std::unique_ptr<strata::Engine> engine;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<Ride> ride;
    Mode mode = Mode::Title;
    int which = 1;
    bool autopilot = false, paused = false;
    double last = 0;
    float clock = 0, doneFor = 0, timeScale = 1;
    // Where the light points on the screen: where the player wants it, and where it is (eased).
    float wantX = 0.5f, wantY = 0.62f, aimX = 0.5f, aimY = 0.62f;
    std::string json;
};

Game* g = nullptr;

const char* kindName(Kind k) {
    switch (k) {
    case Kind::Ghost: return "ghost";
    case Kind::Cat: return "cat";
    case Kind::Jack: return "jack";
    case Kind::Bat: return "bat";
    }
    return "ghost";
}

void startRide(int which, Mode mode) {
    g->which = std::max(0, std::min(rideCount() - 1, which));
    g->ride = std::make_unique<Ride>(g->which);
    g->scene->build(*g->ride);
    g->mode = mode;
    g->doneFor = 0;
    g->paused = false;
    g->aimX = g->wantX = 0.5f;
    g->aimY = g->wantY = 0.62f;
}

// The best spook to shine on, as a place on the screen (for the attract mode and the smoke test).
bool autoTarget(float& nx, float& ny) {
    const Aim a = autopilotAim(*g->ride);
    if (!a.on) return false;
    return g->scene->project(a.origin + a.dir * 10.0f, nx, ny);
}

void frame(void*) {
    StrataEvent e;
    while (g->engine->pollEvent(e)) {
    }
    const double now = emscripten_get_now() / 1000.0;
    float dt = static_cast<float>(std::min(0.25, std::max(0.0, now - g->last)));
    g->last = now;
    int w = 1, h = 1;
    g->engine->drawableSize(w, h);
    g->scene->setCamera(*g->ride, w, h);

    const bool robot = g->mode == Mode::Title || g->autopilot;
    if (robot) {
        float nx, ny;
        if (autoTarget(nx, ny)) {
            g->wantX = nx;
            g->wantY = ny;
        }
    }
    const float ease = std::min(1.0f, dt * (robot ? 7.0f : 22.0f));
    g->aimX += (g->wantX - g->aimX) * ease;
    g->aimY += (g->wantY - g->aimY) * ease;
    Aim aim;
    aim.on = g->mode != Mode::Done;
    aim.origin = g->scene->eye();
    aim.dir = g->scene->rayThrough(g->aimX, g->aimY);

    if (!g->paused) {
        const float step = dt * (g->mode == Mode::Riding ? g->timeScale : 1.0f);
        g->clock += step;
        g->ride->step(step, aim);
        for (const Event& ev : g->ride->takeEvents()) {
            if (g->mode != Mode::Riding) continue;  // the attract ride is silent
            float nx = 0.5f, ny = 0.5f;
            g->scene->project(ev.at, nx, ny);
            const int type = ev.type == Event::Peek ? 0 : ev.type == Event::Catch ? 1 : ev.type == Event::Duck ? 2 : 3;
            js_sound(type, kindName(ev.kind), std::max(-1.0f, std::min(1.0f, nx * 2 - 1)));
        }
        if (g->ride->finished()) {
            g->doneFor += step;
            if (g->mode == Mode::Riding && g->doneFor > 1.4f) g->mode = Mode::Done;
            // The attract ride goes round again.
            if (g->mode == Mode::Title && g->doneFor > 2.0f) startRide(g->which, Mode::Title);
        }
    }
    try {
        g->scene->draw(*g->ride, aim.dir, aim.on, g->clock);
    } catch (const std::exception& ex) {
        // A frame Strata refused: say why (once a second at most) and carry on with the next.
        static double lastReport = -10;
        if (now - lastReport > 1.0) {
            lastReport = now;
            std::fprintf(stderr, "hayride: a frame was not drawn: %s\n", ex.what());
        }
    }
    g->engine->present();
}

}  // namespace

extern "C" {

// Where the player points (0..1 across and down the canvas).
EMSCRIPTEN_KEEPALIVE void hayride_aim(double nx, double ny) {
    if (!g) return;
    g->wantX = static_cast<float>(std::max(0.0, std::min(1.0, nx)));
    g->wantY = static_cast<float>(std::max(0.0, std::min(1.0, ny)));
}
EMSCRIPTEN_KEEPALIVE void hayride_start(int which) {
    if (g) startRide(which, Mode::Riding);
}
// Back to the menu: the attract ride rolls behind it.
EMSCRIPTEN_KEEPALIVE void hayride_title() {
    if (g) startRide(1, Mode::Title);
}
EMSCRIPTEN_KEEPALIVE void hayride_pause(int on) {
    if (g) g->paused = on != 0;
}
// For the smoke test and the curious: the light finds spooks by itself, and the ride runs faster.
EMSCRIPTEN_KEEPALIVE void hayride_auto(int on) {
    if (g) g->autopilot = on != 0;
}
EMSCRIPTEN_KEEPALIVE void hayride_speed(double k) {
    if (g) g->timeScale = static_cast<float>(std::max(0.1, std::min(20.0, k)));
}
// For tuning the look: see Scene::setNight.
EMSCRIPTEN_KEEPALIVE void hayride_night(int mode, double x) {
    if (g) g->scene->setNight(mode, static_cast<float>(x));
}
EMSCRIPTEN_KEEPALIVE void hayride_resize(double w, double h) {
    if (!g || w < 1 || h < 1) return;
    g->engine->resize(static_cast<int32_t>(w), static_cast<int32_t>(h));
}

EMSCRIPTEN_KEEPALIVE const char* hayride_rides() {
    std::string o = "[";
    for (int i = 0; i < rideCount(); ++i) {
        const Ride r(i);
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s{\"seconds\":%d,\"spooks\":%d,\"name\":\"", i ? "," : "",
                      static_cast<int>(r.expectedSeconds() + 0.5f), r.total());
        o += buf;
        o += r.def().name + "\",\"blurb\":\"" + r.def().blurb + "\"}";
    }
    o += "]";
    static std::string keep;
    keep = o;
    return keep.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* hayride_status() {
    if (!g || !g->ride) return "{}";
    const Ride& r = *g->ride;
    const char* mode = g->mode == Mode::Title ? "title" : g->mode == Mode::Riding ? "riding" : "done";
    int out = 0, lit = 0;
    for (int i = 0; i < r.total(); ++i) {
        if (!r.catchable(i)) continue;
        ++out;
        lit += r.spots()[i].light > 0.02f ? 1 : 0;
    }
    float tx = -1, ty = -1;
    {
        float nx, ny;
        if (autoTarget(nx, ny)) tx = nx, ty = ny;
    }
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "{\"mode\":\"%s\",\"ride\":%d,\"caught\":%d,\"total\":%d,\"progress\":%.4f,\"seconds\":%.2f,"
                  "\"expected\":%.1f,\"paused\":%s,\"auto\":%s,\"out\":%d,\"lit\":%d,\"target\":[%.4f,%.4f],\"aim\":[%.4f,%.4f]}",
                  mode, g->which, r.caught(), r.total(), static_cast<double>(r.progress()), static_cast<double>(r.time()),
                  static_cast<double>(r.expectedSeconds()), g->paused ? "true" : "false", g->autopilot ? "true" : "false", out, lit,
                  static_cast<double>(tx), static_cast<double>(ty), static_cast<double>(g->aimX), static_cast<double>(g->aimY));
    g->json = buf;
    return g->json.c_str();
}

}  // extern "C"

int main() {
    static Game game;
    g = &game;
    double cw = 0, ch = 0;
    emscripten_get_element_css_size("#canvas", &cw, &ch);
    if (cw < 1 || ch < 1) {
        cw = 1280;
        ch = 720;
    }
    strata::EngineDesc desc;
    desc.surface = STRATA_SURFACE_WINDOW;
    desc.width = static_cast<int32_t>(cw);
    desc.height = static_cast<int32_t>(ch);
    desc.title = "Haunted Hayride";
    try {
        g->engine = strata::Engine::create(desc);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "hayride: cannot start the renderer: %s\n", ex.what());
        js_renderer_failed(ex.what());
        return 1;
    }
    g->scene = std::make_unique<Scene>(*g->engine);
    startRide(1, Mode::Title);
    g->last = emscripten_get_now() / 1000.0;
    emscripten_run_script("if (Module.onGameReady) Module.onGameReady()");
    emscripten_set_main_loop_arg(frame, nullptr, 0, false);
    return 0;
}
