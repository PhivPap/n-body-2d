#pragma once

#include <cassert>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "SFML/Graphics.hpp"
#include "SFML/System.hpp"


// Two-body orbits of bodies around the named bodies they orbit, and the conics they trace on
// screen. World y maps straight to screen y, which points down, so a positive angular momentum
// is clockwise on screen.
namespace Orbit {
using Vec = sf::Vector2<double>;

// Osculating two-body elements of a relative state, for any conic. The energy tells closed orbits
// from open ones, and e is kept on its side of 1: nearly radial orbits have e = 1 to double
// precision, and their shape comes from p and a.
struct Elements {
    double mu = 0.0;                 // G (m1 + m2) [m^3/s^2]
    double distance = 0.0;           // |r| [m]
    double speed = 0.0;              // |v| [m/s]
    double angular_momentum = 0.0;   // h = r.x v.y - r.y v.x [m^2/s]; > 0 is clockwise on screen
    double eccentricity = 0.0;
    double semi_latus_rectum = 0.0;  // p = h^2 / mu = a (1 - e^2) [m]
    double semi_major_axis = 0.0;    // -mu / (2 energy): < 0 for open orbits, inf for parabolas [m]
    double periapsis = 0.0;          // p / (1 + e) [m]
    double apoapsis = 0.0;           // a (1 + e) for closed orbits, else inf [m]
    double periapsis_angle = 0.0;    // world direction from the focus to the periapsis [rad]
    double true_anomaly = 0.0;       // from the periapsis in the direction of motion, (-pi, pi]
    double period = 0.0;             // closed orbits, else inf [s]
    double time_to_periapsis = 0.0;  // to the next periapsis, inf if leaving on an open orbit [s]
    double excess_speed = 0.0;       // speed at infinity on open orbits, else 0 [m/s]

    bool closed() const {
        return eccentricity < 1.0;
    }
    // Position relative to the focus at true anomaly `nu` (in the direction of motion)
    Vec position_at(double nu) const;
};

// The elements of a body at `rel_pos`, moving at `rel_vel`, relative to the body it orbits, with
// mu = G (m1 + m2). nullopt unless mu > 0, the distance is > 0, the motion isn't purely radial
// (which has no conic) and the inputs and results are finite, except for the values that are
// inf by definition: an open orbit's apoapsis, period and time to a periapsis it passed. A
// circular orbit's periapsis is where the body is (e < 1e-12, whose periapsis is rounding noise).
std::optional<Elements> elements(Vec rel_pos, Vec rel_vel, double mu);

// How far from its focus trace() draws an orbit: its apoapsis, or how far out an open one goes
double extent(const Elements& el);

// The length of the conic from its periapsis to true anomaly `nu` [m], negative before the
// periapsis; closed orbits count on past it, so nu = 2 pi is one perimeter. Exact (elliptic
// integrals) for every eccentricity.
double arc_length(const Elements& el, double nu);

// Chains of primaries are followed for at most this many hops: equal-mass bodies can be each
// other's primary
constexpr uint32_t MAX_PRIMARY_HOPS = 16;

// The primary of a target of mass `target_mass` at `target_pos`: among the named bodies that
// aren't `excluded`, with a mass > 0 and at least the target's, the one with the largest m / d^3.
// That is roughly the body whose Hill sphere the target is deepest in: the Earth for the Moon,
// although the Sun pulls the Moon harder. Returned as its rank in `named`, which lists body
// indices, most massive first; `mass` and `pos` are indexed by body index.
template <typename Excluded>
std::optional<uint32_t> find_primary(double target_mass, Vec target_pos,
        std::span<const uint32_t> named, const double* mass, const Vec* pos, Excluded&& excluded);

// From a target's primary of rank `primary_rank` (find_primary()) up its chain of `primaries`, to
// the first body whose sphere of influence the target is in. A body P keeps the target if
// m_P / d(target, P)^3 >= m_Q / d(P, Q)^3, Q being P's own primary, that is if the target is within
// 3^(1/3) Hill radii of P: a moon stays with its planet, but a planet around a binary star doesn't
// fall to the lighter star as it passes it. The chain is only climbed to heavier bodies, which
// can't belong to the target: bodies of equal masses can be each other's primary.
uint32_t climb_primaries(uint32_t primary_rank, Vec target_pos, std::span<const uint32_t> named,
        std::span<const int32_t> primaries, const double* mass, const Vec* pos);

// Each named body's primary, as its rank in `named` (-1: none), climbed to the body whose sphere
// of influence it is in
std::vector<int32_t> find_primaries(std::span<const uint32_t> named, const double* mass,
        const Vec* pos);

// The named bodies that orbit the named body of rank `rank`, directly or through each other,
// inside its sphere of influence: the lighter bodies whose chain of primaries reaches it, like the
// Moon for the Earth or Charon, Nix and Hydra for Pluto, but not a planet around a binary of equal
// stars. Returned as body indices, most massive first.
std::vector<uint32_t> find_satellites(uint32_t rank, std::span<const uint32_t> named,
        std::span<const int32_t> primaries, const double* mass, const Vec* pos);

// What orbits: a body, or bodies treated as one, like a planet with its satellites
struct Target {
    double mass;   // total mass [kg], 0 for massless
    Vec pos, vel;  // their centre of mass and mass-weighted velocity
};

// Bodies `members` (body indices, at least one) as one target: their total mass, barycentre and
// mass-weighted velocity (plain means if they are massless), summed relative to the first member,
// which keeps their precision far from the origin
Target barycentre(std::span<const uint32_t> members, const double* mass, const Vec* pos,
        const Vec* vel);

// What a target orbits: its primary, climbed to the body whose sphere of influence it is in, merged
// with the named bodies that orbit that primary inside the target's distance (a Jacobi
// coordinate's attractor)
struct Attractor {
    uint32_t primary;       // body index
    uint32_t primary_rank;  // its position in `named`
    uint32_t inner_count;   // named bodies merged in: orbiting the primary inside the target
    double mass;            // primary plus inner bodies [kg]
    Vec pos, vel;           // their barycentre
};

// The attractor of `target`, given the named bodies' cached `primaries` (find_primaries).
// `excluded(i)`: whether body i belongs to the target. A named body is inner if its chain of
// primaries reaches the target's primary, without passing through the target, from a body that
// is closer to the primary than the target. The arrays are indexed by body index.
template <typename Excluded>
std::optional<Attractor> find_attractor(const Target& target, std::span<const uint32_t> named,
        std::span<const int32_t> primaries, const double* mass, const Vec* pos, const Vec* vel,
        Excluded&& excluded);

// Whether a target with `angular_momentum` around `attractor` goes around the other way than the
// attractor's primary goes around its own primary, like Triton around Neptune. Never for a
// primary without a primary, such as the Sun.
bool retrograde(double angular_momentum, const Attractor& attractor,
        std::span<const uint32_t> named, std::span<const int32_t> primaries, const Vec* pos,
        const Vec* vel);

// World (m) to window pixels, what ViewPort::coords_to_pos_on_viewport does, in double precision
struct ScreenTransform {
    Vec origin;               // world position of the window's top-left corner
    double pixels_per_meter;  // uniform scale

    Vec to_screen(Vec world) const {
        return (world - origin) * pixels_per_meter;
    }
};

struct Segment {
    sf::Vector2f a, b;
    double arc_px = 0.0;  // how far along the orbit `a` is from the periapsis, in window pixels
};

// The conic of `el` around `focus` as window-pixel segments, in order along the orbit from its
// periapsis (closed orbits) or its incoming end (open ones, out to extent()), refined until the
// curve is within `tolerance_px` of its chords, and clipped to the window grown by `margin_px`.
// Where a stretch of them starts, arc_px is arc_length(); along it, the chords add up.
void trace(const Elements& el, Vec focus, const ScreenTransform& transform,
        sf::Vector2f window_size, std::vector<Segment>& out, double tolerance_px,
        double margin_px);

// Dashes along segments, as pairs of vertices for sf::PrimitiveType::Lines. They are laid out by
// the segments' arc_px from the periapsis, so they keep their places on the orbit whatever part of
// it the window shows: a followed body passes them, as it passes the grid's lines.
void append_dashes(std::span<const Segment> segments, float dash_px, float gap_px,
        sf::Color color, sf::VertexArray& lines);


template <typename Excluded>
std::optional<uint32_t> find_primary(double target_mass, Vec target_pos,
        std::span<const uint32_t> named, const double* mass, const Vec* pos, Excluded&& excluded) {
    std::optional<uint32_t> primary;
    double best_score = 0.0;
    for (uint32_t rank = 0; rank < named.size(); rank++) {
        const uint32_t k = named[rank];
        if (!(mass[k] > 0.0) || mass[k] < target_mass || excluded(k)) {
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

template <typename Excluded>
std::optional<Attractor> find_attractor(const Target& target, std::span<const uint32_t> named,
        std::span<const int32_t> primaries, const double* mass, const Vec* pos, const Vec* vel,
        Excluded&& excluded) {
    assert(primaries.size() == named.size());
    const std::optional<uint32_t> nearest_primary_rank =
            find_primary(target.mass, target.pos, named, mass, pos, excluded);
    if (!nearest_primary_rank) {
        return std::nullopt;
    }
    const uint32_t primary_rank =
            climb_primaries(*nearest_primary_rank, target.pos, named, primaries, mass, pos);
    const uint32_t j = named[primary_rank];
    const double target_distance = (pos[j] - target.pos).length();
    // The barycentre is summed relative to the primary, which keeps its precision far from the
    // origin
    Attractor attractor{.primary = j, .primary_rank = primary_rank, .inner_count = 0,
            .mass = mass[j], .pos = {}, .vel = {}};
    for (uint32_t rank = 0; rank < named.size(); rank++) {
        const uint32_t k = named[rank];
        if (rank == primary_rank || excluded(k)) {
            continue;
        }
        int32_t body = static_cast<int32_t>(rank), child = -1;
        for (uint32_t hop = 0; hop < MAX_PRIMARY_HOPS && body >= 0 && !excluded(named[body]);
                hop++) {
            if (body == static_cast<int32_t>(primary_rank)) {
                if (child >= 0 && (pos[named[child]] - pos[j]).length() < target_distance) {
                    attractor.inner_count++;
                    attractor.mass += mass[k];
                    attractor.pos += mass[k] * (pos[k] - pos[j]);
                    attractor.vel += mass[k] * (vel[k] - vel[j]);
                }
                break;
            }
            child = body;
            body = primaries[body];
        }
    }
    attractor.pos = pos[j] + attractor.pos / attractor.mass;
    attractor.vel = vel[j] + attractor.vel / attractor.mass;
    return attractor;
}
}  // namespace Orbit
