#include "Orbit/Orbit.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>

#include "Constants/Constants.hpp"


namespace {
constexpr double PI = std::numbers::pi;
constexpr double INF = std::numeric_limits<double>::infinity();
// Below this eccentricity the periapsis direction is noise: the body is taken to be at it
constexpr double CIRCULAR_ECCENTRICITY = 1e-12;
// Lengths along an orbit beyond this many pixels can't place dashes to the pixel in double
// precision: past it, dashes start wherever a visible stretch of the orbit does
constexpr double MAX_ANCHORED_ARC_PX = 1e14;

// +1 if the motion is counter-clockwise in world coordinates (clockwise on screen), else -1
double motion_sign(const Orbit::Elements& el) {
    return el.angular_momentum >= 0.0 ? 1.0 : -1.0;
}

// 1 - e, from p = a (1 - e^2): > 0 for closed orbits, 0 for parabolas, < 0 for open orbits. Unlike
// e, it keeps its precision near e = 1, where nearly radial orbits have e = 1 to double precision.
double one_minus_e(const Orbit::Elements& el) {
    return el.semi_latus_rectum / (el.semi_major_axis * (1.0 + el.eccentricity));
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

// Time to the next periapsis (inf if leaving on an open orbit), from Kepler's equation, with the
// eccentric anomaly from the radial motion `rv` = r.v rather than from the true anomaly, which
// can't place a nearly radial body. Barker's equation only for exact parabolas: near them, it is
// off by (e - 1) tan^2(nu / 2), which grows without bound far out.
double time_to_periapsis(const Orbit::Elements& el, double rv) {
    const double e = el.eccentricity, a = el.semi_major_axis, p = el.semi_latus_rectum;
    const double mu = el.mu;
    if (el.closed()) {
        if (e < CIRCULAR_ECCENTRICITY) {
            return 0.0;  // at its periapsis, by convention
        }
        // e sin(E) = r.v / sqrt(mu a) and e cos(E) = 1 - r / a
        const double E = std::atan2(rv / std::sqrt(mu * a), 1.0 - el.distance / a);
        const double M = one_minus_e(el) * std::sin(E) + x_minus_sin(E);  // E - e sin(E)
        return (M <= 0.0 ? std::abs(M) : 2.0 * PI - M) / (2.0 * PI) * el.period;
    }
    if (rv >= 0.0) {
        return INF;
    }
    if (a == INF) {
        const double D = rv / std::sqrt(mu * p);  // tan(nu / 2)
        return -0.5 * p * std::sqrt(p / mu) * (D + D * D * D / 3.0);
    }
    // e sinh(F) = r.v / sqrt(-mu a)
    const double minus_a = -a;
    const double F = std::asinh(rv / (e * std::sqrt(mu * minus_a)));
    const double M = -one_minus_e(el) * std::sinh(F) + sinh_minus_x(F);  // e sinh(F) - F
    return -M * minus_a * std::sqrt(minus_a / mu);
}

// Carlson's symmetric elliptic integrals R_F(x, y, z) and R_D(x, y, z), for x, y, z >= 0 with at
// most one of them 0 (z > 0 for R_D), by duplication (Numerical Recipes, 3rd ed., 6.12)
double carlson_rf(double x, double y, double z) {
    constexpr double TOLERANCE = 0.0025;  // relative error below TOLERANCE^6 / 4
    double mean = 0.0, dx = 0.0, dy = 0.0, dz = 0.0;
    for (int i = 0; i < 64; i++) {
        const double sx = std::sqrt(x), sy = std::sqrt(y), sz = std::sqrt(z);
        const double lambda = sx * (sy + sz) + sy * sz;
        x = 0.25 * (x + lambda);
        y = 0.25 * (y + lambda);
        z = 0.25 * (z + lambda);
        mean = (x + y + z) / 3.0;
        dx = (mean - x) / mean;
        dy = (mean - y) / mean;
        dz = (mean - z) / mean;
        if (!(std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) > TOLERANCE)) {
            break;
        }
    }
    const double e2 = dx * dy - dz * dz, e3 = dx * dy * dz;
    return (1.0 + (e2 / 24.0 - 0.1 - 3.0 / 44.0 * e3) * e2 + e3 / 14.0) / std::sqrt(mean);
}

double carlson_rd(double x, double y, double z) {
    constexpr double TOLERANCE = 0.0015;
    double sum = 0.0, factor = 1.0, mean = 0.0, dx = 0.0, dy = 0.0, dz = 0.0;
    for (int i = 0; i < 64; i++) {
        const double sx = std::sqrt(x), sy = std::sqrt(y), sz = std::sqrt(z);
        const double lambda = sx * (sy + sz) + sy * sz;
        sum += factor / (sz * (z + lambda));
        factor *= 0.25;
        x = 0.25 * (x + lambda);
        y = 0.25 * (y + lambda);
        z = 0.25 * (z + lambda);
        mean = 0.2 * (x + y + 3.0 * z);
        dx = (mean - x) / mean;
        dy = (mean - y) / mean;
        dz = (mean - z) / mean;
        if (!(std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) > TOLERANCE)) {
            break;
        }
    }
    const double ea = dx * dy, eb = dz * dz, ec = ea - eb, ed = ea - 6.0 * eb, ee = ed + 2.0 * ec;
    return 3.0 * sum
           + factor
                     * (1.0 + ed * (-3.0 / 14.0 + 9.0 / 88.0 * ed - 9.0 / 52.0 * dz * ee)
                             + dz * (ee / 6.0 + dz * (-9.0 / 22.0 * ec + dz * 3.0 / 26.0 * ea)))
                     / (mean * std::sqrt(mean));
}

// The conics' lengths from the periapsis, in Carlson's forms scaled by p, which keep them precise
// near e = 1: the ellipse's and the hyperbola's both tend to the parabola's there.
// An ellipse, x = a cos E and y = b sin E from its centre, has ds = a sqrt(1 - e^2 cos^2 E) dE,
// and for 0 <= E <= pi/2 a length of p sin E (R_F(X) + (e^2 / 3) sin^2 E R_D(X)), with
// X = (k cos^2 E, k + e^2 sin^2 E, k) and k = 1 - e^2 (DLMF 19.25.9 with a negative parameter)
double ellipse_arc_length(const Orbit::Elements& el, double nu) {
    const double e = el.eccentricity, p = el.semi_latus_rectum, one_minus = one_minus_e(el);
    const double k = one_minus * (1.0 + e);
    const auto from_periapsis = [e, p, k](double E) {
        const double s = std::sin(E), c = std::cos(E);
        const double x = k * c * c, y = k + e * e * s * s;
        return p * s * (carlson_rf(x, y, k) + e * e / 3.0 * s * s * carlson_rd(x, y, k));
    };
    const double quarter = from_periapsis(PI / 2.0);  // of the perimeter
    const double turns = std::floor((nu + PI) / (2.0 * PI));
    const double nu_in_turn = nu - 2.0 * PI * turns;  // [-pi, pi)
    const double E = std::abs(2.0 * std::atan2(std::sqrt(one_minus) * std::sin(nu_in_turn / 2.0),
                                     std::sqrt(1.0 + e) * std::cos(nu_in_turn / 2.0)));
    // Symmetric about E = pi/2
    const double length =
            E <= PI / 2.0 ? from_periapsis(E) : 2.0 * quarter - from_periapsis(PI - E);
    return 4.0 * quarter * turns + std::copysign(length, nu_in_turn);
}

// A hyperbola, x = |a| cosh F and y = b sinh F from its centre, has
// ds = |a| sqrt(k + e^2 sinh^2 F) dF with k = e^2 - 1. Through tan(psi) = sinh F and by parts, its
// length is p (sinh F sqrt(k + tanh^2 F) / k - (tanh^3 F / 3) R_D(k / cosh^2 F, k + tanh^2 F, k)).
double hyperbola_arc_length(const Orbit::Elements& el, double nu) {
    const double e = el.eccentricity, p = el.semi_latus_rectum, e_minus_1 = -one_minus_e(el);
    const double k = e_minus_1 * (e + 1.0);
    const double F = 2.0 * std::atanh(std::sqrt(e_minus_1 / (e + 1.0)) * std::tan(nu / 2.0));
    const double t = std::tanh(F), c = std::cosh(F);
    const double y = k + t * t;
    return p * (std::sinh(F) * std::sqrt(y) / k - t * t * t / 3.0 * carlson_rd(k / (c * c), y, k));
}

// A parabola has ds = (p / 2) sec^3(nu / 2) dnu
double parabola_arc_length(const Orbit::Elements& el, double nu) {
    const double t = std::tan(nu / 2.0);
    return el.semi_latus_rectum / 2.0 * (t * std::sqrt(1.0 + t * t) + std::asinh(t));
}

// The orbit's radius at true anomaly nu, from 1 + e cos(nu) = (1 - e) + 2 e cos^2(nu / 2), which
// unlike the left side keeps nearly radial orbits' apoapses and asymptotes
double radius_at(const Orbit::Elements& el, double nu) {
    const double half_cos = std::cos(nu / 2.0);
    return el.semi_latus_rectum
           / (one_minus_e(el) + 2.0 * el.eccentricity * half_cos * half_cos);
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
    Tracer(const Orbit::Elements& el, Orbit::Vec focus, const ScreenTransform& transform,
            sf::Vector2f window_size, double tolerance_px, double margin_px,
            std::vector<Orbit::Segment>& out)
            : el(el), focus(focus), transform(transform), lo(-margin_px, -margin_px),
              hi(Orbit::Vec(window_size) + Orbit::Vec(margin_px, margin_px)),
              tolerance_px(tolerance_px), out(out) {}

    Orbit::Vec screen_at(double nu) const {
        return transform.to_screen(focus + el.position_at(nu));
    }

    // Whether the ring between the orbit's periapsis and its extent() crosses the window: an orbit
    // around a body outside the window, or a large one around a body near it, may not reach it
    bool ring_reaches_window() const {
        const Orbit::Vec centre = transform.to_screen(focus);
        const Orbit::Vec nearest(std::clamp(centre.x, lo.x, hi.x),
                std::clamp(centre.y, lo.y, hi.y));
        const Orbit::Vec farthest(centre.x < 0.5 * (lo.x + hi.x) ? hi.x : lo.x,
                centre.y < 0.5 * (lo.y + hi.y) ? hi.y : lo.y);
        const double ppm = transform.pixels_per_meter;
        return (nearest - centre).length() <= Orbit::extent(el) * ppm
               && (farthest - centre).length() >= el.periapsis * ppm;
    }

    // Whether the arc nu0..nu1 may reach the window: false for boxes that aren't finite
    bool reaches_window(double nu0, double nu1) const {
        const Box box = sector_box(el, focus, nu0, nu1);
        const Orbit::Vec box_lo = transform.to_screen(box.lo), box_hi = transform.to_screen(box.hi);
        return box_hi.x >= lo.x && box_lo.x <= hi.x && box_hi.y >= lo.y && box_lo.y <= hi.y;
    }

    void refine(double nu0, double nu1, Orbit::Vec p0, Orbit::Vec p1, uint32_t depth) {
        if (!reaches_window(nu0, nu1)) {
            return;
        }
        const double nu_mid = 0.5 * (nu0 + nu1);
        const Orbit::Vec p_mid = screen_at(nu_mid);
        if (depth >= Constants::Graphics::ORBIT_MAX_DEPTH
                || (p_mid - 0.5 * (p0 + p1)).length() <= tolerance_px) {
            keep(nu0, nu1, p0, p1);
            return;
        }
        refine(nu0, nu_mid, p0, p_mid, depth + 1);
        refine(nu_mid, nu1, p_mid, p1, depth + 1);
    }

private:
    // Keeps a chord's part in the window, with how far along the orbit it starts: on from the
    // chord before it if they meet, else measured from the periapsis
    void keep(double nu0, double nu1, Orbit::Vec p0, Orbit::Vec p1) {
        if (nu0 != last_nu1) {
            arc_end_px = Orbit::arc_length(el, nu0) * transform.pixels_per_meter;
            if (!(std::abs(arc_end_px) < MAX_ANCHORED_ARC_PX)) {
                arc_end_px = 0.0;
            }
        }
        if (const auto clipped = clip(p0, p1, lo, hi)) {
            out.push_back({sf::Vector2f(clipped->first), sf::Vector2f(clipped->second),
                    arc_end_px + (clipped->first - p0).length()});
        }
        arc_end_px += (p1 - p0).length();
        last_nu1 = nu1;
    }

    const Orbit::Elements& el;
    const Orbit::Vec focus;
    const ScreenTransform& transform;
    const Orbit::Vec lo, hi;  // the window grown by the margin
    const double tolerance_px;
    std::vector<Orbit::Segment>& out;
    double last_nu1 = std::numeric_limits<double>::quiet_NaN();  // where the last chord ended
    double arc_end_px = 0.0;                                     // and how far along the orbit
};

// Whether a body at `distance` from P (of mass `mass`) is inside P's sphere of influence, P being
// `outer_distance` from the body it orbits (of mass `outer_mass`): whether P's m / d^3 at the
// body is at least that one's at P. False for NaNs.
bool inside_sphere(double mass, double distance, double outer_mass, double outer_distance) {
    return mass / (distance * distance * distance)
           >= outer_mass / (outer_distance * outer_distance * outer_distance);
}

// Whether the named body of rank `rank` belongs to a target (`excluded` is empty for none)
bool is_excluded(std::span<const uint8_t> excluded, uint32_t rank) {
    return !excluded.empty() && excluded[rank] != 0;
}

// From the named body of rank `rank` up its chain of primaries, to the first body whose sphere of
// influence a target at `target_pos` is in, short of the target's own bodies
uint32_t climb_primaries(uint32_t rank, Orbit::Vec target_pos, std::span<const uint32_t> named,
        std::span<const int32_t> primaries, const double* mass, const Orbit::Vec* pos,
        std::span<const uint8_t> excluded) {
    for (uint32_t hop = 0; hop < Orbit::MAX_PRIMARY_HOPS && primaries[rank] >= 0; hop++) {
        const auto outer = static_cast<uint32_t>(primaries[rank]);
        const uint32_t p = named[rank], q = named[outer];
        if (is_excluded(excluded, outer)
                || inside_sphere(mass[p], (pos[p] - target_pos).length(), mass[q],
                        (pos[q] - pos[p]).length())) {
            break;
        }
        rank = outer;
    }
    return rank;
}
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
    const double rv = rel_pos.dot(rel_vel);
    const Vec e_vec = ((speed_squared - mu / distance) * rel_pos - rv * rel_vel) / mu;
    const double p = el.angular_momentum * el.angular_momentum / mu;
    if (!(p > 0.0)) {
        return std::nullopt;
    }
    // The energy, not e, tells closed orbits from open ones and gives the semi-major axis: nearly
    // radial orbits have e = 1 to double precision, bound or not, and elsewhere near e = 1 the
    // energy's sign is as reliable as e's at the periapsis, and more so farther out
    const double energy = speed_squared / 2.0 - mu / distance;
    double e = e_vec.length();
    if (energy < 0.0) {
        e = std::min(e, std::nextafter(1.0, 0.0));
    }
    else if (energy > 0.0) {
        e = std::max(e, std::nextafter(1.0, 2.0));
    }
    else {
        e = 1.0;
    }
    el.eccentricity = e;
    el.semi_latus_rectum = p;
    el.semi_major_axis = energy == 0.0 ? INF : -mu / (2.0 * energy);
    el.periapsis = p / (1.0 + e);
    el.apoapsis = el.closed() ? el.semi_major_axis * (1.0 + e) : INF;
    if (e < CIRCULAR_ECCENTRICITY) {
        el.periapsis_angle = std::atan2(rel_pos.y, rel_pos.x);
        el.true_anomaly = 0.0;
    }
    else {
        el.periapsis_angle = std::atan2(e_vec.y, e_vec.x);
        const double cos_w = std::cos(el.periapsis_angle), sin_w = std::sin(el.periapsis_angle);
        const double nu = motion_sign(el) * std::atan2(cos_w * rel_pos.y - sin_w * rel_pos.x,
                                                    cos_w * rel_pos.x + sin_w * rel_pos.y);
        el.true_anomaly = nu == -PI ? PI : nu;
    }
    if (el.closed()) {
        const double a = el.semi_major_axis;
        el.period = 2.0 * PI * a * std::sqrt(a / mu);
        el.excess_speed = 0.0;
    }
    else {
        el.period = INF;
        el.excess_speed = std::sqrt(2.0 * std::max(0.0, energy));
    }
    el.time_to_periapsis = time_to_periapsis(el, rv);

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

double extent(const Elements& el) {
    using namespace Constants::Graphics;
    return el.closed() ? el.apoapsis
                       : std::max(ORBIT_OPEN_DISTANCE_FACTOR * el.distance,
                                 ORBIT_OPEN_PERIAPSIS_FACTOR * el.periapsis);
}

double arc_length(const Elements& el, double nu) {
    if (el.eccentricity < 1.0) {
        return ellipse_arc_length(el, nu);
    }
    if (el.eccentricity > 1.0) {
        return hyperbola_arc_length(el, nu);
    }
    return parabola_arc_length(el, nu);
}

std::optional<uint32_t> find_primary(double target_mass, Vec target_pos,
        std::span<const uint32_t> named, const double* mass, const Vec* pos,
        std::span<const uint8_t> excluded) {
    std::optional<uint32_t> primary;
    double best_score = 0.0;
    for (uint32_t rank = 0; rank < named.size(); rank++) {
        const uint32_t k = named[rank];
        if (mass[k] < target_mass) {
            break;  // so are the bodies after it
        }
        if (!(mass[k] > 0.0) || is_excluded(excluded, rank)) {
            continue;
        }
        const double distance = (pos[k] - target_pos).length();
        const double score = mass[k] / (distance * distance * distance);
        if (distance > 0.0 && score > best_score) {
            primary = rank;
            best_score = score;
        }
    }
    return primary;
}

std::vector<int32_t> find_primaries(std::span<const uint32_t> named, const double* mass,
        const Vec* pos) {
    std::vector<int32_t> primaries(named.size(), -1);
    // Heaviest first: the chain a body climbs is final
    for (uint32_t rank = 0; rank < named.size(); rank++) {
        const uint32_t k = named[rank];
        if (const auto primary = find_primary(mass[k], pos[k], named.first(rank), mass, pos)) {
            primaries[rank] = static_cast<int32_t>(
                    climb_primaries(*primary, pos[k], named, primaries, mass, pos, {}));
        }
    }
    return primaries;
}

std::vector<uint32_t> find_satellites(uint32_t rank, std::span<const uint32_t> named,
        std::span<const int32_t> primaries, const double* mass, const Vec* pos) {
    assert(primaries.size() == named.size());
    const uint32_t j = named[rank];
    // What the body orbits: its primary, or for the first of equal masses, which has none, the
    // others, listed after it
    int32_t outer = primaries[rank];
    if (outer < 0) {
        if (const auto twin = find_primary(mass[j], pos[j], named.subspan(rank + 1), mass, pos)) {
            outer = static_cast<int32_t>(rank + 1 + *twin);
        }
    }
    const double outer_mass = outer >= 0 ? mass[named[outer]] : 0.0;
    const double outer_distance = outer >= 0 ? (pos[named[outer]] - pos[j]).length() : INF;
    // Lighter bodies are listed after it, and their chains reach it through lighter bodies: one as
    // heavy, a twin, has its own satellites
    std::vector<uint32_t> satellites;
    for (uint32_t other = rank + 1; other < named.size(); other++) {
        int32_t body = static_cast<int32_t>(other);
        for (uint32_t hop = 0; hop < MAX_PRIMARY_HOPS && body > static_cast<int32_t>(rank)
                               && mass[named[body]] < mass[j];
                hop++) {
            const int32_t next = primaries[body];
            if (next == static_cast<int32_t>(rank)) {
                if (inside_sphere(mass[j], (pos[named[body]] - pos[j]).length(), outer_mass,
                            outer_distance)) {
                    satellites.push_back(other);
                }
                break;
            }
            body = next;
        }
    }
    return satellites;
}

Target barycentre(std::span<const uint32_t> members, const double* mass, const Vec* pos,
        const Vec* vel) {
    assert(!members.empty());
    const uint32_t first = members.front();
    Target target{0.0, first, {}, {}};
    for (const uint32_t i : members) {
        target.mass += mass[i];
        target.pos += mass[i] * (pos[i] - pos[first]);
        target.vel += mass[i] * (vel[i] - vel[first]);
        if (mass[i] > mass[target.heaviest]) {
            target.heaviest = i;
        }
    }
    if (target.mass > 0.0) {
        target.pos /= target.mass;
        target.vel /= target.mass;
    }
    else {
        for (const uint32_t i : members) {
            target.pos += pos[i] - pos[first];
            target.vel += vel[i] - vel[first];
        }
        target.pos /= static_cast<double>(members.size());
        target.vel /= static_cast<double>(members.size());
    }
    target.pos += pos[first];
    target.vel += vel[first];
    return target;
}

std::optional<Attractor> find_attractor(const Target& target, std::span<const uint32_t> named,
        std::span<const int32_t> primaries, const double* mass, const Vec* pos, const Vec* vel,
        std::span<const uint8_t> excluded) {
    assert(primaries.size() == named.size());
    assert(excluded.empty() || excluded.size() == named.size());
    const std::optional<uint32_t> nearest_primary_rank =
            find_primary(mass[target.heaviest], target.pos, named, mass, pos, excluded);
    if (!nearest_primary_rank) {
        return std::nullopt;
    }
    const uint32_t primary_rank = climb_primaries(*nearest_primary_rank, target.pos, named,
            primaries, mass, pos, excluded);
    const uint32_t j = named[primary_rank];
    const double target_distance = (pos[j] - target.pos).length();
    // The barycentre is summed relative to the primary, which keeps its precision far from the
    // origin
    Attractor attractor{.primary = j, .primary_rank = primary_rank, .inner_count = 0,
            .mass = mass[j], .pos = {}, .vel = {}};
    // Chains that reach the primary start after it
    const auto primary = static_cast<int32_t>(primary_rank);
    for (uint32_t rank = primary_rank + 1; rank < named.size(); rank++) {
        int32_t body = static_cast<int32_t>(rank);
        for (uint32_t hop = 0;
                hop < MAX_PRIMARY_HOPS && body > primary && !is_excluded(excluded, body); hop++) {
            if (primaries[body] == primary) {
                if ((pos[named[body]] - pos[j]).length() < target_distance) {
                    const uint32_t k = named[rank];
                    attractor.inner_count++;
                    attractor.mass += mass[k];
                    attractor.pos += mass[k] * (pos[k] - pos[j]);
                    attractor.vel += mass[k] * (vel[k] - vel[j]);
                }
                break;
            }
            body = primaries[body];
        }
    }
    attractor.pos = pos[j] + attractor.pos / attractor.mass;
    attractor.vel = vel[j] + attractor.vel / attractor.mass;
    return attractor;
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

std::optional<BodyOrbit> body_orbit(uint32_t index, std::optional<uint32_t> rank,
        std::span<const uint32_t> named, std::span<const int32_t> primaries, const double* mass,
        const Vec* pos, const Vec* vel) {
    std::vector<uint32_t> members{index};
    std::vector<uint8_t> excluded(named.size(), 0);
    if (rank) {
        excluded[*rank] = 1;
        for (const uint32_t satellite : find_satellites(*rank, named, primaries, mass, pos)) {
            members.push_back(named[satellite]);
            excluded[satellite] = 1;
        }
    }
    const Target target = barycentre(members, mass, pos, vel);
    const auto attractor = find_attractor(target, named, primaries, mass, pos, vel, excluded);
    if (!attractor) {
        return std::nullopt;
    }
    const auto el = elements(target.pos - attractor->pos, target.vel - attractor->vel,
            Constants::Simulation::G * (target.mass + attractor->mass));
    if (!el) {
        return std::nullopt;
    }
    return BodyOrbit{.attractor = *attractor, .elements = *el,
            .retrograde = retrograde(el->angular_momentum, *attractor, named, primaries, pos, vel),
            .satellite_count = static_cast<uint32_t>(members.size() - 1)};
}

void trace(const Elements& el, Vec focus, const ScreenTransform& transform,
        sf::Vector2f window_size, std::vector<Segment>& out, double tolerance_px,
        double margin_px) {
    using namespace Constants::Graphics;
    if (!(el.semi_latus_rectum > 0.0) || !std::isfinite(el.semi_latus_rectum)
            || !(transform.pixels_per_meter > 0.0)) {
        return;
    }
    // Closed orbits from periapsis to periapsis, open ones out to extent(): there, (1 - e) +
    // 2 e cos^2(nu / 2) = p / extent, which keeps nearly radial orbits' ends finite
    double nu_begin = 0.0, nu_end = 2.0 * PI;
    if (!el.closed()) {
        const double half_cos_squared =
                (el.semi_latus_rectum / extent(el) - one_minus_e(el)) / (2.0 * el.eccentricity);
        nu_end = 2.0 * std::acos(std::sqrt(std::clamp(half_cos_squared, 0.0, 1.0)));
        nu_begin = -nu_end;
    }

    Tracer tracer(el, focus, transform, window_size, tolerance_px, margin_px, out);
    // Zoomed in, most orbits miss the window, and two tests skip them
    if (!std::isfinite(radius_at(el, nu_end)) || !tracer.ring_reaches_window()
            || !tracer.reaches_window(nu_begin, nu_end)) {
        return;
    }
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
    const double period = static_cast<double>(dash_px) + gap_px;
    for (const Segment& segment : segments) {
        const sf::Vector2f direction = segment.b - segment.a;
        const double length = Vec(direction).length();
        if (!(length > 0.0)) {
            continue;
        }
        const auto append = [&](double t0, double t1) {
            lines.append({segment.a + direction * static_cast<float>(t0 / length), color});
            lines.append({segment.a + direction * static_cast<float>(t1 / length), color});
        };
        // Where the segment starts in the pattern: dashes begin at multiples of the period
        double phase = std::fmod(segment.arc_px, period);
        if (phase < 0.0) {
            phase += period;
        }
        double t = 0.0;  // from the segment's start
        while (t < length) {
            if (phase < dash_px) {
                append(t, std::min(length, t + dash_px - phase));
                t += dash_px - phase;
                phase = dash_px;
            }
            else {
                t += period - phase;
                phase = 0.0;
            }
        }
    }
}
}  // namespace Orbit
