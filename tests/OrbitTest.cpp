#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "Check.hpp"
#include "Constants/Constants.hpp"
#include "InputOutput/InputOutput.hpp"
#include "Logger/Logger.hpp"
#include "NamedBodies/NamedBodies.hpp"
#include "Orbit/Orbit.hpp"

using Orbit::Vec;

// Checks that a is within a relative `tolerance` of b
#define CHECK_REL(a, b, tolerance) CHECK_NEAR((a), (b), (tolerance) * std::abs(b))


namespace {
constexpr double G = Constants::Simulation::G;
constexpr double MU = G * 2e30;  // a star of 2e30 kg
constexpr double PI = std::numbers::pi;
constexpr double INF = std::numeric_limits<double>::infinity();
constexpr double AU = 149'597'870'700;  // m

// The elements of a state that has them
Orbit::Elements elements(Vec rel_pos, Vec rel_vel, double mu = MU) {
    const auto el = Orbit::elements(rel_pos, rel_vel, mu);
    CHECK(el.has_value());
    return el.value_or(Orbit::Elements{});
}

// The state at true anomaly `nu` on an orbit of eccentricity `e` and semi-latus rectum `p`, with
// its periapsis along +x, moving counter-clockwise in world coordinates
std::pair<Vec, Vec> state_at(double e, double p, double nu) {
    const Vec pos = p / (1.0 + e * std::cos(nu)) * Vec(std::cos(nu), std::sin(nu));
    const Vec vel = std::sqrt(MU / p) * Vec(-std::sin(nu), e + std::cos(nu));
    return {pos, vel};
}

// Time from true anomaly nu0 to nu1 on that orbit: the integral of r^2 / h, by Simpson's rule
double time_between(double e, double p, double nu0, double nu1) {
    constexpr int INTERVALS = 20'000;
    const double h = std::sqrt(MU * p);
    const auto dt_dnu = [&](double nu) {
        const double r = p / (1.0 + e * std::cos(nu));
        return r * r / h;
    };
    const double step = (nu1 - nu0) / INTERVALS;
    double sum = dt_dnu(nu0) + dt_dnu(nu1);
    for (int i = 1; i < INTERVALS; i++) {
        sum += (i % 2 == 1 ? 4.0 : 2.0) * dt_dnu(nu0 + i * step);
    }
    return sum * step / 3.0;
}

void test_circular() {
    const double r = 1.5e11, speed = std::sqrt(MU / r);
    const Orbit::Elements el = elements({r, 0.0}, {0.0, speed});
    CHECK(el.eccentricity <= 1e-12);
    CHECK_REL(el.semi_major_axis, r, 1e-12);
    CHECK_REL(el.semi_latus_rectum, r, 1e-12);
    CHECK_REL(el.periapsis, r, 1e-12);
    CHECK_REL(el.apoapsis, r, 1e-12);
    CHECK_REL(el.period, 2.0 * PI * std::sqrt(r * r * r / MU), 1e-12);
    CHECK(el.angular_momentum > 0.0 && el.closed());
    CHECK(el.time_to_periapsis >= 0.0 && el.time_to_periapsis <= el.period);

    const Orbit::Elements retrograde = elements({r, 0.0}, {0.0, -speed});
    CHECK(retrograde.angular_momentum < 0.0);
    CHECK_REL(retrograde.semi_major_axis, el.semi_major_axis, 1e-12);
    CHECK_NEAR(retrograde.eccentricity, el.eccentricity, 1e-12);
}

void test_ellipse() {
    const double q = 1e11, e = 0.5, speed = std::sqrt(MU * (1.0 + e) / q);
    const Orbit::Elements el = elements({q, 0.0}, {0.0, speed});
    CHECK_NEAR(el.eccentricity, e, 1e-12);
    CHECK_REL(el.semi_major_axis, 2e11, 1e-12);
    CHECK_REL(el.apoapsis, 3e11, 1e-12);
    CHECK_NEAR(el.periapsis_angle, 0.0, 1e-12);
    CHECK_NEAR(el.true_anomaly, 0.0, 1e-12);
    CHECK(std::min(el.time_to_periapsis, el.period - el.time_to_periapsis) <= 1e-6 * el.period);

    // The same orbit rotated, either way round
    const double theta = 0.5236;
    const Vec dir(std::cos(theta), std::sin(theta)), normal(-std::sin(theta), std::cos(theta));
    const Orbit::Elements rotated = elements(q * dir, speed * normal);
    CHECK_NEAR(rotated.periapsis_angle, theta, 1e-12);
    CHECK_NEAR(rotated.true_anomaly, 0.0, 1e-12);
    const Orbit::Elements reversed = elements(q * dir, -speed * normal);
    CHECK(reversed.angular_momentum < 0.0);
    CHECK_NEAR(reversed.periapsis_angle, theta, 1e-12);
    CHECK_NEAR(reversed.true_anomaly, 0.0, 1e-12);
}

// States built from elements, the time to periapsis against a numeric integral
void test_from_elements() {
    const double e = 0.3, p = 1.2e11;
    for (const double nu0 : {2.0, -1.0, 3.1}) {
        const auto [pos, vel] = state_at(e, p, nu0);
        const Orbit::Elements el = elements(pos, vel);
        CHECK_NEAR(el.eccentricity, e, 1e-10);
        CHECK_REL(el.semi_latus_rectum, p, 1e-10);
        CHECK_NEAR(el.true_anomaly, nu0, 1e-10);
        CHECK_NEAR((el.position_at(el.true_anomaly) - pos).length(), 0.0, 1e-10 * pos.length());
        CHECK_REL(el.time_to_periapsis, time_between(e, p, nu0, nu0 > 0.0 ? 2.0 * PI : 0.0),
                1e-8);
    }

    // Retrograde: the true anomaly still grows in the direction of motion
    const auto [pos, vel] = state_at(e, p, 2.0);
    const Orbit::Elements el = elements({pos.x, -pos.y}, {vel.x, -vel.y});
    CHECK(el.angular_momentum < 0.0);
    CHECK_NEAR(el.true_anomaly, 2.0, 1e-10);
    CHECK_NEAR((el.position_at(2.0) - Vec(pos.x, -pos.y)).length(), 0.0, 1e-10 * pos.length());
}

void test_hyperbola() {
    const double e = 1.5, q = 1e11, p = q * (1.0 + e);
    const Orbit::Elements el = elements({q, 0.0}, {0.0, std::sqrt(MU * (1.0 + e) / q)});
    CHECK(!el.closed());
    CHECK_NEAR(el.eccentricity, e, 1e-12);
    CHECK_REL(el.semi_major_axis, -2e11, 1e-12);
    CHECK(el.apoapsis == INF && el.period == INF);
    CHECK_REL(el.excess_speed, std::sqrt(MU * (e - 1.0) / q), 1e-10);

    const auto [pos_in, vel_in] = state_at(e, p, -1.0);
    const Orbit::Elements incoming = elements(pos_in, vel_in);
    CHECK_NEAR(incoming.true_anomaly, -1.0, 1e-10);
    CHECK_REL(incoming.time_to_periapsis, time_between(e, p, -1.0, 0.0), 1e-8);

    const auto [pos_out, vel_out] = state_at(e, p, 1.0);
    CHECK(elements(pos_out, vel_out).time_to_periapsis == INF);
}

// Barker's equation, whichever side of e = 1 rounding puts the state on
void test_parabola() {
    const double p = 2e11;
    for (const double nu0 : {-PI / 2.0, -0.3, -2.5}) {
        const auto [pos, vel] = state_at(1.0, p, nu0);
        const Orbit::Elements el = elements(pos, vel);
        const double D = std::tan(nu0 / 2.0);
        CHECK_NEAR(el.eccentricity, 1.0, 1e-12);
        CHECK_REL(el.time_to_periapsis, -0.5 * std::sqrt(p * p * p / MU) * (D + D * D * D / 3.0),
                1e-9);
        CHECK(el.excess_speed <= 1e-6 * el.speed);
    }
    const auto [pos, vel] = state_at(1.0, p, -PI / 2.0);
    CHECK_REL(elements(pos, vel).time_to_periapsis, 2.0 / 3.0 * std::sqrt(p * p * p / MU), 1e-9);

    // Barely closed and barely open orbits agree with it
    for (const double e : {1.0 - 1e-9, 1.0 + 1e-9, 1.0 + 1e-5}) {
        const auto [pos_e, vel_e] = state_at(e, p, -PI / 2.0);
        const Orbit::Elements el = elements(pos_e, vel_e);
        CHECK(el.closed() == (e < 1.0));
        CHECK_REL(el.time_to_periapsis, time_between(e, p, -PI / 2.0, 0.0), 1e-8);
    }
}

void test_degenerate() {
    const Vec pos(1.5e11, 0.0), vel(0.0, 3e4);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(!Orbit::elements({0.0, 0.0}, vel, MU));
    CHECK(!Orbit::elements(pos, vel, 0.0));
    CHECK(!Orbit::elements(pos, vel, -MU));
    CHECK(!Orbit::elements(pos, vel, nan));
    CHECK(!Orbit::elements({nan, 0.0}, vel, MU));
    CHECK(!Orbit::elements(pos, {0.0, nan}, MU));
    CHECK(!Orbit::elements(pos, {INF, 0.0}, MU));
    // Radial motion has no conic
    CHECK(!Orbit::elements(pos, {-3e4, 0.0}, MU));
    CHECK(!Orbit::elements(pos, {0.0, 0.0}, MU));
}

// A hand-made system, with the bodies' primaries. Positions are all that primaries need.
struct System {
    std::vector<std::string> ids;
    std::vector<double> mass;
    std::vector<Vec> pos, vel;
    std::vector<uint32_t> named;
    std::vector<int32_t> primaries;

    System() {
        const auto add = [this](const char* id, double m, Vec p) {
            ids.push_back(id);
            mass.push_back(m);
            pos.push_back(p);
            vel.push_back({});
        };
        add("Sun", 2e30, {0.0, 0.0});
        add("Earth", 6e24, {1.5e11, 0.0});
        add("Moon", 7.3e22, pos[1] + Vec(3.84e8, 0.0));
        add("Mars", 6.4e23, {-2.28e11, 0.0});
        add("Phobos", 1.07e16, pos[3] + Vec(0.0, 9.4e6));
        add("Pluto", 1.3e22, {0.0, 5.9e12});
        add("Charon", 1.6e21, pos[5] + Vec(1.96e7, 0.0));
        add("Nix", 4.5e16, pos[5] + Vec(0.0, 4.87e7));
        add("probe", 0.0, pos[3] + Vec(2e8, 0.0));
        named = {0, 1, 3, 2, 5, 6, 7, 4, 8};  // most massive first
        primaries = Orbit::find_primaries(named, mass.data(), pos.data());
    }

    uint32_t index(const std::string& id) const {
        return static_cast<uint32_t>(std::find(ids.begin(), ids.end(), id) - ids.begin());
    }

    // The id of a body's primary, "" for none
    std::string primary_of(const std::string& id) const {
        const auto rank = std::find(named.begin(), named.end(), index(id)) - named.begin();
        return primaries[rank] < 0 ? "" : ids[named[primaries[rank]]];
    }

    // The ids of a body's satellites, most massive first
    std::vector<std::string> satellites_of(const std::string& id) const {
        const auto rank = std::find(named.begin(), named.end(), index(id)) - named.begin();
        std::vector<std::string> satellites;
        for (const uint32_t i : Orbit::find_satellites(static_cast<uint32_t>(rank), named,
                     primaries, mass.data(), pos.data())) {
            satellites.push_back(ids[i]);
        }
        return satellites;
    }

    // The attractor of the bodies in `selection`, treated as one body
    std::optional<Orbit::Attractor> attractor(const std::vector<std::string>& selection) const {
        std::vector<uint32_t> members;
        for (const auto& id : selection) {
            members.push_back(index(id));
        }
        const Orbit::Target target =
                Orbit::barycentre(members, mass.data(), pos.data(), vel.data());
        return Orbit::find_attractor(target, named, primaries, mass.data(), pos.data(), vel.data(),
                [&members](uint32_t i) {
                    return std::find(members.begin(), members.end(), i) != members.end();
                });
    }
};

void test_primaries() {
    const System system;
    CHECK(system.primaries.size() == system.named.size());
    for (const char* id : {"Earth", "Mars", "Pluto"}) {
        CHECK(system.primary_of(id) == "Sun");
    }
    CHECK(system.primary_of("Moon") == "Earth");
    CHECK(system.primary_of("Phobos") == "Mars");
    CHECK(system.primary_of("Charon") == "Pluto");
    CHECK(system.primary_of("Nix") == "Pluto");
    CHECK(system.primary_of("probe") == "Mars");
    CHECK(system.primary_of("Sun") == "");
}

void test_attractors() {
    const System system;
    const uint32_t pluto = system.index("Pluto"), charon = system.index("Charon");

    const auto nix = system.attractor({"Nix"});
    CHECK(nix && nix->primary == pluto && nix->inner_count == 1);
    if (nix) {
        const double mass = system.mass[pluto] + system.mass[charon];
        const Vec barycentre =
                (system.mass[pluto] * system.pos[pluto] + system.mass[charon] * system.pos[charon])
                / mass;
        CHECK_REL(nix->mass, mass, 1e-15);
        CHECK_NEAR((nix->pos - barycentre).length(), 0.0, 1e-14 * barycentre.length());
    }

    const auto earth_moon = system.attractor({"Earth", "Moon"});
    CHECK(earth_moon && earth_moon->primary == system.index("Sun") && earth_moon->inner_count == 0);
    // The Moon's chain of primaries passes through the target
    const auto earth = system.attractor({"Earth"});
    CHECK(earth && earth->primary == system.index("Sun") && earth->inner_count == 0);
    CHECK(!system.attractor({"Sun"}));
}

void test_satellites() {
    const System system;
    using Ids = std::vector<std::string>;
    CHECK(system.satellites_of("Earth") == Ids({"Moon"}));
    CHECK(system.satellites_of("Pluto") == Ids({"Charon", "Nix"}));
    CHECK(system.satellites_of("Mars") == Ids({"Phobos", "probe"}));
    CHECK(system.satellites_of("Sun").size() == 8);
    CHECK(system.satellites_of("Moon").empty() && system.satellites_of("probe").empty());

    // Barycentres, by mass or, for massless bodies, plain means
    const Orbit::Target earth_moon = Orbit::barycentre(std::vector<uint32_t>{1, 2},
            system.mass.data(), system.pos.data(), system.vel.data());
    CHECK_REL(earth_moon.mass, 6e24 + 7.3e22, 1e-15);
    CHECK_REL(earth_moon.pos.x, 1.5e11 + 3.84e8 * 7.3e22 / (6e24 + 7.3e22), 1e-15);
    const std::vector<double> massless(2, 0.0);
    const std::vector<Vec> points = {{1.0, 2.0}, {3.0, 6.0}};
    const Orbit::Target mean = Orbit::barycentre(std::vector<uint32_t>{0, 1}, massless.data(),
            points.data(), points.data());
    CHECK(mean.mass == 0.0 && mean.pos == Vec(2.0, 4.0) && mean.vel == Vec(2.0, 4.0));
}

// Equal-mass stars are each other's primary
void test_binary_primaries() {
    // A planet around a binary orbits the nearer star, merged with the other one
    const std::vector<double> mass = {1e30, 1e30, 1e20};
    const std::vector<Vec> pos = {{0.0, 0.0}, {1e11, 0.0}, {5e11, 0.0}}, vel(3);
    const std::vector<uint32_t> named = {0, 1, 2};
    const auto primaries = Orbit::find_primaries(named, mass.data(), pos.data());
    CHECK(primaries == std::vector<int32_t>({1, 0, 1}));
    const auto attractor = Orbit::find_attractor({1e20, pos[2], {}}, named, primaries,
            mass.data(), pos.data(), vel.data(), [](uint32_t i) { return i == 2; });
    CHECK(attractor && attractor->primary == 1 && attractor->inner_count == 1);
    CHECK(attractor && (attractor->pos - Vec(5e10, 0.0)).length() <= 1e-15 * 5e10);
    // It orbits the pair, so it isn't a satellite of either star
    CHECK(Orbit::find_satellites(0, named, primaries, mass.data(), pos.data()).empty());
    CHECK(Orbit::find_satellites(1, named, primaries, mass.data(), pos.data()).empty());
    // Unlike a planet close to one star of a wide pair
    const std::vector<Vec> wide = {{0.0, 0.0}, {1e13, 0.0}, {1e13 + 1.5e11, 0.0}};
    const auto wide_primaries = Orbit::find_primaries(named, mass.data(), wide.data());
    CHECK(Orbit::find_satellites(1, named, wide_primaries, mass.data(), wide.data())
            == std::vector<uint32_t>({2}));
    CHECK(Orbit::find_satellites(0, named, wide_primaries, mass.data(), wide.data()).empty());

    // A planet around a star far from a binary: the binary's chains go round in circles
    const std::vector<double> mass2 = {1e31, 1e30, 1e30, 0.0};
    const std::vector<Vec> pos2 = {{0.0, 0.0}, {1e13, 0.0}, {1e13 + 1e11, 0.0}, {1e11, 0.0}},
                           vel2(4);
    const std::vector<uint32_t> named2 = {0, 1, 2, 3};
    const auto primaries2 = Orbit::find_primaries(named2, mass2.data(), pos2.data());
    CHECK(primaries2 == std::vector<int32_t>({-1, 2, 1, 0}));
    const auto attractor2 = Orbit::find_attractor({0.0, pos2[3], {}}, named2, primaries2,
            mass2.data(), pos2.data(), vel2.data(), [](uint32_t i) { return i == 3; });
    CHECK(attractor2 && attractor2->primary == 0 && attractor2->inner_count == 0);
}

// A planet around a binary star stays with both stars as it passes the lighter one, unlike a moon
// of that star
void test_sphere_of_influence() {
    // Kepler-16-like: stars A and B, a planet beyond B and a moon of B
    const std::vector<double> mass = {1.37e30, 4e29, 6.3e26, 1e22};
    const std::vector<Vec> pos = {{0.0, 0.0}, {3.4e10, 0.0}, {9e10, 0.0}, {3.5e10, 0.0}}, vel(4);
    const std::vector<uint32_t> named = {0, 1, 2, 3};
    const auto is = [](uint32_t index) { return [index](uint32_t i) { return i == index; }; };
    // By m / d^3, B is the planet's primary, but the planet is far out of B's sphere
    CHECK(Orbit::find_primary(mass[2], pos[2], named, mass.data(), pos.data(), is(2)) == 1u);
    const auto primaries = Orbit::find_primaries(named, mass.data(), pos.data());
    CHECK(primaries == std::vector<int32_t>({-1, 0, 0, 1}));
    const auto planet = Orbit::find_attractor({mass[2], pos[2], {}}, named, primaries,
            mass.data(), pos.data(), vel.data(), is(2));
    CHECK(planet && planet->primary == 0 && planet->inner_count == 2);
    const auto moon = Orbit::find_attractor({mass[3], pos[3], {}}, named, primaries, mass.data(),
            pos.data(), vel.data(), is(3));
    CHECK(moon && moon->primary == 1 && moon->inner_count == 0);
}

// The window and orbit of the tracing tests: e = 0.5, q = 1e11 around the origin
constexpr sf::Vector2f WINDOW(1600.f, 900.f);
constexpr double MARGIN_PX = 2.0, TOLERANCE_PX = 0.5;

Orbit::Elements ellipse() {
    const double q = 1e11, e = 0.5;
    return elements({q, 0.0}, {0.0, std::sqrt(MU * (1.0 + e) / q)});
}

bool in_grown_window(sf::Vector2f p) {
    return p.x >= -MARGIN_PX - 1e-3 && p.x <= WINDOW.x + MARGIN_PX + 1e-3
           && p.y >= -MARGIN_PX - 1e-3 && p.y <= WINDOW.y + MARGIN_PX + 1e-3;
}

std::vector<Orbit::Segment> trace(const Orbit::Elements& el,
        const Orbit::ScreenTransform& transform) {
    std::vector<Orbit::Segment> segments;
    Orbit::trace(el, {0.0, 0.0}, transform, WINDOW, segments, TOLERANCE_PX, MARGIN_PX);
    return segments;
}

void test_trace_visible() {
    const Orbit::Elements el = ellipse();
    // The ellipse's centre, at (-a e, 0), in the middle of the window
    const double ppm = 2e-9;
    const Orbit::ScreenTransform transform{Vec(-1e11, 0.0) - Vec(WINDOW) / 2.0 / ppm, ppm};
    const auto segments = trace(el, transform);
    CHECK(!segments.empty() && segments.size() < 2000);

    double length = 0.0, worst_error = 0.0;
    for (const auto& segment : segments) {
        length += (segment.b - segment.a).length();
        for (const sf::Vector2f end : {segment.a, segment.b}) {
            CHECK(in_grown_window(end));
            const Vec world = transform.origin + Vec(end) / ppm;
            const double phi = std::atan2(world.y, world.x);
            const double r = el.semi_latus_rectum
                             / (1.0 + el.eccentricity * std::cos(phi - el.periapsis_angle));
            worst_error = std::max(worst_error, std::abs(world.length() - r) * ppm);
        }
    }
    CHECK(worst_error <= 1.0);
    // Ramanujan's perimeter
    const double a = el.semi_major_axis, b = a * std::sqrt(1.0 - el.eccentricity * el.eccentricity);
    const double perimeter = PI * (3.0 * (a + b) - std::sqrt((3.0 * a + b) * (a + 3.0 * b)));
    CHECK_REL(length, perimeter * ppm, 0.01);
    // In order along the orbit, starting at its periapsis
    CHECK_NEAR((Vec(segments.front().a) - transform.to_screen(el.position_at(0.0))).length(), 0.0,
            1e-3);
    for (size_t i = 1; i < segments.size(); i++) {
        CHECK_NEAR((segments[i].a - segments[i - 1].b).length(), 0.0, 1e-3);
    }
}

void test_trace_deep_zoom() {
    const Orbit::Elements el = ellipse();
    const double ppm = 1.0;  // 1 m per pixel
    const Orbit::ScreenTransform transform{el.position_at(1.0) - Vec(WINDOW) / 2.0 / ppm, ppm};
    const auto segments = trace(el, transform);
    CHECK(!segments.empty() && segments.size() < 200);

    double nearest = INF;
    const sf::Vector2f centre = WINDOW / 2.f;
    for (const auto& segment : segments) {
        CHECK(in_grown_window(segment.a) && in_grown_window(segment.b));
        const sf::Vector2f d = segment.b - segment.a;
        const float t = std::clamp((centre - segment.a).dot(d) / d.lengthSquared(), 0.f, 1.f);
        nearest = std::min(nearest, static_cast<double>((segment.a + t * d - centre).length()));
    }
    CHECK(nearest <= 1.0);
}

void test_trace_off_screen() {
    const Orbit::ScreenTransform transform{{1e13, 1e13}, 2e-9};
    CHECK(trace(ellipse(), transform).empty());
}

void test_trace_open() {
    const double e = 1.5, q = 1e11;
    const Orbit::Elements el = elements({q, 0.0}, {0.0, std::sqrt(MU * (1.0 + e) / q)});
    const double ppm = 2e-9;
    const auto segments = trace(el, {-Vec(WINDOW) / 2.0 / ppm, ppm});
    CHECK(!segments.empty());
    for (const auto& segment : segments) {
        CHECK(std::isfinite(segment.a.x) && std::isfinite(segment.a.y)
                && std::isfinite(segment.b.x) && std::isfinite(segment.b.y));
        CHECK(in_grown_window(segment.a) && in_grown_window(segment.b));
    }
}

void test_dashes() {
    const std::vector<Orbit::Segment> segments = {{{10.f, 20.f}, {110.f, 20.f}}};
    sf::VertexArray lines(sf::PrimitiveType::Lines);
    Orbit::append_dashes(segments, 8.f, 6.f, sf::Color::Red, lines);
    CHECK(lines.getVertexCount() == 16);
    if (lines.getVertexCount() == 16) {
        CHECK(lines[0].position == sf::Vector2f(10.f, 20.f));
        for (size_t dash = 0; dash < 8; dash++) {
            const float length = (lines[2 * dash + 1].position - lines[2 * dash].position).length();
            CHECK_NEAR(length, dash < 7 ? 8.0 : 2.0, 1e-4);
            CHECK_NEAR(lines[2 * dash].position.x, 10.0 + 14.0 * dash, 1e-4);
        }
        CHECK(lines[0].color == sf::Color::Red);
    }

    // The pattern carries on across segments: dashes 0..5 and 5..8, a gap to 14, then 14..20
    sf::VertexArray split(sf::PrimitiveType::Lines);
    Orbit::append_dashes(std::vector<Orbit::Segment>{{{0.f, 0.f}, {5.f, 0.f}},
                                 {{5.f, 0.f}, {10.f, 0.f}}, {{10.f, 0.f}, {20.f, 0.f}}},
            8.f, 6.f, sf::Color::Red, split);
    CHECK(split.getVertexCount() == 6);
    if (split.getVertexCount() == 6) {
        CHECK(split[1].position == sf::Vector2f(5.f, 0.f));
        CHECK(split[3].position == sf::Vector2f(8.f, 0.f));
        CHECK(split[4].position == sf::Vector2f(14.f, 0.f));
        CHECK(split[5].position == sf::Vector2f(20.f, 0.f));
    }
}

// A selection of committed Solar System bodies, treated as one body, and the generator's input
// elements of its orbit
struct Golden {
    std::vector<std::string> selection;
    std::string primary;  // "" for none
    uint32_t inner_count;
    double semi_major_axis;  // m
    double eccentricity;
    double tolerance;        // relative on the semi-major axis, absolute on the eccentricity
};

// Elements at t = 0 of the committed solar-system.csv, which the generator built around the
// barycentre of each primary with its inner orbiters, like the attractors here. Halley's inner
// bodies, counted by distance, include Uranus, which the generator orders by semi-major axis.
void test_golden(const char* csv_path) {
    const Bodies bodies = IO::parse_csv(csv_path, false);
    const std::vector<uint32_t> named = find_named_bodies(bodies,
            Constants::Graphics::NAMED_GROUP_MAX_SIZE, Constants::Graphics::NAMED_BODIES_MAX);
    CHECK(named.size() == 44);
    const std::vector<int32_t> primaries =
            Orbit::find_primaries(named, bodies.mass_data(), bodies.pos_data());
    const auto index = [&bodies](const std::string& id) {
        for (uint32_t i = 0; i < bodies.n; i++) {
            if (bodies.id(i) == id) {
                return i;
            }
        }
        CHECK(!"body in the CSV");
        return 0u;
    };
    const auto satellites = [&](const std::string& id) {
        const auto rank = std::find(named.begin(), named.end(), index(id)) - named.begin();
        return Orbit::find_satellites(static_cast<uint32_t>(rank), named, primaries,
                bodies.mass_data(), bodies.pos_data());
    };
    const auto satellite_ids = [&](const std::string& id) {
        std::vector<std::string> ids;
        for (const uint32_t i : satellites(id)) {
            ids.push_back(bodies.id(i));
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    };

    struct Result {
        std::optional<Orbit::Attractor> attractor;
        std::optional<Orbit::Elements> elements;
    };
    const auto inspect_members = [&](const std::vector<uint32_t>& members) {
        const Orbit::Target target = Orbit::barycentre(members, bodies.mass_data(),
                bodies.pos_data(), bodies.vel_data());
        Result result;
        result.attractor = Orbit::find_attractor(target, named, primaries, bodies.mass_data(),
                bodies.pos_data(), bodies.vel_data(), [&members](uint32_t i) {
                    return std::find(members.begin(), members.end(), i) != members.end();
                });
        if (result.attractor) {
            result.elements = Orbit::elements(target.pos - result.attractor->pos,
                    target.vel - result.attractor->vel, G * (target.mass + result.attractor->mass));
        }
        return result;
    };
    const auto inspect = [&](const std::vector<std::string>& selection) {
        std::vector<uint32_t> members;
        for (const auto& id : selection) {
            members.push_back(index(id));
        }
        return inspect_members(members);
    };
    // A single body, as the inspector shows it: with its satellites
    const auto inspect_body = [&](const std::string& id) {
        std::vector<uint32_t> members = satellites(id);
        members.insert(members.begin(), index(id));
        return inspect_members(members);
    };

    const std::vector<Golden> golden = {
            {{"Mercury"}, "Sun", 0, 0.38709927 * AU, 0.20563593, 1e-8},
            {{"Venus"}, "Sun", 1, 0.72333566 * AU, 0.00677672, 1e-8},
            {{"Earth", "Moon"}, "Sun", 2, 1.00000261 * AU, 0.01671123, 1e-8},
            {{"Mars", "Phobos", "Deimos"}, "Sun", 4, 1.52371034 * AU, 0.09339410, 1e-8},
            {{"Jupiter", "Io", "Europa", "Ganymede", "Callisto"}, "Sun", 11, 5.20288700 * AU,
                    0.04838624, 1e-8},
            {{"Saturn", "Mimas", "Enceladus", "Tethys", "Dione", "Rhea", "Titan", "Hyperion",
                     "Iapetus"},
                    "Sun", 16, 9.53667594 * AU, 0.05386179, 1e-8},
            {{"Uranus", "Miranda", "Ariel", "Umbriel", "Titania", "Oberon"}, "Sun", 25,
                    19.18916464 * AU, 0.04725744, 1e-8},
            {{"Neptune", "Proteus", "Triton", "Nereid"}, "Sun", 32, 30.06992276 * AU, 0.00859048,
                    1e-8},
            {{"Pluto", "Charon", "Nix", "Hydra"}, "Sun", 36, 39.48211675 * AU, 0.24882730, 1e-8},
            {{"Moon"}, "Earth", 0, 384'399e3, 0.0549, 1e-8},
            {{"Phobos"}, "Mars", 0, 9'376e3, 0.0151, 1e-8},
            {{"Deimos"}, "Mars", 1, 23'463.2e3, 0.00033, 1e-8},
            {{"Io"}, "Jupiter", 0, 421'700e3, 0.0041, 1e-8},
            {{"Callisto"}, "Jupiter", 3, 1'882'709e3, 0.0074, 1e-8},
            {{"Titan"}, "Saturn", 5, 1'221'870e3, 0.0288, 1e-8},
            {{"Iapetus"}, "Saturn", 7, 3'560'820e3, 0.0286, 1e-8},
            {{"Triton"}, "Neptune", 1, 354'759e3, 0.000016, 1e-8},
            {{"Nereid"}, "Neptune", 2, 5'513'818e3, 0.7507, 1e-8},
            {{"Charon"}, "Pluto", 0, 19'591e3, 0.0002, 1e-8},
            {{"Nix"}, "Pluto", 1, 48'694e3, 0.002, 1e-8},
            {{"Hydra"}, "Pluto", 2, 64'738e3, 0.0059, 1e-8},
            {{"Ceres"}, "Sun", 9, 2.7675 * AU, 0.0758, 1e-8},
            {{"Eris"}, "Sun", 42, 67.86 * AU, 0.4361, 1e-8},
            {{"Halley"}, "Sun", 31, 17.834 * AU, 0.96714, 1e-4},
            // Not merged with the Moon, whose primary is in the target
            {{"Earth"}, "Sun", 2, 149'737'337'272.1, 0.0175941387, 1e-8},
            {{"Sun"}, "", 0, 0.0, 0.0, 0.0}};

    const auto check_row = [&bodies](const Golden& row, const Result& result) {
        if (row.primary.empty()) {
            CHECK(!result.attractor);
            return;
        }
        CHECK(result.attractor && result.elements);
        if (!result.attractor || !result.elements) {
            fmt::println("  {}: no orbit", row.selection.front());
            return;
        }
        const bool ok = bodies.id(result.attractor->primary) == row.primary
                        && result.attractor->inner_count == row.inner_count
                        && std::abs(result.elements->semi_major_axis / row.semi_major_axis - 1.0)
                                   <= row.tolerance
                        && std::abs(result.elements->eccentricity - row.eccentricity)
                                   <= row.tolerance;
        CHECK(ok);
        if (!ok) {
            fmt::println("  {}: {} (+{} inner), a = {} m, e = {}", row.selection.front(),
                    bodies.id(result.attractor->primary), result.attractor->inner_count,
                    result.elements->semi_major_axis, result.elements->eccentricity);
        }
    };
    size_t body_rows = 0;
    for (const Golden& row : golden) {
        check_row(row, inspect(row.selection));
        // Rows of a body with all its satellites: the body alone, as the inspector shows it
        std::vector<std::string> others(row.selection.begin() + 1, row.selection.end());
        std::sort(others.begin(), others.end());
        if (satellite_ids(row.selection.front()) == others) {
            check_row(row, inspect_body(row.selection.front()));
            body_rows++;
        }
    }
    // All but the Earth without the Moon, and the Sun with everything
    CHECK(body_rows == golden.size() - 2);
    CHECK(satellite_ids("Pluto") == std::vector<std::string>({"Charon", "Hydra", "Nix"}));
    CHECK(satellite_ids("Saturn").size() == 8 && satellite_ids("Sun").size() == 43);
    CHECK(satellite_ids("Moon").empty() && satellite_ids("Halley").empty());

    // Times to the next periapsis and directions
    const auto time_to_periapsis = [&](const std::vector<std::string>& selection) {
        const Result result = inspect(selection);
        return result.elements ? result.elements->time_to_periapsis : -1.0;
    };
    CHECK_REL(time_to_periapsis({"Earth", "Moon"}), 216'797.18, 1e-6);
    CHECK_REL(time_to_periapsis({"Moon"}), 434'288.51, 1e-6);
    CHECK_REL(time_to_periapsis({"Nix"}), 1'654'764.06, 1e-6);
    const auto angular_momentum = [&](const std::string& id) {
        const Result result = inspect({id});
        return result.elements ? result.elements->angular_momentum : 0.0;
    };
    const auto retrograde = [&](const std::string& id) {
        const Result result = inspect({id});
        return result.elements
               && Orbit::retrograde(result.elements->angular_momentum, *result.attractor, named,
                       primaries, bodies.pos_data(), bodies.vel_data());
    };
    CHECK(angular_momentum("Triton") < 0.0 && retrograde("Triton"));
    CHECK(angular_momentum("Earth") > 0.0 && !retrograde("Earth"));
    CHECK(!retrograde("Moon") && !retrograde("Nereid") && !retrograde("Nix"));
    // Halley goes the other way round the Sun, which has no primary
    CHECK(angular_momentum("Halley") < 0.0 && !retrograde("Halley"));
}
}  // namespace


// test-orbit [solar-system.csv]: with the CSV, also checks the committed Solar System
int main(int argc, const char* argv[]) {
    Log::verbosity = Log::Verbosity::WARNING;
    test_circular();
    test_ellipse();
    test_from_elements();
    test_hyperbola();
    test_parabola();
    test_degenerate();
    test_primaries();
    test_attractors();
    test_satellites();
    test_binary_primaries();
    test_sphere_of_influence();
    test_trace_visible();
    test_trace_deep_zoom();
    test_trace_off_screen();
    test_trace_open();
    test_dashes();
    if (argc > 1) {
        test_golden(argv[1]);
    }
    return Check::result();
}
