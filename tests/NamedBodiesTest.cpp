#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "Check.hpp"
#include "Constants/Constants.hpp"
#include "Logger/Logger.hpp"
#include "NamedBodies/NamedBodies.hpp"
#include "Selector/Selector.hpp"
#include "Text/Text.hpp"

using Vector = sf::Vector2<double>;


namespace {
// Bodies with these ids and masses at the origin, at rest
Bodies make_bodies(std::vector<std::string> ids, std::vector<double> masses) {
    const size_t n = ids.size();
    return Bodies(std::move(ids), std::move(masses), std::vector<Vector>(n),
            std::vector<Vector>(n));
}

// `count` ids `prefix` + number, of mass `mass`, appended
void add_numbered(std::vector<std::string>& ids, std::vector<double>& masses,
        const std::string& prefix, uint32_t first, uint32_t count, double mass) {
    for (uint32_t i = first; i < first + count; i++) {
        ids.push_back(prefix + std::to_string(i));
        masses.push_back(mass);
    }
}

void test_id_group() {
    CHECK(id_group("ring-12") == "ring");
    CHECK(id_group("S55") == "S");
    CHECK(id_group("G1-d123") == "G1-d");
    CHECK(id_group("Kepler-16b") == "Kepler-16b");
    CHECK(id_group("123") == "");
    CHECK(id_group("ring_7") == "ring");
    CHECK(id_group("moonlet-0") == "moonlet");
    CHECK(id_group("TRAPPIST-1") == "TRAPPIST");
    CHECK(id_group("Sgr A*") == "Sgr A*");
    CHECK(id_group("") == "");
    CHECK(id_group("ring-") == "ring-");
    CHECK(id_group("-7") == "");
    CHECK(id_group("Ŋ-42") == "Ŋ");
}

void test_id_number() {
    CHECK(id_number("ring-12") == 12u);
    CHECK(id_number("S55") == 55u);
    CHECK(id_number("moonlet-007") == 7u);
    CHECK(id_number("123") == 123u);
    CHECK(!id_number("Kepler-16b") && !id_number("Sgr A*") && !id_number(""));
    CHECK(id_number("p18446744073709551615") == 18446744073709551615u);
    CHECK(!id_number("p18446744073709551616"));  // doesn't fit in 64 bits
}

// Ids are decoded as UTF-8, and a byte that doesn't fit doesn't swallow what follows it
void test_from_utf8() {
    using U32 = std::u32string;
    const auto decoded = [](std::string_view utf8) { return Text::from_utf8(utf8).toUtf32(); };
    CHECK(decoded("Earth") == U"Earth");
    CHECK(decoded("") == U"");
    CHECK(decoded("\xC5\x8A-42") == U"\u014A-42");             // Ŋ-42
    CHECK(decoded("\xF0\x9F\x98\x80") == U32(1, U'\U0001F600'));  // 4 bytes
    CHECK(decoded("B\xE9\n[") == U"B\uFFFD\n[");               // Latin-1 é, then a new line
    CHECK(decoded("\xE2\x82x") == U"\uFFFDx");                  // cut short
    CHECK(decoded("\x80\xBF") == U"\uFFFD\uFFFD");               // continuation bytes alone
    CHECK(decoded("\xC0\xAF") == U"\uFFFD");                     // overlong /
    CHECK(decoded("\xED\xA0\x80") == U"\uFFFD");                 // a surrogate
    CHECK(decoded("\xF4\x90\x80\x80") == U"\uFFFD");             // beyond U+10FFFF
}

void test_find_named_bodies() {
    using V = std::vector<uint32_t>;
    const auto count_named = [](const std::vector<std::string>& ids) {
        return find_named_bodies(make_bodies(ids, std::vector<double>(ids.size(), 1.0)), 64, 8,
                1024)
                .size();
    };
    {
        std::vector<std::string> ids = {"Sun", "Earth"};
        std::vector<double> masses = {2e30, 6e24};
        add_numbered(ids, masses, "ring-", 0, 100, 1e15);
        CHECK(find_named_bodies(make_bodies(ids, masses), 64, 8, 1024) == V({0, 1}));
    }
    {
        // Catalogue numbers, with gaps, name each S-star
        std::vector<std::string> ids;
        std::vector<double> masses;
        for (uint32_t k = 1; k <= 37; k++) {
            ids.push_back("S" + std::to_string(k * k));
            masses.push_back(2e31);
        }
        ids.push_back("Sgr A*");
        masses.push_back(8e36);
        const auto named = find_named_bodies(make_bodies(ids, masses), 64, 8, 1024);
        CHECK(named.size() == 38 && named.front() == 37);
    }
    {
        // Maxwell's moonlets, numbered in sequence, are a population, unlike their planet
        std::vector<std::string> ids = {"Saturn"};
        std::vector<double> masses = {5.7e26};
        add_numbered(ids, masses, "moonlet-", 0, 36, 1e18);
        CHECK(find_named_bodies(make_bodies(ids, masses), 64, 8, 1024) == V({0}));
    }
    {
        std::vector<std::string> ids;
        std::vector<double> masses;
        add_numbered(ids, masses, "p", 0, 100, 1.0);
        CHECK(find_named_bodies(make_bodies(ids, masses), 64, 8, 1024).empty());
    }
    // A few bodies numbered in sequence are named: three suns, two black holes
    const auto numbered = [](const std::string& prefix, uint32_t first, uint32_t count,
                                  uint32_t step) {
        std::vector<std::string> ids;
        for (uint32_t k = 0; k < count; k++) {
            ids.push_back(prefix + std::to_string(first + k * step));
        }
        return ids;
    };
    CHECK(count_named(numbered("star-", 0, 3, 1)) == 3);
    CHECK(count_named(numbered("BH-", 1, 2, 1)) == 2);
    CHECK(count_named(numbered("star-", 0, 8, 1)) == 8);
    CHECK(count_named(numbered("star-", 0, 9, 1)) == 0);
    // Groups of up to the maximum size are named, unless numbered in sequence
    CHECK(count_named(numbered("moonlet-", 0, 64, 2)) == 64);
    CHECK(count_named(numbered("moonlet-", 0, 65, 2)) == 0);
    // Not sequences: a member without a number, or a number twice
    std::vector<std::string> unnumbered = numbered("star-", 1, 9, 1);
    unnumbered.push_back("star");
    CHECK(count_named(unnumbered) == 10);
    std::vector<std::string> twice = numbered("star-", 1, 9, 1);
    twice.push_back("star_9");
    CHECK(count_named(twice) == 10);

    CHECK(find_named_bodies(make_bodies({"a", "b", "c", "d"}, {1, 2, 1, 2}), 64, 8, 1024)
            == V({1, 3, 0, 2}));
    CHECK(find_named_bodies(make_bodies({"a", "b", "c", "d", "e"}, {1, 5, 3, 4, 2}), 64, 8, 2)
            == V({1, 3}));
    CHECK(find_named_bodies(make_bodies({"x", "y", "z"}, {0, 1, 0}), 64, 8, 1024)
            == V({1, 0, 2}));
    CHECK(find_named_bodies(make_bodies({}, {}), 64, 8, 1024).empty());
}

void test_marker_diameter() {
    const double max = 2e30;
    CHECK_NEAR(marker_diameter(2e30, max, 1.f, true), 13.0, 1e-4);
    CHECK_NEAR(marker_diameter(2e21, max, 1.f, true), 3.0, 1e-4);
    CHECK_NEAR(marker_diameter(2e30 * std::pow(10.0, -4.5), max, 1.f, true), 8.0, 1e-4);
    CHECK_NEAR(marker_diameter(1.0, max, 1.f, true), 3.0, 1e-4);
    CHECK_NEAR(marker_diameter(0.0, max, 1.f, true), 3.0, 1e-4);
    CHECK_NEAR(marker_diameter(2e30, 0.0, 1.f, true), 3.0, 1e-4);
    for (const double mass : {2e30, 2e21, 0.0}) {
        CHECK_NEAR(marker_diameter(mass, max, 1.f, false), 3.0, 1e-4);
        CHECK_NEAR(marker_diameter(mass, max, 5.f, true),
                marker_diameter(mass, max, 1.f, true) + 4.0, 1e-4);
    }
}

void test_pick_marker() {
    using Constants::Graphics::BODY_COLOR;
    // The Moon, the Earth, the Sun, two equal stars and a massless probe
    const std::vector<double> mass = {7e22, 6e24, 2e30, 1e30, 1e30, 0.0};
    const std::vector<uint32_t> named = {2, 3, 4, 1, 0, 5};  // most massive first
    const std::vector<sf::Vector2f> positions = {{103.f, 100.f}, {100.f, 100.f}, {500.f, 500.f},
            {300.f, 300.f}, {306.f, 300.f}, {700.f, 100.f}};
    sf::VertexArray vertices(sf::PrimitiveType::Points);
    for (const sf::Vector2f pos : positions) {
        vertices.append({pos, BODY_COLOR});
    }
    const auto pick = [&](sf::Vector2f cursor, bool scale_by_mass = true) {
        return pick_marker(cursor, named, mass.data(), vertices, 1.f, scale_by_mass);
    };
    // Marker radii: the Earth 3.43 px, the Moon 2.36 px, the stars 6.33 px, the probe 1.5 px,
    // with a 3 px margin
    CHECK(pick({103.f, 100.f}) == 1u);  // on the Moon, but also on the heavier Earth
    CHECK(pick({106.f, 100.f}) == 1u);  // nearer the Moon, still within the Earth's margin
    CHECK(pick({108.f, 100.f}) == 0u);  // only the Moon
    CHECK(pick({305.f, 300.f}) == 4u);  // equal masses: the nearest edge
    CHECK(pick({301.f, 300.f}) == 3u);
    CHECK(pick({704.4f, 100.f}) == 5u);
    CHECK(!pick({704.6f, 100.f}));
    CHECK(!pick({900.f, 800.f}));
    // Without mass scaling every marker has a 1.5 px radius
    CHECK(pick({102.f, 100.f}, false) == 1u);
    CHECK(pick({105.5f, 100.f}, false) == 0u);
    CHECK(!pick_marker({100.f, 100.f}, {}, mass.data(), vertices, 1.f, true));
}

void test_place_labels() {
    const sf::Vector2f window(1600.f, 900.f), size(40.f, 10.f);
    const float gap = 4.f;
    using Placements = std::vector<LabelPlacement>;
    const auto shown = [](const Placements& placements) {
        return std::count_if(placements.begin(), placements.end(),
                [](const LabelPlacement& p) { return p.position.has_value(); });
    };
    // Right of a marker of radius 5 at (100, 100), centred on it vertically: (109..149, 95..105)
    const Placements one = place_labels(std::vector<LabelBox>{{{100.f, 100.f}, 5.f, size}},
            window, 128, gap);
    CHECK(one.size() == 1 && one[0].position == sf::Vector2f(109.f, 95.f) && !one[0].more);

    // A label that would overlap one placed before it is hidden, and that one gets a "+". A hidden
    // label hides nothing: the third one doesn't overlap the first. Labels LABEL_SPACING_PX apart
    // both fit.
    const Placements close = place_labels(std::vector<LabelBox>{{{100.f, 100.f}, 5.f, size},
                                                  {{120.f, 105.f}, 3.f, size},
                                                  {{150.f, 105.f}, 3.f, size},
                                                  {{100.f, 112.f}, 5.f, size},
                                                  {{100.f, 111.5f}, 5.f, size}},
            window, 128, gap);
    CHECK(close[0].position == sf::Vector2f(109.f, 95.f) && close[0].more);
    CHECK(!close[1].position && !close[1].more);
    CHECK(close[2].position == sf::Vector2f(157.f, 100.f) && !close[2].more);
    CHECK(close[3].position == sf::Vector2f(109.f, 107.f) && !close[3].more);
    CHECK(!close[4].position);  // overlaps the first and the fourth: the first has the "+"

    // Of the labels a hidden one overlaps, the first gets the "+"
    const Placements between = place_labels(std::vector<LabelBox>{{{100.f, 100.f}, 5.f, size},
                                                    {{100.f, 113.f}, 5.f, size},
                                                    {{110.f, 106.f}, 3.f, size}},
            window, 128, gap);
    CHECK(between[0].more && !between[1].more && between[1].position && !between[2].position);

    // Labels never move: they may run off the window, but are skipped outside it
    const Placements edges = place_labels(std::vector<LabelBox>{{{1590.f, 100.f}, 5.f, size},
                                                  {{1597.f, 300.f}, 5.f, size},
                                                  {{100.f, -3.f}, 5.f, size},
                                                  {{100.f, -6.f}, 5.f, size},
                                                  {{-50.f, 500.f}, 5.f, size}},
            window, 128, gap);
    CHECK(edges[0].position == sf::Vector2f(1599.f, 95.f));
    CHECK(!edges[1].position && edges[2].position == sf::Vector2f(109.f, -8.f));
    CHECK(!edges[3].position && !edges[4].position);

    // Labels apart from each other all fit, up to the maximum
    std::vector<LabelBox> apart;
    for (int i = 0; i < 10; i++) {
        apart.push_back({{100.f, 50.f + 80.f * i}, 5.f, size});
    }
    CHECK(shown(place_labels(apart, window, 128, gap)) == 10);
    const Placements limited = place_labels(apart, window, 3, gap);
    CHECK(shown(limited) == 3 && limited[0].position && limited[1].position && limited[2].position);
    CHECK(place_labels({}, window, 128, gap).empty());
}

void test_selector() {
    using namespace Constants::Graphics;
    Bodies bodies({"a", "b", "c", "d"}, {1.0, 3.0, 0.0, 0.0},
            {{0.0, 0.0}, {10.0, 0.0}, {20.0, 4.0}, {30.0, 8.0}},
            {{1.0, 0.0}, {0.0, 2.0}, {3.0, 4.0}, {5.0, 6.0}});
    const std::vector<Vector> positions = {bodies.pos(0), bodies.pos(1), bodies.pos(2),
            bodies.pos(3)};
    sf::VertexArray vertices(sf::PrimitiveType::Points);
    for (const float x : {10.f, 20.f, 30.f, 40.f}) {
        vertices.append({{x, 10.f}, BODY_COLOR});
    }
    Selector selector(bodies, positions, vertices);
    selector.set_show(true);

    selector.select_body(1);
    auto stats = selector.compute_stats();
    CHECK(stats.n == 1 && selector.num_selected() == 1);
    CHECK(selector.selection_mask() == std::vector<uint8_t>({0, 1, 0, 0}));
    CHECK(selector.selected_body() == 1u);
    CHECK(stats.heaviest == 1);
    CHECK(stats.center_of_mass == bodies.pos(1) && stats.weighted_velocity == bodies.vel(1));
    CHECK(vertices[1].color == SELECT_COLOR && vertices[0].color == BODY_COLOR);

    selector.select(sf::FloatRect({0.f, 0.f}, {100.f, 100.f}));
    stats = selector.compute_stats();
    CHECK(stats.n == 4 && stats.heaviest == 1);
    CHECK(selector.selection_mask() == std::vector<uint8_t>({1, 1, 1, 1}));
    CHECK(stats.center_of_mass == Vector(7.5, 0.0));  // (1 * 0 + 3 * 10) / 4

    // Massless: the body's own position and velocity, not the origin
    selector.select_body(2);
    stats = selector.compute_stats();
    CHECK(stats.n == 1 && stats.total_mass == 0.0 && stats.heaviest == 2);
    CHECK(stats.center_of_mass == bodies.pos(2) && stats.weighted_velocity == bodies.vel(2));
    CHECK(selector.selection_mask() == std::vector<uint8_t>({0, 0, 1, 0}));
    CHECK(vertices[0].color == BODY_COLOR && vertices[1].color == BODY_COLOR);
    CHECK(vertices[2].color == SELECT_COLOR && vertices[3].color == BODY_COLOR);

    // Several massless bodies: their plain mean
    selector.select(sf::FloatRect({25.f, 0.f}, {100.f, 100.f}));
    stats = selector.compute_stats();
    CHECK(stats.n == 2 && stats.heaviest == 2);
    CHECK(stats.center_of_mass == Vector(25.0, 6.0) && stats.weighted_velocity == Vector(4.0, 5.0));

    // Hidden selections keep the body colour
    selector.set_show(false);
    selector.select_body(0);
    CHECK(selector.selection_mask()[0] == 1 && vertices[0].color == BODY_COLOR);
}
}  // namespace


int main() {
    Log::verbosity = Log::Verbosity::WARNING;
    test_id_group();
    test_id_number();
    test_from_utf8();
    test_find_named_bodies();
    test_marker_diameter();
    test_pick_marker();
    test_place_labels();
    test_selector();
    return Check::result();
}
