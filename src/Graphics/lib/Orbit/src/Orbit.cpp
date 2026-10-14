#include "Orbit/Orbit.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "Constants/Constants.hpp"


namespace {
constexpr double PI = std::numbers::pi;
constexpr double INF = std::numeric_limits<double>::infinity();
// Open orbits closer than this to parabolic use Barker's equation for the time to periapsis
constexpr double BARKER_MAX_EXCESS_ECCENTRICITY = 1e-6;
// Below this eccentricity the periapsis direction is noise, and the current direction is used
constexpr double CIRCULAR_ECCENTRICITY = 1e-12;

// +1 if the motion is counter-clockwise in world coordinates (clockwise on screen), else -1
double motion_sign(const Orbit::Elements& el) {
    return el.angular_momentum >= 0.0 ? 1.0 : -1.0;
}

// x - sin(x) and sinh(x) - x, without the cancellation of small x: they keep Kepler's equations
// precise near e = 1 and at small anomalies
double x_minus_sin(double x) {
    if (std::abs(x) > 0.5) {
        return x - std::sin(x);
    }
    double term = x * x * x / 6.0, sum = 0.0;
    for (int k = 2; k <= 10; k++) {
        sum += term;
        term *= -x * x / ((2 * k) * (2 * k + 1));
    }
    return sum;
}

double sinh_minus_x(double x) {
    if (std::abs(x) > 0.5) {
        return std::sinh(x) - x;
    }
    double term = x * x * x / 6.0, sum = 0.0;
    for (int k = 2; k <= 10; k++) {
        sum += term;
        term *= x * x / ((2 * k) * (2 * k + 1));
    }
    return sum;
}

// Time to the next periapsis on a closed orbit, from Kepler's equation
double closed_time_to_periapsis(const Orbit::Elements& el) {
    const double e = el.eccentricity, nu = el.true_anomaly;
    const double E = 2.0 * std::atan2(std::sqrt(1.0 - e) * std::sin(nu / 2.0),
                                 std::sqrt(1.0 + e) * std::cos(nu / 2.0));
    const double M = (1.0 - e) * std::sin(E) + x_minus_sin(E);  // E - e sin(E), in [-pi, pi]
    return (M <= 0.0 ? -M : 2.0 * PI - M) / (2.0 * PI) * el.period;
}

// Time to the periapsis of an open orbit that hasn't reached it yet (nu < 0)
double open_time_to_periapsis(const Orbit::Elements& el) {
    const double e = el.eccentricity, nu = el.true_anomaly, p = el.semi_latus_rectum;
    if (e - 1.0 <= BARKER_MAX_EXCESS_ECCENTRICITY) {
        const double D = std::tan(nu / 2.0);
        return -0.5 * p * std::sqrt(p / el.mu) * (D + D * D * D / 3.0);
    }
    const double F = 2.0 * std::atanh(std::sqrt((e - 1.0) / (e + 1.0)) * std::tan(nu / 2.0));
    const double M = (e - 1.0) * std::sinh(F) + sinh_minus_x(F);  // e sinh(F) - F
    const double minus_a = -el.semi_major_axis;
    return -M * minus_a * std::sqrt(minus_a / el.mu);
}

// The orbit's radius at true anomaly nu
double radius_at(const Orbit::Elements& el, double nu) {
    return el.semi_latus_rectum / (1.0 + el.eccentricity * std::cos(nu));
}

struct Box {
    Orbit::Vec lo, hi;

    void include(Orbit::Vec point) {
        lo = {std::min(lo.x, point.x), std::min(lo.y, point.y)};
        hi = {std::max(hi.x, point.x), std::max(hi.y, point.y)};
    }
};

// A world box that contains the arc nu0..nu1 (nu0 < nu1): the bounding box of the annular sector
// between the arc's directions and its smallest and largest radius, which it has at its ends or
// at an apsis
Box sector_box(const Orbit::Elements& el, Orbit::Vec focus, double nu0, double nu1) {
    const double r0 = radius_at(el, nu0), r1 = radius_at(el, nu1);
    double r_lo = std::min(r0, r1), r_hi = std::max(r0, r1);
    if ((nu0 <= 0.0 && nu1 >= 0.0) || (nu0 <= 2.0 * PI && nu1 >= 2.0 * PI)) {
        r_lo = el.periapsis;
    }
    if (el.closed() && nu0 <= PI && nu1 >= PI) {
        r_hi = el.apoapsis;
    }
    const double phi0 = el.periapsis_angle + motion_sign(el) * nu0;
    const double phi1 = el.periapsis_angle + motion_sign(el) * nu1;
    const double phi_a = std::min(phi0, phi1), phi_b = std::max(phi0, phi1);
    const auto point = [&](double r, double phi) {
        return focus + r * Orbit::Vec(std::cos(phi), std::sin(phi));
    };
    Box box{point(r_lo, phi_a), point(r_lo, phi_a)};
    box.include(point(r_lo, phi_b));
    box.include(point(r_hi, phi_a));
    box.include(point(r_hi, phi_b));
    // The sector reaches furthest along an axis at its outer radius
    for (double k = std::ceil(phi_a / (PI / 2.0)); k * (PI / 2.0) <= phi_b; k++) {
        box.include(point(r_hi, k * (PI / 2.0)));
    }
    return box;
}

// Liang-Barsky: the part of the segment a..b inside the box lo..hi, if any
std::optional<std::pair<Orbit::Vec, Orbit::Vec>> clip(Orbit::Vec a, Orbit::Vec b, Orbit::Vec lo,
        Orbit::Vec hi) {
    const Orbit::Vec d = b - a;
    double t0 = 0.0, t1 = 1.0;
    // Keeps the part where p t <= q
    const auto keep = [&t0, &t1](double p, double q) {
        if (p == 0.0) {
            return q >= 0.0;
        }
        if (p < 0.0) {
            t0 = std::max(t0, q / p);
        }
        else {
            t1 = std::min(t1, q / p);
        }
        return t0 <= t1;
    };
    if (keep(-d.x, a.x - lo.x) && keep(d.x, hi.x - a.x) && keep(-d.y, a.y - lo.y)
            && keep(d.y, hi.y - a.y)) {
        return std::pair(a + t0 * d, a + t1 * d);
    }
    return std::nullopt;
}

// Refines the conic's arcs into flat enough chords, skipping arcs that can't reach the window
class Tracer {
public:
    Tracer(const Orbit::Elements& el, Orbit::Vec focus, const Orbit::ScreenTransform& transform,
            sf::Vector2f window_size, double tolerance_px, double margin_px,
            std::vector<Orbit::Segment>& out)
            : el(el), focus(focus), transform(transform), lo(-margin_px, -margin_px),
              hi(Orbit::Vec(window_size) + Orbit::Vec(margin_px, margin_px)),
              tolerance_px(tolerance_px), out(out) {}

    Orbit::Vec screen_at(double nu) const {
        return transform.to_screen(focus + el.position_at(nu));
    }

    void refine(double nu0, double nu1, Orbit::Vec p0, Orbit::Vec p1, uint32_t depth) const {
        const Box box = sector_box(el, focus, nu0, nu1);
        const Orbit::Vec box_lo = transform.to_screen(box.lo), box_hi = transform.to_screen(box.hi);
        // Also skips boxes that aren't finite
        if (!(box_hi.x >= lo.x && box_lo.x <= hi.x && box_hi.y >= lo.y && box_lo.y <= hi.y)) {
            return;
        }
        const double nu_mid = 0.5 * (nu0 + nu1);
        const Orbit::Vec p_mid = screen_at(nu_mid);
        if (depth >= Constants::Graphics::ORBIT_MAX_DEPTH
                || (p_mid - 0.5 * (p0 + p1)).length() <= tolerance_px) {
            if (const auto clipped = clip(p0, p1, lo, hi)) {
                out.push_back({sf::Vector2f(clipped->first), sf::Vector2f(clipped->second)});
            }
            return;
        }
        refine(nu0, nu_mid, p0, p_mid, depth + 1);
        refine(nu_mid, nu1, p_mid, p1, depth + 1);
    }

private:
    const Orbit::Elements& el;
    const Orbit::Vec focus;
    const Orbit::ScreenTransform& transform;
    const Orbit::Vec lo, hi;  // the window grown by the margin
    const double tolerance_px;
    std::vector<Orbit::Segment>& out;
};
}  // namespace


namespace Orbit {
Vec Elements::position_at(double nu) const {
    const double r = radius_at(*this, nu);
    const double phi = periapsis_angle + motion_sign(*this) * nu;
    return {r * std::cos(phi), r * std::sin(phi)};
}

std::optional<Elements> elements(Vec rel_pos, Vec rel_vel, double mu) {
    const double distance = rel_pos.length();
    if (!(mu > 0.0 && distance > 0.0) || !std::isfinite(mu) || !std::isfinite(distance)
            || !std::isfinite(rel_vel.x) || !std::isfinite(rel_vel.y)) {
        return std::nullopt;
    }
    Elements el;
    el.mu = mu;
    el.distance = distance;
    const double speed_squared = rel_vel.lengthSquared();
    el.speed = std::sqrt(speed_squared);
    el.angular_momentum = rel_pos.cross(rel_vel);
    const Vec e_vec =
            ((speed_squared - mu / distance) * rel_pos - rel_pos.dot(rel_vel) * rel_vel) / mu;
    const double e = e_vec.length(), p = el.angular_momentum * el.angular_momentum / mu;
    if (!(p > 0.0)) {
        return std::nullopt;
    }
    el.eccentricity = e;
    el.semi_latus_rectum = p;
    // Rather than -mu / (2 x energy), which loses its sign near e = 1
    el.semi_major_axis = e == 1.0 ? INF : p / ((1.0 - e) * (1.0 + e));
    el.periapsis = p / (1.0 + e);
    el.apoapsis = e < 1.0 ? p / (1.0 - e) : INF;
    el.periapsis_angle = e < CIRCULAR_ECCENTRICITY ? std::atan2(rel_pos.y, rel_pos.x)
                                                   : std::atan2(e_vec.y, e_vec.x);
    const double cos_w = std::cos(el.periapsis_angle), sin_w = std::sin(el.periapsis_angle);
    const double nu = motion_sign(el) * std::atan2(cos_w * rel_pos.y - sin_w * rel_pos.x,
                                                cos_w * rel_pos.x + sin_w * rel_pos.y);
    el.true_anomaly = nu == -PI ? PI : nu;
    if (el.closed()) {
        const double a = el.semi_major_axis;
        el.period = 2.0 * PI * a * std::sqrt(a / mu);
        el.time_to_periapsis = closed_time_to_periapsis(el);
        el.excess_speed = 0.0;
    }
    else {
        el.period = INF;
        el.time_to_periapsis = el.true_anomaly >= 0.0 ? INF : open_time_to_periapsis(el);
        el.excess_speed = std::sqrt(std::max(0.0, speed_squared - 2.0 * mu / distance));
    }

    const bool finite = std::isfinite(el.speed) && std::isfinite(el.angular_momentum)
                        && std::isfinite(e) && std::isfinite(p) && std::isfinite(el.periapsis)
                        && std::isfinite(el.periapsis_angle) && std::isfinite(el.true_anomaly)
                        && std::isfinite(el.excess_speed);
    const bool closed_finite = std::isfinite(el.semi_major_axis) && std::isfinite(el.apoapsis)
                               && std::isfinite(el.period)
                               && std::isfinite(el.time_to_periapsis);
    const bool open_valid = !std::isnan(el.semi_major_axis) && !std::isnan(el.time_to_periapsis);
    if (!finite || !(el.closed() ? closed_finite : open_valid)) {
        return std::nullopt;
    }
    return el;
}

std::vector<int32_t> find_primaries(std::span<const uint32_t> named, const double* mass,
        const Vec* pos) {
    std::vector<int32_t> primaries(named.size(), -1);
    for (uint32_t rank = 0; rank < named.size(); rank++) {
        const uint32_t k = named[rank];
        const auto primary =
                find_primary(mass[k], pos[k], named, mass, pos, [k](uint32_t i) { return i == k; });
        if (primary) {
            primaries[rank] = static_cast<int32_t>(*primary);
        }
    }
    return primaries;
}

bool retrograde(double angular_momentum, const Attractor& attractor,
        std::span<const uint32_t> named, std::span<const int32_t> primaries, const Vec* pos,
        const Vec* vel) {
    const int32_t outer = primaries[attractor.primary_rank];
    if (outer < 0) {
        return false;
    }
    const uint32_t j = attractor.primary, k = named[outer];
    const double primary_angular_momentum = (pos[j] - pos[k]).cross(vel[j] - vel[k]);
    return primary_angular_momentum != 0.0
           && (primary_angular_momentum > 0.0) != (angular_momentum > 0.0);
}

void trace(const Elements& el, Vec focus, const ScreenTransform& transform,
        sf::Vector2f window_size, std::vector<Segment>& out, double tolerance_px,
        double margin_px) {
    using namespace Constants::Graphics;
    if (!(el.semi_latus_rectum > 0.0) || !std::isfinite(el.semi_latus_rectum)
            || !(transform.pixels_per_meter > 0.0)) {
        return;
    }
    // Closed orbits from periapsis to periapsis, open ones out to a few times their distance
    double nu_begin = 0.0, nu_end = 2.0 * PI;
    if (!el.closed()) {
        const double e = el.eccentricity;
        const double r_max = std::max(ORBIT_OPEN_DISTANCE_FACTOR * el.distance,
                ORBIT_OPEN_PERIAPSIS_FACTOR * el.periapsis);
        const double nu_asymptote = std::acos(-1.0 / e);
        nu_end = std::min(nu_asymptote - 1e-9,
                std::acos(std::clamp((el.semi_latus_rectum / r_max - 1.0) / e, -1.0, 1.0)));
        nu_begin = -nu_end;
    }

    const Tracer tracer(el, focus, transform, window_size, tolerance_px, margin_px, out);
    double nu0 = nu_begin;
    Vec p0 = tracer.screen_at(nu0);
    for (uint32_t i = 1; i <= ORBIT_INITIAL_SEGMENTS; i++) {
        const double nu1 = nu_begin + (nu_end - nu_begin) * i / ORBIT_INITIAL_SEGMENTS;
        const Vec p1 = tracer.screen_at(nu1);
        tracer.refine(nu0, nu1, p0, p1, 0);
        nu0 = nu1;
        p0 = p1;
    }
}

void append_dashes(std::span<const Segment> segments, float dash_px, float gap_px,
        sf::Color color, sf::VertexArray& lines) {
    if (!(dash_px > 0.f && gap_px > 0.f)) {
        return;
    }
    bool in_dash = true;
    double remaining = dash_px;  // of the current dash or gap
    for (const Segment& segment : segments) {
        const sf::Vector2f direction = segment.b - segment.a;
        const double length = Vec(direction).length();
        const auto append = [&](double t0, double t1) {
            lines.append({segment.a + direction * static_cast<float>(t0 / length), color});
            lines.append({segment.a + direction * static_cast<float>(t1 / length), color});
        };
        double t = 0.0;
        // Every dash or gap that ends inside the segment
        while (remaining < length - t) {
            if (in_dash && remaining > 0.0) {
                append(t, t + remaining);
            }
            t += remaining;
            in_dash = !in_dash;
            remaining = in_dash ? dash_px : gap_px;
        }
        if (in_dash && t < length) {
            append(t, length);
        }
        remaining -= length - t;
    }
}
}  // namespace Orbit
