#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "scipy"]
# ///
"""Scenario generator for n-body-2d.

Every scenario writes two files into scenarios/ (at the repo root):
    <name>.csv    initial conditions (id,mass,x,y,vel_x,vel_y in SI units)
    <name>.json   config that runs it (algorithm, timestep, softening, theta, zoom, trails, ...)
The config refers to the CSV by file name (the simulator resolves it next to the config), and the
simulator writes the final state to output.csv in the same folder when it exits.

Usage (from anywhere; paths are resolved against the repo root). Needs numpy and scipy
(`pip install numpy scipy`), or let uv install them: `uv run scripts/scenario-generator.py`.
    python3 scripts/scenario-generator.py --list
    python3 scripts/scenario-generator.py                    # generate every scenario
    python3 scripts/scenario-generator.py solar-system 'galaxy-*'
    python3 scripts/scenario-generator.py --scale 0.1 ...    # 10x fewer particles (quick tests)
    python3 scripts/scenario-generator.py --verify solar-system
    ./build/n-body-2d scenarios/solar-system.json

How the configs are chosen (this mirrors the simulator, see src/Simulation/):
  * Algorithm by body count: All Pairs (exact) up to ALL_PAIRS_MAX_BODIES, the GPU Barnes-Hut
    above that. The CPU Barnes-Hut is never the fastest (measured on a Ryzen 9 9950X3D + RTX 5070
    Ti, see estimate_ips() and choose_algorithm()).
  * Softening: each scenario asks for an absolute Plummer length epsilon [m]. The simulator only
    takes `softening_factor` = epsilon / (average pairwise distance), so the generator measures that
    distance the same way the simulator does and divides. Systems with tight orbits (planets, moons,
    stars around a black hole) use epsilon = 0, or an epsilon far below their closest approaches.
  * Timestep: the largest step that keeps the tightest orbit healthy (`timestep_max_s`, at least
    STEPS_PER_ORBIT steps per orbit and fine steps through every periapsis) caps the default
    timestep, which is otherwise picked so the scenario plays at a watchable speed. You can speed a
    scenario up at runtime (Right arrow) up to `timestep_max_s` without breaking its tight orbits.
  * Opening angle (as cell side / distance; the CPU tree gets the diagonal equivalent): 0.3 for
    self-gravitating systems and 0.6 where one dominant body sets the forces. The trees are
    monopole-only: the GPU tree's median force error is 0.45% at 0.3, 1.2% at 0.5 and 2.5% at 0.7
    (100k-body spiral galaxy), and these errors slowly puff up hot components. In galaxy-spiral
    the dark halo's half-mass radius grows by an extra ~13% * theta^2 over two rotations (1.2% at
    0.3, 3.4% at 0.5, 6.6% at 0.7), while a 3x smaller timestep changes nothing.
  * The simulator integrates with a fixed global timestep, so a scenario can only contain orbits
    whose periods are within a few orders of magnitude of each other (a moon around a planet inside
    a galaxy would need ~1e10 steps per galactic orbit). Scenarios are designed around that: the
    timestep is always capped by the tightest orbit, and the generator refuses a scenario whose
    softening would weaken an orbit that has to hold.

Physics notes:
  * Bodies live in a plane but gravity is 3D (F ~ 1/r^2), so disks are razor-thin 3D disks: the
    rotation curves of disk components are computed exactly (elliptic integrals over rings, with
    the simulator's Plummer softening) instead of using spherical M(<r)/r formulas.
  * Hot (pressure supported) components are sampled from the exact isotropic equilibrium of a
    planar system: f(E) = (1/2pi) dSigma/dPsi, which needs no integral inversion in 2D.
  * Cold disks get Toomre-Q based velocity dispersions and asymmetric-drift corrected rotation.
"""

import argparse
import fnmatch
import json
import math
import os
import subprocess
import sys
import tempfile
import time
import zlib
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from scipy.ndimage import map_coordinates
from scipy.special import ellipe, ellipkm1

REPO_ROOT = Path(__file__).resolve().parent.parent
SCENARIOS_DIR = Path("scenarios")  # relative to the repo root
SIMULATOR = REPO_ROOT / "build" / "n-body-2d"

# ------------------------------------------------------------------------------------------------
# Units & constants (SI)
# ------------------------------------------------------------------------------------------------
G = 6.67430e-11  # must match Constants::Simulation::G
AU = 1.495978707e11
KM = 1e3
DAY = 86400.0
YEAR = 365.25 * DAY
MYR = 1e6 * YEAR
PC = 3.0856775814913673e16
KPC = 1e3 * PC
R_SUN = 6.957e8
DEG = math.pi / 180


def mass_from_gm(gm_km3_s2):
    """Mass [kg] from a GM value [km^3/s^2]. GMs are known far better than G, so dividing by the
    simulator's G makes orbits come out exactly as intended."""
    return gm_km3_s2 * 1e9 / G


M_SUN = mass_from_gm(1.32712440041e11)
M_EARTH = mass_from_gm(398600.435507)
M_JUPITER = mass_from_gm(126686531.9)

# ------------------------------------------------------------------------------------------------
# Simulator model (keep in sync with src/common_lib/Constants and src/Simulation)
# ------------------------------------------------------------------------------------------------
ALL_PAIRS = "All Pairs"
BH_CPU = "Barnes-Hut"
BH_GPU = "Barnes-Hut GPU"

TIMESTEP_RANGE = (1e-9, 31_556_952_000_000_000)
SOFTENING_FACTOR_RANGE = (0.0, 0.2)
THREADS_RANGE = (1, 256)
PIXEL_SCALE_RANGE = (1e-12, 8.8e50)

ALL_PAIRS_MAX_BODIES = 300
CPU_THREADS = max(THREADS_RANGE[0], min(THREADS_RANGE[1], os.cpu_count() or 1))

STEPS_PER_ORBIT = 300  # minimum steps per orbit of the tightest bound pair (timestep_max_s)
PERIAPSIS_STEP_FRACTION = 0.03  # max step as a fraction of sqrt(r_p^3 / GM) of eccentric orbits

RESOLUTION = (1600, 900)
VIEW_FILL = 0.92  # fraction of the window the requested view occupies


def estimate_ips(algorithm, n, theta):
    """Iterations per second, fitted to measurements on a Ryzen 9 9950X3D + RTX 5070 Ti (in a
    headless session, so the window was drawn by another GPU; within ~15%).

    All Pairs:   single threaded, ~0.93 ns per pair (a bit more for small systems) plus ~50 ns
                 per iteration (19M it/s for 3 bodies, 0.77M for 44, 2.1k for 1000).
    Barnes-Hut:  ~0.2 us per body with 32 threads at theta(diagonal) = 1.0, scaling like
                 theta^-1.25, plus ~10 us per iteration.
    GPU:         ~45 us per iteration plus 15 ns per body of fixed costs, capped at ~100 us (from
                 ~4k bodies), plus 0.64 ns + 0.14 ns * theta(side)^-2 per body (the interactions per
                 body grow like theta^-2). Measured: 14k it/s for 1.5k bodies, 2.2k for 300k at
                 theta = 0.5, 760-880 for 1M at 0.6-0.7.
    `theta` is the side-length opening angle used throughout this script.
    """
    if algorithm == ALL_PAIRS:
        pairs = n * (n - 1) / 2
        return 1.0 / (50e-9 + 0.93e-9 * pairs * (1 + 900 / (1250 + pairs)))
    if algorithm == BH_CPU:
        return 1.0 / (10e-6 + 0.2e-6 * n * (theta * math.sqrt(2)) ** -1.25)
    return 1.0 / (min(100e-6, 45e-6 + 15e-9 * n) + n * (0.64e-9 + 0.14e-9 * theta ** -2))


def choose_algorithm(n):
    """All Pairs (exact) while it keeps up with the GPU tree (they break even at ~330 bodies), the
    GPU Barnes-Hut above. The CPU Barnes-Hut is never the fastest on the reference machine (1.6x
    slower than the GPU at 300 bodies, 14x at 6k), so only scenarios that ask for it use it."""
    return ALL_PAIRS if n <= ALL_PAIRS_MAX_BODIES else BH_GPU


def config_theta(algorithm, theta):
    """The CPU tree compares the cell *diagonal* to the distance, the GPU tree the cell *side*.
    Scenarios specify the side-length angle; convert so both trees are equally accurate."""
    return theta * math.sqrt(2) if algorithm == BH_CPU else theta


def mean_pairwise_distance(pos, rng=None):
    """What Simulation::compute_plummer_softening divides by: the exact mean over all pairs for
    n * n <= 1e6, otherwise an estimate from 1e6 random pairs (here: exact up to 20k bodies,
    4e6 sampled pairs above, so the simulator's own estimate lands within ~0.1%)."""
    n = len(pos)
    if n < 2:
        return 0.0
    if n <= 20_000:
        total = 0.0
        for i0 in range(0, n - 1, 256):
            block = pos[i0:i0 + 256]
            d = np.hypot(block[:, None, 0] - pos[None, :, 0], block[:, None, 1] - pos[None, :, 1])
            rows = np.arange(i0, i0 + len(block))[:, None]
            total += d[np.arange(n)[None, :] > rows].sum()
        return total / (n * (n - 1) / 2)
    rng = rng or np.random.default_rng(12345)
    i = rng.integers(0, n, 4_000_000)
    j = rng.integers(0, n - 1, 4_000_000)
    j += j >= i
    return float(np.hypot(*(pos[i] - pos[j]).T).mean())


# ------------------------------------------------------------------------------------------------
# Bodies
# ------------------------------------------------------------------------------------------------
@dataclass
class Bodies:
    ids: np.ndarray  # str
    mass: np.ndarray  # (n,)
    pos: np.ndarray  # (n, 2)
    vel: np.ndarray  # (n, 2)

    @staticmethod
    def single(name, mass, pos=(0.0, 0.0), vel=(0.0, 0.0)):
        return Bodies(np.array([name]), np.array([float(mass)]), np.array([pos], float),
                np.array([vel], float))

    @staticmethod
    def group(prefix, mass, pos, vel):
        n = len(pos)
        ids = np.char.add(prefix, np.arange(n).astype(str))
        return Bodies(ids, np.broadcast_to(np.asarray(mass, float), (n,)).copy(),
                np.asarray(pos, float), np.asarray(vel, float))

    @staticmethod
    def concat(*parts):
        parts = [p for p in parts if len(p)]
        return Bodies(np.concatenate([p.ids for p in parts]),
                np.concatenate([p.mass for p in parts]),
                np.concatenate([p.pos for p in parts]), np.concatenate([p.vel for p in parts]))

    def __len__(self):
        return len(self.mass)

    @property
    def total_mass(self):
        return float(self.mass.sum())

    def com(self):
        m = self.mass[:, None]
        return (m * self.pos).sum(0) / self.total_mass, (m * self.vel).sum(0) / self.total_mass

    def moved(self, dpos=(0.0, 0.0), dvel=(0.0, 0.0)):
        return Bodies(self.ids, self.mass, self.pos + np.asarray(dpos, float),
                self.vel + np.asarray(dvel, float))

    def in_com_frame(self):
        p, v = self.com()
        return self.moved(-p, -v)

    def rotated(self, angle):
        c, s = math.cos(angle), math.sin(angle)
        rot = np.array([[c, -s], [s, c]])
        return Bodies(self.ids, self.mass, self.pos @ rot.T, self.vel @ rot.T)

    def index(self, name):
        hits = np.flatnonzero(self.ids == name)
        if len(hits) != 1:
            raise KeyError(name)
        return int(hits[0])


# ------------------------------------------------------------------------------------------------
# Orbits
# ------------------------------------------------------------------------------------------------
def rotate(vec, angle):
    c, s = np.cos(angle), np.sin(angle)
    return np.stack([c * vec[..., 0] - s * vec[..., 1], s * vec[..., 0] + c * vec[..., 1]], -1)


def true_anomaly(mean_anomaly, e):
    """Solve Kepler's equation (vectorized) and return the true anomaly of an ellipse."""
    M = np.remainder(np.asarray(mean_anomaly, float) + np.pi, 2 * np.pi) - np.pi
    e = np.asarray(e, float)
    E = M + 0.85 * e * np.sign(M)  # Danby's starter: Newton converges for every e < 1
    for _ in range(60):
        dE = (E - e * np.sin(E) - M) / (1 - e * np.cos(E))
        E = E - dE
        if np.all(np.abs(dE) < 1e-15):
            break
    return 2 * np.arctan2(np.sqrt(1 + e) * np.sin(E / 2), np.sqrt(1 - e) * np.cos(E / 2))


def conic_state(mu, periapsis, e, nu, arg_periapsis=0.0, retrograde=False):
    """Relative position & velocity on any conic (e < 1, = 1 or > 1) at true anomaly `nu`."""
    p = np.asarray(periapsis * (1 + e), float)
    nu = np.asarray(nu, float)
    r = p / (1 + e * np.cos(nu))
    pos = np.stack([r * np.cos(nu), r * np.sin(nu)], -1)
    vel = np.sqrt(mu / p)[..., None] * np.stack([-np.sin(nu), e + np.cos(nu)], -1)
    if np.any(retrograde):
        flip = np.where(np.asarray(retrograde)[..., None], [1.0, -1.0], [1.0, 1.0])
        pos, vel = pos * flip, vel * flip
    return rotate(pos, arg_periapsis), rotate(vel, arg_periapsis)


def orbit_state(mu, a, e, arg_periapsis, mean_anomaly, retrograde=False):
    return conic_state(mu, np.asarray(a) * (1 - np.asarray(e)), e, true_anomaly(mean_anomaly, e),
            arg_periapsis, retrograde)


def orbital_period(mu, a):
    return 2 * math.pi * math.sqrt(a ** 3 / mu)


def periapsis_time_scale(mu, r_periapsis):
    return math.sqrt(r_periapsis ** 3 / mu)


def orbit_dt_limit(mu, a, e):
    """Largest timestep that keeps an orbit healthy under the simulator's symplectic Euler."""
    return min(orbital_period(mu, a) / STEPS_PER_ORBIT,
            PERIAPSIS_STEP_FRACTION * periapsis_time_scale(mu, a * (1 - e)))


def parabolic_true_anomaly(mu, periapsis, time_to_periapsis):
    """True anomaly (< 0, approaching) on a parabola `time_to_periapsis` before periapsis:
    Barker's equation D + D^3/3 = -2 t sqrt(mu / p^3), D = tan(nu / 2), solved with Cardano."""
    p = 2 * periapsis
    w = 3 * time_to_periapsis * math.sqrt(mu / p ** 3)
    s = math.sqrt(w * w + 1)
    return 2 * math.atan(float(np.cbrt(s - w) - np.cbrt(s + w)))


def place_parabolic(b1, b2, periapsis, time_to_periapsis, arg_periapsis=0.0):
    """Two systems (treated as point masses) on a counter-clockwise parabolic orbit that reaches
    periapsis after `time_to_periapsis`."""
    m1, m2 = b1.total_mass, b2.total_mass
    mu = G * (m1 + m2)
    r, v = conic_state(mu, periapsis, 1.0, parabolic_true_anomaly(mu, periapsis,
            time_to_periapsis), arg_periapsis)
    f = m2 / (m1 + m2)
    return Bodies.concat(b1.moved(-f * r, -f * v), b2.moved((1 - f) * r, (1 - f) * v))


@dataclass
class Orbiter:
    """A body (with its own satellites) on a Keplerian orbit; angles in radians. Orbits are
    Jacobi orbits: around the barycentre of the central body plus all *inner* orbiters."""
    name: str
    mass: float
    a: float
    e: float = 0.0
    arg_periapsis: float = 0.0
    mean_anomaly: float = 0.0
    retrograde: bool = False
    satellites: list = field(default_factory=list)


@dataclass
class BoundPair:
    """An orbit that must hold: the generator resolves it with the timestep and --verify checks
    that it still exists with a similar semi-major axis. Like the orbits in build_system() these
    are Jacobi orbits: the barycentre of `outer` (default: the secondary) around the barycentre
    of `inner` (default: the primary), e.g. Nix around Pluto + Charon."""
    primary: str
    secondary: str
    a: float
    e: float
    mu: float
    inner: tuple = ()
    outer: tuple = ()

    @property
    def dt_limit(self):
        return orbit_dt_limit(self.mu, self.a, self.e)


def build_system(name, mass, satellites):
    """Central body plus (recursively) orbiting subsystems, returned in the barycentric frame,
    together with the bound pairs (central, orbiter) of every level."""
    system = Bodies.single(name, mass)
    pairs = []
    for sat in sorted(satellites, key=lambda s: s.a):
        sub, sub_pairs = build_system(sat.name, sat.mass, sat.satellites)
        mu = G * (system.total_mass + sub.total_mass)
        r, v = orbit_state(mu, sat.a, sat.e, sat.arg_periapsis, sat.mean_anomaly, sat.retrograde)
        pairs += [BoundPair(name, sat.name, sat.a, sat.e, mu, tuple(system.ids.tolist()),
                tuple(sub.ids.tolist()))] + sub_pairs
        system = Bodies.concat(system, sub.moved(r, v)).in_com_frame()
    return system, pairs


# ------------------------------------------------------------------------------------------------
# Self-gravitating, axisymmetric planar systems
# ------------------------------------------------------------------------------------------------
def ring_field(R, ring_r, ring_m, eps):
    """In-plane potential [J/kg] and radial acceleration [m/s^2, + outward] at radii R of thin
    rings (radii ring_r, masses ring_m) under Plummer softening eps, i.e. exactly what the
    simulator computes for a smooth ring: Phi = -(2 G m / pi) K(m) / s, s^2 = (R + a)^2 + eps^2,
    m = 4 R a / s^2. Validated against direct quadrature (1e-8) and Freeman's exponential disk."""
    R = np.asarray(R, float)
    phi = np.zeros(len(R))
    g = np.zeros(len(R))
    a = np.asarray(ring_r, float)[None, :]
    mr = np.asarray(ring_m, float)[None, :]
    rows = max(1, 4_000_000 // max(1, a.shape[1]))
    for i0 in range(0, len(R), rows):
        r = R[i0:i0 + rows, None]
        s2 = (r + a) ** 2 + eps ** 2
        p = ((r - a) ** 2 + eps ** 2) / s2  # 1 - m, computed without cancellation
        m = 1.0 - p
        s = np.sqrt(s2)
        K = ellipkm1(p)
        E = ellipe(m)
        small = m < 1e-6
        dKdm = np.where(small, math.pi / 8 * (1 + 9 / 8 * m),
                (E - p * K) / (2 * np.where(small, 1.0, m) * p))
        dmdR = 4 * a / s2 * (1 - 2 * r * (r + a) / s2)
        phi[i0:i0 + rows] = (-(2 * G / math.pi) * mr * K / s).sum(1)
        g[i0:i0 + rows] = ((2 * G / math.pi) * mr * (dKdm * dmdR / s - K * (r + a) / s ** 3)).sum(1)
    return phi, g


class Profile:
    """Axisymmetric surface density truncated at r_max, defined by its cumulative mass."""
    r_max: float
    mass: float

    def cum_fraction(self, R):  # M(<R) / M(<r_max), R <= r_max
        raise NotImplementedError

    def surface_density(self, R):
        raise NotImplementedError

    def scale(self):  # characteristic radius (sampling grids)
        raise NotImplementedError

    def sample_radii(self, rng, n):
        grid = self._grid()
        cum = self.cum_fraction(grid)
        return np.interp(rng.random(n), cum / cum[-1], grid)

    def _grid(self):
        lo = min(self.scale(), self.r_max) * 1e-4
        return np.concatenate([[0.0], np.geomspace(lo, self.r_max, 20000)])

    def radius_at_density(self, sigma):
        """Inverse of the (monotonically decreasing) surface density, clamped to [0, r_max]."""
        grid = self._grid()[1:]
        log_s = np.log(np.maximum(self.surface_density(grid), 1e-300))
        with np.errstate(divide="ignore"):
            return np.interp(-np.log(sigma), -log_s, grid)


class ExponentialDisk(Profile):
    def __init__(self, mass, scale_length, r_max):
        self.mass, self.rd, self.r_max = mass, scale_length, r_max
        self._norm = self._cum(r_max)

    def _cum(self, R):
        x = np.asarray(R) / self.rd
        return 1 - np.exp(-x) * (1 + x)

    def cum_fraction(self, R):
        return np.clip(self._cum(R) / self._norm, 0, 1)

    def surface_density(self, R):
        R = np.asarray(R, float)
        sig = self.mass / (2 * math.pi * self.rd ** 2 * self._norm) * np.exp(-R / self.rd)
        return np.where(R <= self.r_max, sig, 0.0)

    def scale(self):
        return self.rd


class PlummerDisk(Profile):
    """Surface density of a projected Plummer sphere: Sigma = M a^2 / (pi (a^2 + R^2)^2)."""

    def __init__(self, mass, a, r_max):
        self.mass, self.a, self.r_max = mass, a, r_max
        self._norm = r_max ** 2 / (r_max ** 2 + a ** 2)

    def cum_fraction(self, R):
        R = np.minimum(np.asarray(R, float), self.r_max)
        return R ** 2 / (R ** 2 + self.a ** 2) / self._norm

    def surface_density(self, R):
        R = np.asarray(R, float)
        sig = self.mass / self._norm * self.a ** 2 / (math.pi * (self.a ** 2 + R ** 2) ** 2)
        return np.where(R <= self.r_max, sig, 0.0)

    def scale(self):
        return self.a


class MestelDisk(Profile):
    """Cored Mestel disk, Sigma ~ 1/sqrt(R^2 + rc^2): a thin disk with a flat rotation curve.
    Stands in for a dark halo, which in a planar simulation must be a hot planar component."""

    def __init__(self, mass, core, r_max):
        self.mass, self.rc, self.r_max = mass, core, r_max
        self._norm = math.hypot(r_max, core) - core

    def cum_fraction(self, R):
        R = np.minimum(np.asarray(R, float), self.r_max)
        return (np.hypot(R, self.rc) - self.rc) / self._norm

    def surface_density(self, R):
        R = np.asarray(R, float)
        sig = self.mass / (2 * math.pi * self._norm) / np.hypot(R, self.rc)
        return np.where(R <= self.r_max, sig, 0.0)

    def scale(self):
        return self.rc


class PowerLawDisk(Profile):
    """Sigma ~ R^-gamma between r_in and r_max (gamma < 2)."""

    def __init__(self, mass, gamma, r_in, r_max):
        self.mass, self.gamma, self.r_in, self.r_max = mass, gamma, r_in, r_max
        k = 2 - gamma
        self._norm = r_max ** k - r_in ** k

    def cum_fraction(self, R):
        R = np.clip(np.asarray(R, float), self.r_in, self.r_max)
        return (R ** (2 - self.gamma) - self.r_in ** (2 - self.gamma)) / self._norm

    def surface_density(self, R):
        R = np.asarray(R, float)
        sig = self.mass * (2 - self.gamma) / (2 * math.pi * self._norm) * np.maximum(
                R, self.r_in * 1e-3) ** -self.gamma
        return np.where((R >= self.r_in) & (R <= self.r_max), sig, 0.0)

    def scale(self):
        return self.r_in


class Tapered(Profile):
    """`base` with its outer edge smoothed: Sigma falls to 0 between r_taper and base.r_max with a
    cosine taper (a hard truncation of a hot component looks like a sharp-edged disc)."""

    def __init__(self, base, r_taper):
        self.base, self.r_max, self.r_taper = base, base.r_max, r_taper
        grid = np.concatenate([[0.0], np.geomspace(min(base.scale(), base.r_max) * 1e-4,
                base.r_max, 20000)])
        sig = self.surface_density(grid, normalized=False)
        cum = np.concatenate([[0.0], np.cumsum(np.diff(grid) * np.pi * (grid[1:] * sig[1:]
                + grid[:-1] * sig[:-1]))])
        self.mass = base.mass
        self._norm = base.mass / cum[-1]
        self._tab_r, self._tab_cum = grid, cum / cum[-1]

    def taper(self, R):
        x = np.clip((np.asarray(R, float) - self.r_taper) / (self.r_max - self.r_taper), 0, 1)
        return 0.5 * (1 + np.cos(math.pi * x))

    def surface_density(self, R, normalized=True):
        sig = self.base.surface_density(R) * self.taper(R)
        return sig * self._norm if normalized else sig

    def cum_fraction(self, R):
        return np.interp(R, self._tab_r, self._tab_cum)

    def scale(self):
        return self.base.scale()


class RadialField:
    """Potential and radial acceleration of axisymmetric components plus a central point mass,
    tabulated on a fine grid (with the simulator's Plummer softening)."""

    def __init__(self, profiles, eps, point_mass=0.0):
        if eps <= 0:
            raise ValueError("a self-gravitating system needs a softening length")
        r_max = max(p.r_max for p in profiles)
        r_out = 4 * r_max
        smallest = min(min(p.scale(), p.r_max) for p in profiles)
        # Rings must be spaced well below eps or the ring lattice shows up in the forces.
        dr = min(eps / 3, smallest / 20)
        edges = np.linspace(0, r_max, int(math.ceil(r_max / dr)) + 1)
        mid = 0.5 * (edges[1:] + edges[:-1])
        masses = sum(p.mass * np.diff(p.cum_fraction(edges)) for p in profiles)
        self.R = np.concatenate([np.linspace(0, r_max * 1.02, 6000)[:-1],
                np.geomspace(r_max * 1.02, r_out, 400)])
        self.phi, self.g = ring_field(self.R, mid, masses, eps)
        if point_mass:
            soft = self.R ** 2 + eps ** 2
            self.phi -= G * point_mass / np.sqrt(soft)
            self.g -= G * point_mass * self.R / soft ** 1.5
        self.total_mass = sum(p.mass for p in profiles) + point_mass
        self._r_out = r_out

    def psi(self, R):
        """Relative potential -Phi (> 0 for bound orbits), point-mass tail beyond the grid."""
        R = np.asarray(R, float)
        tail = G * self.total_mass / np.maximum(R, 1e-300)
        return np.where(R < self._r_out, np.interp(R, self.R, -self.phi), tail)

    def g_in(self, R):
        """Inward radial acceleration."""
        R = np.asarray(R, float)
        tail = G * self.total_mass / np.maximum(R, 1e-300) ** 2
        return np.where(R < self._r_out, np.interp(R, self.R, -self.g), tail)

    def v_circ(self, R):
        return np.sqrt(np.maximum(R * self.g_in(R), 0.0))

    def omega_kappa(self, R):
        """Angular and epicyclic frequencies (kappa^2 = R dOmega^2/dR + 4 Omega^2)."""
        Rg = self.R[1:]
        om2 = np.maximum(-self.g[1:] / Rg, 0.0)
        k2 = Rg * np.gradient(om2, Rg) + 4 * om2
        R = np.asarray(R, float)
        om2_r = np.interp(R, Rg, om2)
        k2_r = np.interp(R, Rg, np.maximum(k2, 1e-3 * om2))
        return np.sqrt(om2_r), np.sqrt(k2_r)

    def min_orbital_period(self, r_from=0.0):
        """Shortest circular orbit period at radii >= r_from (inner cores set the timestep)."""
        R = self.R[(self.R > max(r_from, 1e-300))]
        return float(np.min(2 * math.pi * R / np.maximum(self.v_circ(R), 1e-300)))

    def max_acceleration(self):
        return float(np.max(-self.g))


def random_directions(rng, n):
    ang = rng.uniform(0, 2 * math.pi, n)
    return np.stack([np.cos(ang), np.sin(ang)], -1)


def sample_hot(rng, n, profile, fld):
    """Positions & velocities of a non-rotating, isotropic component with surface density
    `profile`, in equilibrium in the total field `fld`. In a planar system f(E) =
    (1/2pi) dSigma/dPsi exactly, so at radius R a speed follows from a random 'turning radius'
    R' >= R with Sigma(R') = u Sigma(R), u ~ U(0,1): v^2 = 2 (Psi(R) - Psi(R'))."""
    R = profile.sample_radii(rng, n)
    target = rng.random(n) * profile.surface_density(R)
    r_turn = np.maximum(profile.radius_at_density(np.maximum(target, 1e-300)), R)
    v = np.sqrt(np.maximum(2 * (fld.psi(R) - fld.psi(r_turn)), 0.0))
    pos = R[:, None] * random_directions(rng, n)
    return pos, v[:, None] * random_directions(rng, n)


def sample_cold_disk(rng, n, profile, fld, Q, sigma_cap=0.5):
    """Positions & velocities of a rotating disk with Toomre parameter Q (stellar: sigma_R =
    3.36 G Sigma Q / kappa, at most sigma_cap times the circular speed), epicyclic sigma_phi,
    and asymmetric-drift corrected rotation (radial Jeans equation)."""
    R = profile.sample_radii(rng, n)
    Rg = np.geomspace(max(profile.r_max * 1e-4, 1e-300), profile.r_max, 4000)
    vc = fld.v_circ(Rg)
    om, kap = fld.omega_kappa(Rg)
    sig_s = profile.surface_density(Rg)
    sig_r = np.minimum(3.36 * G * sig_s * Q / np.maximum(kap, 1e-300), sigma_cap * vc)
    sig_p = sig_r * np.clip(kap / (2 * np.maximum(om, 1e-300)), 0.5, 1.0)
    dln = np.gradient(np.log(np.maximum(sig_s * sig_r ** 2, 1e-300)), np.log(Rg))
    vphi2 = vc ** 2 + sig_r ** 2 * (1 - (sig_p / np.maximum(sig_r, 1e-300)) ** 2 + dln)
    vphi = np.sqrt(np.maximum(vphi2, 0.0))
    at = lambda arr: np.interp(R, Rg, arr)
    v_r = rng.normal(0, 1, n) * at(sig_r)
    v_p = at(vphi) + rng.normal(0, 1, n) * at(sig_p)
    # never launch a particle above 95% of the local escape speed
    v_esc = np.sqrt(2 * fld.psi(R))
    speed = np.hypot(v_r, v_p)
    shrink = np.minimum(1.0, 0.95 * v_esc / np.maximum(speed, 1e-300))
    v_r, v_p = v_r * shrink, v_p * shrink
    th = rng.uniform(0, 2 * math.pi, n)
    c, s = np.cos(th), np.sin(th)
    pos = np.stack([R * c, R * s], -1)
    vel = np.stack([v_r * c - v_p * s, v_r * s + v_p * c], -1)
    return pos, vel


def circular_disk(rng, n, profile, v_of_r):
    """Test-particle disk on circular orbits with speed v_of_r(R)."""
    R = profile.sample_radii(rng, n)
    th = rng.uniform(0, 2 * math.pi, n)
    v = v_of_r(R)
    pos = np.stack([R * np.cos(th), R * np.sin(th)], -1)
    vel = np.stack([-v * np.sin(th), v * np.cos(th)], -1)
    return pos, vel


def particle_counts(n_total, masses, weights=None):
    """Split n_total particles over components so particle masses are equal (or scaled by
    `weights`, e.g. 2 = particles twice as heavy as the rest)."""
    weights = weights or [1.0] * len(masses)
    share = np.array([m / w for m, w in zip(masses, weights)])
    counts = np.floor(n_total * share / share.sum()).astype(int)
    counts[np.argmax(share)] += n_total - counts.sum()
    return [int(c) for c in counts]


@dataclass
class GalaxyModel:
    disk: Profile | None = None
    bulge: Profile | None = None
    halo: Profile | None = None
    Q: float = 1.5
    halo_particle_weight: float = 1.0  # halo particles this many times heavier than the rest


def make_galaxy(rng, model, n, eps, prefix=""):
    """Equilibrium galaxy (disk + bulge + halo, any of them optional) in its COM frame."""
    comps = [(c, w) for c, w in ((model.bulge, 1.0), (model.disk, 1.0),
            (model.halo, model.halo_particle_weight)) if c is not None]
    fld = RadialField([c for c, _ in comps], eps)
    counts = particle_counts(n, [c.mass for c, _ in comps], [w for _, w in comps])
    parts = []
    for (comp, _), count in zip(comps, counts):
        if comp is model.disk:
            pos, vel = sample_cold_disk(rng, count, comp, fld, model.Q)
            tag = "d"
        else:
            pos, vel = sample_hot(rng, count, comp, fld)
            tag = "b" if comp is model.bulge else "h"
        parts.append(Bodies.group(prefix + tag, comp.mass / count, pos, vel))
    return Bodies.concat(*parts).in_com_frame(), fld


def pair_rows(pos, mass, eps, targets, virial=False):
    """For every target i: sum_j m_i m_j / sqrt(r_ij^2 + eps^2), or with `virial` the virial
    term sum_j m_i m_j r_ij^2 / (r_ij^2 + eps^2)^1.5 (self excluded)."""
    out = np.zeros(len(targets))
    rows = max(1, 10_000_000 // len(pos))
    for i0 in range(0, len(targets), rows):
        t = targets[i0:i0 + rows]
        r2 = ((pos[t, None, :] - pos[None, :, :]) ** 2).sum(-1)
        s2 = r2 + eps ** 2
        s2[np.arange(len(t)), t] = np.inf
        term = r2 / s2 ** 1.5 if virial else 1 / np.sqrt(s2)
        out[i0:i0 + rows] = mass[t] * (mass[None, :] * term).sum(1)
    return out


def pair_sum(b, eps, virial=False, max_targets=4000, rng=None):
    """-G/2 sum_i pair_rows(i): the potential energy W, or with `virial` sum_i r_i . F_i. Exact
    for small systems; for big ones the rows of the heaviest bodies are summed exactly and the
    rest estimated from a random sample of rows."""
    n = len(b)
    if n <= max_targets:
        return -0.5 * G * pair_rows(b.pos, b.mass, eps, np.arange(n), virial).sum()
    order = np.argsort(b.mass)[::-1]
    heavy, rest = order[:64], order[64:]
    sample = (rng or np.random.default_rng(1)).choice(rest, min(max_targets, len(rest)),
            replace=False)
    total = pair_rows(b.pos, b.mass, eps, heavy, virial).sum()
    total += pair_rows(b.pos, b.mass, eps, sample, virial).sum() * len(rest) / len(sample)
    return -0.5 * G * total


def virial_ratio(b, eps):
    """2T / |sum r.F|, which is 1 in equilibrium. (With softening this is not 2T/|W|: the
    softened force is weaker than the gradient of 1/r, so equilibria have 2T < |W|.)"""
    vcom = b.com()[1]
    T = 0.5 * float((b.mass * ((b.vel - vcom) ** 2).sum(1)).sum())
    return 2 * T / abs(pair_sum(b, eps, virial=True))


# ------------------------------------------------------------------------------------------------
# Scenario definition & registry
# ------------------------------------------------------------------------------------------------
@dataclass
class Scenario:
    bodies: Bodies
    description: str
    view: tuple  # (half width, half height) [m] that the initial view must show
    show_period: float  # [s] the motion the default speed is tuned for...
    show_seconds: float  # ...should take this many wall seconds
    softening: float = 0.0  # Plummer epsilon [m]
    dt_max: float = math.inf  # accuracy limit [s] besides the bound pairs
    pairs: list = field(default_factory=list)  # BoundPairs (tight orbits that must hold)
    trails: float = 0.0  # trail fade time [s]
    algorithm: str | None = None  # None: by body count
    theta: float = 0.3  # Barnes-Hut opening angle (cell side / distance), see the module docstring
    tips: list = field(default_factory=list)
    check_time: float = 0.0  # simulated time --verify runs (default: 3 show periods)
    self_gravitating: bool = False  # report the virial ratio
    softening_tolerance: float = 0.05  # max softening / periapsis of the bound pairs
    check: object = None  # extra --verify check: (initial, final) -> [(passed, message), ...]
    virial_prefix: str = ""  # ids of the subsystem whose virial ratio is reported


SCENARIOS = {}


def scenario(name, group, summary):
    def register(fn):
        SCENARIOS[name] = (group, summary, fn)
        return fn

    return register


def scenario_rng(name, seed):
    return np.random.default_rng([seed, zlib.crc32(name.encode())])


def scaled(n, scale, minimum=1):
    return max(minimum, int(round(n * scale)))


def view_circle(radius):
    return (radius, radius)


def direct_accelerations(pos, mass, eps):
    """Plummer-softened accelerations by direct summation (what All Pairs computes)."""
    acc = np.zeros((len(pos), 2))
    rows = max(1, 4_000_000 // len(pos))
    for i0 in range(0, len(pos), rows):
        t = np.arange(i0, min(i0 + rows, len(pos)))
        d = pos[None, :, :] - pos[t, None, :]
        r2 = (d ** 2).sum(-1) + eps ** 2
        r2[np.arange(len(t)), t] = np.inf
        acc[i0:i0 + rows] = G * (d * (mass[None, :] / r2 ** 1.5)[..., None]).sum(1)
    return acc


# ------------------------------------------------------------------------------------------------
# Scenarios: three-body orbits and planetary systems (mostly All Pairs, no softening)
# ------------------------------------------------------------------------------------------------
def normalized_three_body(pos, vel, period, r_min, half_size, description, show_seconds, tips,
        periods_checked=5):
    """Three equal suns from a G = m = 1 periodic solution, scaled to 1 solar mass and 1 AU
    (the time unit is then 58 days). --verify checks they are back where they started after
    `periods_checked` periods."""
    t_unit = math.sqrt(AU ** 3 / (G * M_SUN))
    bodies = Bodies.group("star-", M_SUN, np.asarray(pos, float) * AU,
            np.asarray(vel, float) * AU / t_unit).in_com_frame()
    T = period * t_unit

    def periodic_check(init, final):
        dev = float(np.abs(final.pos - init.pos).max()) / AU
        return [(dev < 0.05, f"back on the periodic orbit after {periods_checked} periods "
                f"(largest deviation {dev:.1e} AU)")]

    return Scenario(bodies, description, view=(half_size[0] * AU, half_size[1] * AU),
            show_period=T, show_seconds=show_seconds,
            dt_max=PERIAPSIS_STEP_FRACTION * periapsis_time_scale(2 * G * M_SUN, r_min * AU),
            trails=show_seconds, tips=list(tips), check_time=periods_checked * T,
            check=periodic_check)


@scenario("three-body-figure-eight", "three-body",
        "Three suns chasing each other on a figure-eight")
def three_body_figure_eight(rng, scale):
    x1 = np.array([0.97000436, -0.24308753])  # Chenciner & Montgomery (2000), linearly stable
    v3 = np.array([-0.93240737, -0.86473146])
    return normalized_three_body([x1, -x1, [0, 0]], [-v3 / 2, -v3 / 2, v3], 6.32591398, 0.69,
            (1.15, 0.45), "The Chenciner-Montgomery figure-eight: three equal suns share one "
            "orbit. It is linearly stable, so it keeps going indefinitely.", 8,
            ["The trails draw the orbit; Ctrl+Right makes them longer."])


# Periodic orbits of Suvakov & Dmitrasinovic (2013): x1 = (-1, 0), x2 = (1, 0), x3 = (0, 0),
# v1 = v2 = (p1, p2), v3 = -2 (p1, p2), G = m = 1. The published 6-digit values were refined by
# shooting (least squares on the return error) until each orbit closes to < 1e-10.
# name: (p1, p2, period, closest approach, half width, half height, wall seconds per period)
SUVAKOV_ORBITS = {
    "butterfly": (0.306893420461733, 0.125506567015994, 6.234674838707566, 0.01104, 1.042,
            0.175, 8),
    "moth": (0.464445172818034, 0.396060014652821, 14.894305175038079, 0.07947, 1.115, 1.137,
            12),
    "yin-yang": (0.513938537460686, 0.304735919346756, 17.328834018610277, 0.01328, 1.275,
            1.064, 14),
    "dragonfly": (0.080584225542783, 0.588836089778272, 21.272337394551737, 0.00734, 1.032,
            0.897, 15),
    "bumblebee": (0.184278499156632, 0.587188172192724, 63.534353340392002, 0.01191, 1.048,
            1.159, 30),
}


def suvakov_scenario(name):
    p1, p2, T, r_min, hx, hy, seconds = SUVAKOV_ORBITS[name]

    def make(rng, scale):
        return normalized_three_body([[-1, 0], [1, 0], [0, 0]], [[p1, p2], [p1, p2], [-2 * p1,
                -2 * p2]], T, r_min, (1.1 * hx, max(0.4, 1.1 * hy)), f"The '{name}' periodic "
                "three-body orbit found by Suvakov & Dmitrasinovic (2013): three equal suns "
                "trace one repeating pattern. Like most such orbits it is unstable, so after "
                "some periods the pattern drifts and the system falls into chaos.", seconds,
                ["The trails draw the orbit; Ctrl+Right makes them longer.",
                "Slower timesteps (Left arrow) keep it on the periodic orbit for longer."])

    return make


for _name in SUVAKOV_ORBITS:
    scenario(f"three-body-{_name}", "three-body",
            f"Three suns on the unstable '{_name}' periodic orbit")(suvakov_scenario(_name))


@scenario("maxwell-ring-stable", "systems",
        "36 moonlets on one circular orbit, light enough to be stable (Maxwell 1859)")
def maxwell_ring_stable(rng, scale):
    return maxwell_ring(rng, unstable=False)


@scenario("maxwell-ring-unstable", "systems",
        "The same ring with heavier moonlets: Maxwell's instability breaks it into clumps")
def maxwell_ring_unstable(rng, scale):
    return maxwell_ring(rng, unstable=True)


def maxwell_ring(rng, unstable):
    """Maxwell showed (Adams Prize, 1859) that a ring of n equal satellites around a planet is
    stable only while m/M < 2.298/n^3, which is why Saturn's rings must be made of many small
    particles. The ring is set up in exact (softened) equilibrium and nudged by 1e-6 of r."""
    planet = mass_from_gm(PLANET_GM["Saturn"])
    n, r = 36, 1.0e8
    m = 2.298 * planet / n ** 3 * (12.0 if unstable else 0.25)
    eps = 1e-3 * r
    ang = 2 * math.pi * np.arange(n) / n
    pos = np.concatenate([[[0.0, 0.0]], r * np.stack([np.cos(ang), np.sin(ang)], -1)])
    mass = np.concatenate([[planet], np.full(n, m)])
    a_in = -(direct_accelerations(pos, mass, eps)[1:] * pos[1:]).sum(1) / r
    v = np.sqrt(r * a_in)
    vel = np.concatenate([[[0.0, 0.0]], v[:, None] * np.stack([-np.sin(ang), np.cos(ang)], -1)])
    pos[1:] *= 1 + 1e-6 * rng.standard_normal(n)[:, None]
    bodies = Bodies.concat(Bodies.single("Saturn", planet),
            Bodies.group("moonlet-", m, pos[1:], vel[1:])).in_com_frame()
    P = 2 * math.pi * r / v.mean()
    ratio = m / (2.298 * planet / n ** 3)
    pairs = [] if unstable else [BoundPair("Saturn", f"moonlet-{k}", r, 0.0, G * (planet + m))
            for k in range(0, n, 9)]
    what = ("They break up after a few orbits, bunching into clumps that scatter each other."
            if unstable else "They stay a ring indefinitely.")

    def ring_check(init, final):
        rel = final.pos[1:] - final.pos[final.index("Saturn")]
        spread = float(np.std(np.hypot(*rel.T)) / r)
        if unstable:
            return [(spread > 0.02, f"the ring broke up (radial spread {spread:.1%} of r)")]
        return [(spread < 1e-3, f"the ring stayed circular (radial spread {spread:.1e} of r)")]
    return Scenario(bodies, f"{n} moonlets of {ratio:g}x Maxwell's critical mass "
            f"(2.298 M/n^3) share a circular orbit around Saturn. {what}", view_circle(1.4 * r),
            show_period=P, show_seconds=6,
            dt_max=PERIAPSIS_STEP_FRACTION * periapsis_time_scale(2 * G * m, eps),
            softening=eps, pairs=pairs, trails=1.5, check_time=(30 if unstable else 10) * P,
            check=ring_check,
            tips=["The instability needs a few orbits to grow." if unstable else
                    "Compare with maxwell-ring-unstable."])


@scenario("trappist-1", "systems",
        "Seven Earth-sized planets in a resonant chain around a red dwarf")
def trappist_1(rng, scale):
    star = 0.0898 * M_SUN
    # name, period [d], mass [M_earth], e   (Agol et al. 2021; e from Grimm et al. 2018)
    planets = [("b", 1.510826, 1.374, 0.006), ("c", 2.421937, 1.308, 0.007),
            ("d", 4.049219, 0.388, 0.008), ("e", 6.101013, 0.692, 0.005),
            ("f", 9.207540, 1.039, 0.010), ("g", 12.352446, 1.321, 0.002),
            ("h", 18.772866, 0.326, 0.006)]
    # Mean longitudes satisfying the measured three-body Laplace angles (Luger et al. 2017)
    lam = trappist_longitudes(rng)
    sats = []
    for (p, period, m_e, e), l in zip(planets, lam):
        m = m_e * M_EARTH
        a = (G * (star + m) * (period * DAY) ** 2 / (4 * math.pi ** 2)) ** (1 / 3)
        varpi = rng.uniform(0, 2 * math.pi)
        sats.append(Orbiter(f"TRAPPIST-1{p}", m, a, e, varpi, l - varpi))
    bodies, pairs = build_system("TRAPPIST-1", star, sats)
    a_h = sats[-1].a
    return Scenario(bodies, "TRAPPIST-1: seven roughly Earth-mass planets packed inside 0.07 AU, "
            "locked in a chain of near-resonances (8:5, 5:3, 3:2, 3:2, 4:3, 3:2).",
            view_circle(1.15 * a_h), show_period=planets[-1][1] * DAY, show_seconds=12,
            pairs=pairs, trails=12, check_time=2000 * planets[-1][1] * DAY,
            tips=["The whole system would fit well inside Mercury's orbit."])


def trappist_longitudes(rng):
    """Mean longitudes that satisfy the three-body Laplace angles phi_i = p L_i - (p + q) L_i+1
    + q L_i+2 measured by Luger et al. (2017): 176, 41, 174, 319, 177 degrees."""
    lam = [rng.uniform(0, 2 * math.pi), rng.uniform(0, 2 * math.pi)]
    for (p, q), phi in zip([(2, 3), (1, 2), (2, 3), (1, 2), (1, 2)], [176, 41, 174, 319, 177]):
        # phi = p*L0 - (p+q)*L1 + q*L2  ->  L2 = (phi - p*L0 + (p+q)*L1) / q
        lam.append((phi * DEG - p * lam[-2] + (p + q) * lam[-1]) / q)
    return lam


# J2000 Keplerian elements (Standish, JPL "Approximate Positions of the Planets"):
# a [AU], e, mean longitude L [deg], longitude of perihelion varpi [deg]; projected on the ecliptic
PLANET_ELEMENTS = {
    "Mercury": (0.38709927, 0.20563593, 252.25032350, 77.45779628),
    "Venus": (0.72333566, 0.00677672, 181.97909950, 131.60246718),
    "Earth": (1.00000261, 0.01671123, 100.46457166, 102.93768193),
    "Mars": (1.52371034, 0.09339410, -4.55343205, -23.94362959),
    "Jupiter": (5.20288700, 0.04838624, 34.39644051, 14.72847983),
    "Saturn": (9.53667594, 0.05386179, 49.95424423, 92.59887831),
    "Uranus": (19.18916464, 0.04725744, 313.23810451, 170.95427630),
    "Neptune": (30.06992276, 0.00859048, -55.12002969, 44.96476227),
    "Pluto": (39.48211675, 0.24882730, 238.92903833, 224.06891629),
}
# GM [km^3/s^2] of the bodies alone (JPL DE440 / satellite ephemerides)
PLANET_GM = {"Mercury": 22031.868551, "Venus": 324858.592, "Earth": 398600.435507,
        "Mars": 42828.375816, "Jupiter": 126686531.9, "Saturn": 37931206.234,
        "Uranus": 5793951.256, "Neptune": 6835099.97, "Pluto": 869.326}
# Moons: name, a [km], e, GM [km^3/s^2], retrograde
MOONS = {
    "Earth": [("Moon", 384399, 0.0549, 4902.800118, False)],
    "Mars": [("Phobos", 9376, 0.0151, 7.087e-4, False), ("Deimos", 23463.2, 0.00033, 9.615e-5,
            False)],
    "Jupiter": [("Io", 421700, 0.0041, 5959.916, False), ("Europa", 671034, 0.0094, 3202.739,
            False), ("Ganymede", 1070412, 0.0013, 9887.834, False),
            ("Callisto", 1882709, 0.0074, 7179.289, False)],
    "Saturn": [("Mimas", 185539, 0.0196, 2.5026, False), ("Enceladus", 237948, 0.0047, 7.2027,
            False), ("Tethys", 294619, 0.0001, 41.2067, False), ("Dione", 377396, 0.0022,
            73.1146, False), ("Rhea", 527108, 0.0013, 153.9426, False), ("Titan", 1221870,
            0.0288, 8978.1382, False), ("Hyperion", 1481010, 0.1230, 0.3727, False),
            ("Iapetus", 3560820, 0.0286, 120.5038, False)],
    "Uranus": [("Miranda", 129390, 0.0013, 4.3, False), ("Ariel", 190900, 0.0012, 83.5, False),
            ("Umbriel", 266000, 0.0039, 85.1, False), ("Titania", 435910, 0.0011, 226.9, False),
            ("Oberon", 583520, 0.0014, 205.3, False)],
    "Neptune": [("Proteus", 117647, 0.0005, 2.9, False), ("Triton", 354759, 0.000016, 1428.5,
            True), ("Nereid", 5513818, 0.7507, 2.06, False)],
    "Pluto": [("Charon", 19591, 0.0002, 106.1, False), ("Nix", 48694, 0.002, 0.003, False),
            ("Hydra", 64738, 0.0059, 0.003, False)],
}
# Smaller bodies: name, a [AU], e, varpi [deg], mean anomaly [deg], GM [km^3/s^2], retrograde
MINOR_BODIES = [
    ("Ceres", 2.7675, 0.0758, 153.9, 95.99, 62.6284, False),
    ("Vesta", 2.3615, 0.0887, 254.2, 205.5, 17.288, False),
    ("Pallas", 2.7720, 0.2302, 283.8, 78.2, 13.665, False),
    ("Hygiea", 3.1415, 0.1125, 312.3, 152.2, 5.78, False),
    ("Eris", 67.86, 0.4361, 187.0, 205.99, 1108.0, False),
    ("Haumea", 43.13, 0.1950, 361.4, 218.2, 267.0, False),
    ("Makemake", 45.43, 0.1559, 377.5, 165.5, 207.0, False),
    ("Halley", 17.834, 0.96714, 302.9, 66.4, 1.5e-5, True),  # perihelion 1986.1, P = 75.3 yr
]


def solar_system_orbiters(rng, moons=True, minor=True):
    """Planets (with their moons, or as single barycentre bodies) and minor bodies at J2000."""
    sats = []
    for name, (a, e, L, varpi) in PLANET_ELEMENTS.items():
        moon_list = MOONS.get(name, [])
        if moons:
            subs = [Orbiter(m, mass_from_gm(gm), am * KM, em, rng.uniform(0, 2 * math.pi),
                    rng.uniform(0, 2 * math.pi), retro) for m, am, em, gm, retro in moon_list]
            if name == "Jupiter":  # Laplace resonance: L_Io - 3 L_Europa + 2 L_Ganymede = 180
                io, eu, ga = subs[0], subs[1], subs[2]
                lam_io = 3 * (eu.arg_periapsis + eu.mean_anomaly) - 2 * (
                        ga.arg_periapsis + ga.mean_anomaly) + math.pi
                io.mean_anomaly = lam_io - io.arg_periapsis
            mass = mass_from_gm(PLANET_GM[name])
        else:
            subs = []
            mass = mass_from_gm(PLANET_GM[name] + sum(gm for _, _, _, gm, _ in moon_list))
        sats.append(Orbiter(name, mass, a * AU, e, varpi * DEG, (L - varpi) * DEG,
                satellites=subs))
    if minor:
        for name, a, e, varpi, M, gm, retro in MINOR_BODIES:
            sats.append(Orbiter(name, mass_from_gm(gm), a * AU, e, varpi * DEG, M * DEG, retro))
    return sats


@scenario("solar-system", "systems",
        "Sun, planets with 26 major moons, dwarf planets and Halley's comet at J2000")
def solar_system(rng, scale):
    bodies, pairs = build_system("Sun", M_SUN, solar_system_orbiters(rng))
    a_jup = PLANET_ELEMENTS["Jupiter"][0] * AU
    return Scenario(bodies, "The Solar System at J2000 (orbits projected onto the ecliptic): "
            "Sun, eight planets with their major moons (Moon, Phobos, Deimos, the Galilean moons "
            "in their Laplace resonance, Saturn's eight major moons, Uranus' five, Proteus, "
            "retrograde Triton and Nereid), Pluto-Charon with Nix and Hydra, Ceres, Vesta, "
            "Pallas, Hygiea, Eris, Haumea, Makemake and retrograde Halley's comet. No softening, "
            "and a timestep that resolves Phobos' 7.7 hour orbit.",
            view_circle(1.12 * a_jup), show_period=orbital_period(G * M_SUN, a_jup),
            show_seconds=30, pairs=pairs, trails=3, check_time=2 * orbital_period(
            G * M_SUN, a_jup), tips=["Right-drag around a planet to select it with its moons, "
            "press F to follow it and C to centre it, then zoom in with the mouse wheel.",
            "Slow time down (Left arrow) to watch the moons; the speed-up limit "
            "(timestep_max_s) is set by Phobos."])


@scenario("sgr-a-star", "systems",
        "The S-stars whipping around the Milky Way's central black hole")
def sgr_a_star(rng, scale):
    m_bh = 4.297e6 * M_SUN  # GRAVITY Collaboration (2022)
    arcsec = 8277 * AU  # 1" at R0 = 8.277 kpc
    # a ["], e (Gillessen et al. 2017; S2 orientation from GRAVITY 2018). Orientations and phases
    # of the others are random: in 2D only a, e and the period matter for the picture.
    stars = [("S2", 0.1255, 0.8839), ("S1", 0.595, 0.556), ("S4", 0.3570, 0.3905),
            ("S6", 0.6574, 0.840), ("S8", 0.4047, 0.8031), ("S9", 0.2724, 0.644),
            ("S12", 0.2987, 0.8883), ("S13", 0.2641, 0.4250), ("S14", 0.2863, 0.9761),
            ("S17", 0.3559, 0.397), ("S18", 0.2379, 0.471), ("S19", 0.520, 0.750),
            ("S21", 0.2190, 0.764), ("S22", 1.31, 0.449), ("S23", 0.253, 0.56),
            ("S24", 0.944, 0.897), ("S29", 0.428, 0.728), ("S31", 0.449, 0.5497),
            ("S33", 0.657, 0.608), ("S38", 0.1416, 0.8201), ("S39", 0.370, 0.9236),
            ("S42", 0.95, 0.567), ("S54", 1.20, 0.893), ("S55", 0.1078, 0.7209),
            ("S60", 0.3877, 0.7179), ("S66", 1.502, 0.128), ("S67", 1.126, 0.293),
            ("S71", 0.973, 0.899), ("S83", 2.785, 0.365), ("S85", 4.6, 0.78),
            ("S87", 2.74, 0.224), ("S89", 1.081, 0.639), ("S91", 1.917, 0.303),
            ("S96", 1.499, 0.174), ("S97", 2.32, 0.35), ("S145", 1.1, 0.5),
            ("S175", 0.414, 0.9867)]
    sats = []
    for name, a, e in stars:
        if name == "S2":  # periapsis 2018.38, P = 16.05 yr -> shown at epoch 2010.0
            argp, M = (66.13 + 227.85) * DEG, 2 * math.pi * (2010.0 - 2018.38) / 16.05
        else:
            argp, M = rng.uniform(0, 2 * math.pi), rng.uniform(0, 2 * math.pi)
        sats.append(Orbiter(name, 10 * M_SUN, a * arcsec, e, argp, M))
    bodies, pairs = build_system("Sgr A*", m_bh, sats)
    s2 = stars[0][1] * arcsec
    return Scenario(bodies, "37 S-stars around Sagittarius A* (4.3 million solar masses), with "
            "semi-major axes and eccentricities from Gillessen et al. (2017). S2 reaches 7650 "
            "km/s at periapsis; S175 and S14 dive within ~50 AU. Newtonian (no relativistic "
            "precession).", view_circle(2.6 * s2), show_period=orbital_period(G * m_bh, s2),
            show_seconds=12, pairs=pairs, trails=10, check_time=4 * orbital_period(G * m_bh, s2),
            tips=["Zoom out to see the outer S-stars (periods up to ~3500 years)."])


@scenario("kepler-16", "systems",
        "Tatooine: a planet and a debris ring around a pair of suns (Kepler-16, 1.5k bodies)")
def kepler_16(rng, scale):
    m_a, m_b = 0.6897 * M_SUN, 0.20255 * M_SUN  # Doyle et al. (2011)
    p_bin, e_bin = 41.079220 * DAY, 0.15944
    mu_bin = G * (m_a + m_b)
    a_bin = (mu_bin * p_bin ** 2 / (4 * math.pi ** 2)) ** (1 / 3)
    stars, pairs = build_system("Kepler-16A", m_a, [Orbiter("Kepler-16B", m_b, a_bin, e_bin,
            263.464 * DEG, rng.uniform(0, 2 * math.pi))])
    m_p = 0.333 * M_JUPITER
    p_planet = 228.776 * DAY
    a_p = (G * (m_a + m_b + m_p) * p_planet ** 2 / (4 * math.pi ** 2)) ** (1 / 3)
    r, v = orbit_state(G * (m_a + m_b + m_p), a_p, 0.0069, 318.0 * DEG, rng.uniform(0, 2 * math.pi))
    planet = Bodies.single("Kepler-16b", m_p, r, v)
    # Test-particle ring from inside the stability limit (~2.9 a_bin) outwards. Circular speeds
    # include the binary's time-averaged quadrupole.
    n = scaled(1500, scale, 10)
    ring = PowerLawDisk(1.0, 1.0, 1.5 * a_bin, 7.0 * a_bin)
    q_coef = m_a * m_b / (m_a + m_b) ** 2 * (1 + 1.5 * e_bin ** 2)
    pos, vel = circular_disk(rng, n, ring, lambda R: np.sqrt(mu_bin / R * (1 + 0.75 * q_coef * (
            a_bin / R) ** 2)))
    debris = Bodies.group("debris-", 1e10, pos, vel)
    bodies = Bodies.concat(stars, planet, debris).in_com_frame()
    pairs.append(BoundPair("Kepler-16AB", "Kepler-16b", a_p, 0.0069, G * (m_a + m_b + m_p),
            ("Kepler-16A", "Kepler-16B")))
    return Scenario(bodies, "Kepler-16, the first known circumbinary planet: an orange and a red "
            "dwarf on a 41-day eccentric orbit, Saturn-mass Kepler-16b at 0.70 AU, and a ring of "
            f"{n} massless debris particles from 1.5 to 7 binary separations. Debris inside the "
            "stability limit (~2.9 a_bin = 0.65 AU, right where the planet sits) is stirred into "
            "chaos and cleared within a few dozen binary orbits.", view_circle(1.75 * AU),
            show_period=p_bin, show_seconds=2.5, softening=0.005 * AU,
            pairs=pairs, trails=1.0, theta=0.6, check_time=40 * p_bin,
            tips=["The two stars and the planet are the bodies that never leave the centre."])


# ------------------------------------------------------------------------------------------------
# Scenarios: rings and disks (test particles around massive bodies, Barnes-Hut GPU)
# ------------------------------------------------------------------------------------------------
def bound_fraction_check(label, idx_of, center_name, a_tolerance=0.1, required=0.98):
    """--verify check: the share of particles still orbiting `center_name` with a similar
    semi-major axis."""

    def check(init, final):
        c = final.index(center_name)
        idx = idx_of(init)

        def axes(b):
            mu = G * (b.mass[c] + b.mass[idx])
            dr, dv = b.pos[idx] - b.pos[c], b.vel[idx] - b.vel[c]
            energy = 0.5 * (dv ** 2).sum(1) - mu / np.hypot(*dr.T)
            return np.where(energy < 0, -mu / (2 * np.minimum(energy, -1e-300)), np.inf)

        kept = np.mean(np.abs(axes(final) / axes(init) - 1) < a_tolerance)
        return [(kept >= required, f"{kept:.2%} of the {label} still orbit {center_name} within "
                f"{a_tolerance:.0%} of their semi-major axis (required {required:.1%})")]

    return check


def ids_with_prefix(prefix):
    return lambda b: np.flatnonzero(np.char.startswith(b.ids, prefix))


@scenario("saturn-rings", "disks",
        "Saturn's rings with their gaps, shepherd moons and major moons (250k bodies)")
def saturn_rings(rng, scale):
    saturn = mass_from_gm(PLANET_GM["Saturn"])
    small = [("Pan", 133584, 0.0, 4.95e15), ("Daphnis", 136505, 0.0, 7.7e13),
            ("Atlas", 137670, 0.0012, 6.6e15), ("Prometheus", 139380, 0.0022, 1.595e17),
            ("Pandora", 141720, 0.0042, 1.371e17), ("Epimetheus", 151422, 0.0098, 5.266e17),
            ("Janus", 151472, 0.0068, 1.8975e18)]
    moons = [Orbiter(n, m, a * KM, e, rng.uniform(0, 2 * math.pi), rng.uniform(0, 2 * math.pi))
            for n, a, e, m in small]
    moons += [Orbiter(n, mass_from_gm(gm), a * KM, e, rng.uniform(0, 2 * math.pi),
            rng.uniform(0, 2 * math.pi)) for n, a, e, gm, _ in MOONS["Saturn"]]
    system, pairs = build_system("Saturn", saturn, moons)
    s = system.index("Saturn")

    # Ring segments: inner, outer [km], relative particle surface density (~ optical depth)
    segments = [(66900, 74510, 0.02),  # D ring
            (74658, 91975, 0.12),  # C ring
            (91975, 117507, 1.00),  # B ring
            (117507, 122340, 0.07),  # Cassini Division
            (122340, 133410, 0.50),  # A ring, inside the Encke gap (Pan)
            (133740, 136485, 0.42),  # A ring between the Encke and Keeler (Daphnis) gaps
            (136530, 136775, 0.42)]  # A ring outer edge
    n = scaled(250_000, scale, 1000)
    n_f = n // 60  # the narrow F ring
    area = np.array([w * (r2 ** 2 - r1 ** 2) for r1, r2, w in segments])
    counts = np.floor((n - n_f) * area / area.sum()).astype(int)
    counts[2] += n - n_f - counts.sum()
    radii = []
    for (r1, r2, _), count in zip(segments, counts):
        # ringlets and density waves: random radial modulation of the particle density
        waves = [(rng.uniform(300, 3000), rng.uniform(0, 2 * math.pi)) for _ in range(4)]
        got = []
        while sum(len(g) for g in got) < count:
            r = np.sqrt(rng.uniform(r1 ** 2, r2 ** 2, 2 * count + 100))
            mod = 1 + sum(0.18 * np.sin(2 * math.pi * r / lam + ph) for lam, ph in waves)
            got.append(r[rng.random(len(r)) * 1.8 < mod])
        radii.append(np.concatenate(got)[:count])
    radii.append(140220 + 25 * rng.standard_normal(n_f))  # F ring core
    r = np.concatenate(radii) * KM
    th = rng.uniform(0, 2 * math.pi, len(r))
    v = np.sqrt(G * saturn / r)
    ring = Bodies.group("ring-", 1e3, np.stack([r * np.cos(th), r * np.sin(th)], -1),
            np.stack([-v * np.sin(th), v * np.cos(th)], -1))
    bodies = Bodies.concat(system, ring.moved(system.pos[s], system.vel[s])).in_com_frame()
    mimas = MOONS["Saturn"][0][1] * KM
    return Scenario(bodies, f"Saturn with {n:,} ring particles (D, C, B and A rings, the "
            "Cassini Division, the Encke and Keeler gaps with Pan and Daphnis inside them, and "
            "the narrow F ring between its shepherds Prometheus and Pandora), co-orbital Janus "
            "and Epimetheus, and Saturn's eight major moons out to Iapetus. Ring particles are "
            "massless; moons have their real masses.", (2.9e8, 1.55e8),
            show_period=orbital_period(G * saturn, mimas), show_seconds=12,
            dt_max=orbital_period(G * saturn, 66900 * KM) / STEPS_PER_ORBIT, pairs=pairs,
            trails=0.4, theta=0.6, check_time=5 * DAY,
            check=bound_fraction_check("ring particles", ids_with_prefix("ring-"), "Saturn",
                    a_tolerance=0.01, required=0.999),
            tips=["Zoom into the outer A ring for Pan and Daphnis in their gaps, and the F ring "
                    "with Prometheus and Pandora.",
                    "Janus and Epimetheus share an orbit and swap places every ~4 years."])


@scenario("binary-black-hole", "disks",
        "Two supermassive black holes clearing a cavity in a 100k-particle disk")
def binary_black_hole(rng, scale):
    m1, m2 = 7.5e7 * M_SUN, 2.5e7 * M_SUN
    a, e = 0.02 * PC, 0.3
    mu = G * (m1 + m2)
    holes, pairs = build_system("BH-1", m1, [Orbiter("BH-2", m2, a, e, 0.0, math.pi)])
    n = scaled(100_000, scale, 1000)
    disk = PowerLawDisk(1.0, 1.0, 1.6 * a, 7.0 * a)
    q_coef = m1 * m2 / (m1 + m2) ** 2 * (1 + 1.5 * e * e)
    pos, vel = circular_disk(rng, n, disk, lambda R: np.sqrt(mu / R * (1 + 0.75 * q_coef * (
            a / R) ** 2)))
    bodies = Bodies.concat(holes, Bodies.group("gas-", 1e20, pos, vel)).in_com_frame()
    eps = 0.08 * a
    P = orbital_period(mu, a)
    return Scenario(bodies, "A 3:1 pair of supermassive black holes (1e8 solar masses in "
            "total, e = 0.3, 26-year orbit) inside a disk of 100,000 massless gas clouds. The "
            "binary cannot hold material closer than ~2-3 separations: the inner disk is flung "
            "into spiral streams and a lopsided cavity opens, as in simulations of "
            "circumbinary accretion.", view_circle(7.4 * a), show_period=P, show_seconds=3,
            softening=eps, dt_max=0.2 * math.sqrt(eps ** 3 / (G * m1)), pairs=pairs, trails=0.5,
            theta=0.6, check_time=15 * P, softening_tolerance=0.15)  # binary precesses a bit


@scenario("solar-system-belts", "disks",
        "Planets plus the asteroid belt, Hildas, Trojans and Kuiper belt (50k bodies)")
def solar_system_belts(rng, scale):
    system, pairs = build_system("Sun", M_SUN, solar_system_orbiters(rng, moons=False))
    sun = system.index("Sun")
    jup = system.index("Jupiter")
    nep = system.index("Neptune")
    lam_of = lambda i: math.atan2(*(system.pos[i] - system.pos[sun])[::-1])
    n = scaled(50_000, scale, 1000)
    counts = particle_counts(n, [0.55, 0.04, 0.09, 0.32])
    mu_sun = G * M_SUN
    parts = []

    # Main belt with the Kirkwood gaps at the 3:1, 5:2, 7:3 and 2:1 resonances with Jupiter
    a = rng.uniform(2.1, 3.28, 3 * counts[0])
    for gap, half in ((2.502, 0.025), (2.825, 0.015), (2.958, 0.01)):
        a = a[np.abs(a - gap) > half]
    a = a[:counts[0]] * AU
    e = np.minimum(rng.rayleigh(0.09, counts[0]), 0.3)
    r, v = orbit_state(mu_sun, a, e, rng.uniform(0, 2 * math.pi, counts[0]),
            rng.uniform(0, 2 * math.pi, counts[0]))
    parts.append(Bodies.group("mainbelt-", 1e12, r, v))

    # Hildas: 3:2 resonance with Jupiter, conjunctions at perihelion (3 L_J - 2 L - varpi ~ 0)
    k = counts[1]
    a = rng.uniform(3.85, 4.05, k) * AU
    e = rng.uniform(0.08, 0.28, k)
    varpi = rng.uniform(0, 2 * math.pi, k)
    phi = rng.uniform(-40, 40, k) * DEG
    lam = (3 * lam_of(jup) - varpi - phi) / 2 + math.pi * rng.integers(0, 2, k)
    r, v = orbit_state(mu_sun, a, e, varpi, lam - varpi)
    parts.append(Bodies.group("hilda-", 1e12, r, v))

    # Jupiter Trojans: tadpole orbits around L4 (60 deg ahead of Jupiter) and L5 (60 behind)
    k = counts[2]
    a_j = PLANET_ELEMENTS["Jupiter"][0] * AU
    a = a_j * (1 + 0.003 * rng.standard_normal(k))
    e = rng.uniform(0, 0.08, k)
    varpi = rng.uniform(0, 2 * math.pi, k)
    lam = lam_of(jup) + np.where(rng.random(k) < 0.6, 60, -60) * DEG + rng.uniform(-18, 18,
            k) * DEG
    r, v = orbit_state(G * (M_SUN + system.mass[jup]), a, e, varpi, lam - varpi)
    parts.append(Bodies.group("trojan-", 1e12, r, v))

    # Kuiper belt: classical, plutinos (3:2 with Neptune, aphelion at conjunction) and scattered
    k = counts[3]
    kind = rng.choice(3, k, p=[0.6, 0.25, 0.15])
    a = np.where(kind == 0, rng.uniform(42, 48, k), np.where(kind == 1, rng.uniform(39.2, 39.6,
            k), rng.uniform(50, 150, k)))
    q = rng.uniform(30, 38, k)
    e = np.where(kind == 0, rng.uniform(0, 0.1, k), np.where(kind == 1, rng.uniform(0.1, 0.3, k),
            1 - q / a))
    varpi = rng.uniform(0, 2 * math.pi, k)
    lam = rng.uniform(0, 2 * math.pi, k)
    phi = rng.uniform(140, 220, k) * DEG
    lam = np.where(kind == 1, (phi + 2 * lam_of(nep) + varpi) / 3 + 2 * math.pi / 3 * rng.integers(
            0, 3, k), lam)
    r, v = orbit_state(G * system.total_mass, a * AU, e, varpi, lam - varpi)
    kuiper = Bodies.group("kuiper-", 1e12, r, v)

    inner = Bodies.concat(*parts).moved(system.pos[sun], system.vel[sun])
    bodies = Bodies.concat(system, inner, kuiper).in_com_frame()
    T_j = orbital_period(G * M_SUN, a_j)
    return Scenario(bodies, f"The Solar System at J2000 with {n:,} small bodies: the main "
            "asteroid belt with its Kirkwood gaps, the Hilda group in 3:2 resonance with "
            "Jupiter, Jupiter's Trojan swarms at L4 and L5, and the Kuiper belt (classical "
            "objects, Plutinos in 3:2 resonance with Neptune, scattered disk). Planets carry "
            "their moons' mass; small bodies are massless.", view_circle(5.8 * AU),
            show_period=T_j, show_seconds=40, pairs=pairs, trails=0.3, theta=0.6,
            check_time=T_j, check=bound_fraction_check("main-belt asteroids",
                    ids_with_prefix("mainbelt-"), "Sun", required=0.98),
            tips=["The Trojan swarms ride 60 degrees ahead of and behind Jupiter.",
                    "Zoom out (mouse wheel) for the Kuiper belt at 30-50 AU."])


@scenario("stellar-flyby", "disks", "A passing star tears through two debris disks (90k bodies)")
def stellar_flyby(rng, scale):
    m_a, m_b = M_SUN, 0.6 * M_SUN
    eps = 0.5 * AU
    planets = [Orbiter("A-planet-1", 95 * M_EARTH, 11 * AU, 0.02, rng.uniform(0, 2 * math.pi),
            rng.uniform(0, 2 * math.pi)), Orbiter("A-planet-2", 17 * M_EARTH, 18 * AU, 0.01,
            rng.uniform(0, 2 * math.pi), rng.uniform(0, 2 * math.pi))]
    star_a, pairs = build_system("Star-A", m_a, planets)
    n = scaled(90_000, scale, 1000)
    n_a = 2 * n // 3
    pos, vel = circular_disk(rng, n_a, PowerLawDisk(1.0, 1.0, 25 * AU, 100 * AU),
            lambda R: np.sqrt(G * star_a.total_mass / R))
    disk_a = Bodies.concat(star_a, Bodies.group("disk-A-", 1e15, pos, vel))
    pos, vel = circular_disk(rng, n - n_a, PowerLawDisk(1.0, 1.0, 10 * AU, 50 * AU),
            lambda R: np.sqrt(G * m_b / R))
    disk_b = Bodies.concat(Bodies.single("Star-B", m_b), Bodies.group("disk-B-", 1e15, pos, vel))
    # Parabolic, prograde encounter (both disks turn the same way as the orbit)
    bodies = place_parabolic(disk_a, disk_b, 120 * AU, 650 * YEAR).in_com_frame()
    return Scenario(bodies, "A Sun-like star with two giant planets and a 25-100 AU debris "
            f"disk meets a 0.6 solar-mass star with its own disk ({n:,} massless particles in "
            "total). The parabolic, prograde encounter (closest approach 120 AU after ~650 "
            "years) pulls out tidal tails and a bridge, and the stars steal each other's "
            "debris, as in models of stellar flybys in birth clusters.", view_circle(330 * AU),
            show_period=1600 * YEAR, show_seconds=60, softening=eps, pairs=pairs, trails=0.4,
            theta=0.6, check_time=1600 * YEAR)


@scenario("protoplanetary-disk", "disks",
        "A massive young disk around a star grows spiral arms (self-gravitating, 200k bodies)")
def protoplanetary_disk(rng, scale):
    star = M_SUN
    disk = PowerLawDisk(0.25 * M_SUN, 1.0, 5 * AU, 100 * AU)
    eps = 0.5 * AU
    fld = RadialField([disk], eps, point_mass=star)
    n = scaled(200_000, scale, 1000)
    pos, vel = sample_cold_disk(rng, n - 1, disk, fld, Q=1.3)
    bodies = Bodies.concat(Bodies.single("Star", star),
            Bodies.group("disk-", disk.mass / (n - 1), pos, vel)).in_com_frame()
    return Scenario(bodies, "A young star with a disk of a quarter of its mass (5-100 AU, "
            f"{n - 1:,} self-gravitating particles, Toomre Q = 1.3). Gravitational instability "
            "grows multi-armed spirals within a few outer orbits, which then heat the disk, "
            "as in models of massive protoplanetary disks such as Elias 2-27.",
            view_circle(105 * AU), show_period=orbital_period(G * 1.12 * M_SUN, 40 * AU),
            show_seconds=30, softening=eps,
            dt_max=orbital_period(G * star, 5 * AU) / STEPS_PER_ORBIT, trails=0.0, theta=0.6,
            self_gravitating=True, check_time=orbital_period(G * 1.12 * M_SUN, 40 * AU))


# ------------------------------------------------------------------------------------------------
# Scenarios: galaxies and other self-gravitating systems (Barnes-Hut GPU)
# ------------------------------------------------------------------------------------------------
def galaxy_dt_max(fld, eps):
    """100 steps per orbit where orbits are fastest, and dt < 0.3 sqrt(eps / a_max)."""
    return min(fld.min_orbital_period() / 100, 0.3 * math.sqrt(eps / fld.max_acceleration()))


def spiral_model(mass=1.0, size=1.0, Q=1.2, halo_weight=3.0):
    """Sub-maximal disk (about half of the mass inside 2.2 disk scale lengths) with a compact
    bulge and a cored dark halo (planar, see MestelDisk). Halo particles are `halo_weight` times
    heavier, so most of the particles (and pixels: every body is drawn alike, so the halo shows
    up as a haze) go to the disk; heavier halo particles heat the disk faster, though."""
    return GalaxyModel(disk=ExponentialDisk(4e10 * mass * M_SUN, 3 * size * KPC, 15 * size * KPC),
            bulge=PlummerDisk(1e10 * mass * M_SUN, 0.8 * size * KPC, 6 * size * KPC),
            halo=Tapered(MestelDisk(1.4e11 * mass * M_SUN, 3 * size * KPC, 40 * size * KPC),
                    25 * size * KPC), Q=Q,
            halo_particle_weight=halo_weight)


def galaxy_scenario(bodies, fld, eps, description, view, show_seconds, tips, check_rot=2.0,
        show_radius=8 * KPC, theta=0.3, trails=0.0, virial_prefix=""):
    T = 2 * math.pi * show_radius / float(fld.v_circ(show_radius))
    return Scenario(bodies, description, view, show_period=T, show_seconds=show_seconds,
            softening=eps, dt_max=galaxy_dt_max(fld, eps), trails=trails, theta=theta, tips=tips,
            self_gravitating=True, check_time=check_rot * T, virial_prefix=virial_prefix)


@scenario("galaxy-spiral", "galactic", "A disk galaxy that grows its own spiral arms (300k bodies)")
def galaxy_spiral(rng, scale):
    eps = 0.08 * KPC
    bodies, fld = make_galaxy(rng, spiral_model(), scaled(300_000, scale, 2000), eps)
    return galaxy_scenario(bodies, fld, eps, "A Milky-Way-sized disk galaxy: exponential disk "
            "(4e10 solar masses, Toomre Q = 1.2), bulge and dark halo, all live particles. Swing "
            "amplification of the particle noise grows two- and three-armed spirals within a "
            "rotation; after ~2 rotations a bar forms in the centre, as in many real spirals.",
            view_circle(17 * KPC), 20, ["Each rotation at 8 kpc takes "
            "~250 million years."])


@scenario("galaxy-barred", "galactic",
        "A heavy disk that forms a central bar and two arms (300k bodies)")
def galaxy_barred(rng, scale):
    eps = 0.08 * KPC
    model = GalaxyModel(disk=ExponentialDisk(6e10 * M_SUN, 3 * KPC, 15 * KPC),
            bulge=PlummerDisk(0.4e10 * M_SUN, 0.4 * KPC, 4 * KPC),
            halo=Tapered(MestelDisk(5e10 * M_SUN, 8 * KPC, 40 * KPC), 25 * KPC), Q=1.5,
            halo_particle_weight=3.0)
    bodies, fld = make_galaxy(rng, model, scaled(300_000, scale, 2000), eps)
    return galaxy_scenario(bodies, fld, eps, "A disk-dominated galaxy (maximal disk, small "
            "bulge, light halo, Q = 1.5). Such disks are bar-unstable (Ostriker & Peebles 1973): "
            "a central bar with trailing spiral arms forms within ~2 rotations.",
            view_circle(17 * KPC), 20, ["The bar forms in the inner ~6 kpc after a few hundred "
            "million years."], check_rot=1.0)


@scenario("galaxy-whirlpool", "galactic",
        "A companion galaxy flies by and raises grand-design spiral arms (M51-like, 400k bodies)")
def galaxy_whirlpool(rng, scale):
    eps = 0.08 * KPC
    n = scaled(400_000, scale, 2000)
    main, fld = make_galaxy(rng, spiral_model(Q=1.5, halo_weight=8.0), n * 7 // 8, eps, "M51a-")
    comp_model = GalaxyModel(bulge=PlummerDisk(4.5e10 * M_SUN, 1.5 * KPC, 12 * KPC))
    comp, _ = make_galaxy(rng, comp_model, n - n * 7 // 8, eps, "M51b-")
    bodies = place_parabolic(main, comp, 22 * KPC, 250 * MYR, rng.uniform(0, 2 * math.pi))
    return galaxy_scenario(bodies.in_com_frame(), fld, eps, "A stable disk galaxy (Q = 1.5) and "
            "a compact companion a quarter of its mass on a prograde parabolic orbit with a 22 "
            "kpc perigalacticon after 250 million years. The tidal kick raises the two-armed "
            "grand-design spiral and the bridge that make M51, the Whirlpool, famous (Toomre & "
            "Toomre 1972).", view_circle(45 * KPC), 20, ["Closest approach happens about 250 "
            "million years in.", "The haze around the galaxy is its dark-matter halo: every body "
            "is drawn alike."], check_rot=3.0, virial_prefix="M51a-")


@scenario("galaxy-collision", "galactic",
        "Two spiral galaxies merge, throwing out long tidal tails (Antennae-like, 1M bodies)")
def galaxy_collision(rng, scale):
    eps = 0.08 * KPC
    n = scaled(1_000_000, scale, 4000)
    g1, fld = make_galaxy(rng, spiral_model(Q=1.5, halo_weight=8.0), n // 2, eps, "G1-")
    g2, _ = make_galaxy(rng, spiral_model(Q=1.5, halo_weight=8.0), n - n // 2, eps, "G2-")
    g2 = g2.rotated(rng.uniform(0, 2 * math.pi))
    bodies = place_parabolic(g1, g2, 9 * KPC, 300 * MYR, rng.uniform(0, 2 * math.pi))
    return galaxy_scenario(bodies.in_com_frame(), fld, eps, "Two identical Milky-Way-sized disk "
            "galaxies (500k particles each, with bulges and dark halos) on a prograde parabolic "
            "orbit with a 9 kpc closest approach after 300 million years. Both disks fling out "
            "long tidal tails and bridges, fall back together and merge within ~1.5 billion "
            "years, like the Antennae galaxies (Toomre & Toomre 1972).", view_circle(50 * KPC),
            30, ["The galaxies meet ~300 million years in; zoom out to follow the tails.",
            "The haze around each galaxy is its dark-matter halo: every body is drawn alike."],
            check_rot=2.0, virial_prefix="G1-")


@scenario("tidal-stream", "galactic",
        "A dwarf galaxy torn into long tidal streams by a massive host (100k bodies)")
def tidal_stream(rng, scale):
    host = 2e11 * M_SUN
    dwarf = PlummerDisk(2e9 * M_SUN, 1.0 * KPC, 8 * KPC)
    eps = 0.03 * KPC
    fld = RadialField([dwarf], eps)
    n = scaled(100_000, scale, 1000)
    pos, vel = sample_hot(rng, n - 1, dwarf, fld)
    sat = Bodies.group("dwarf-", dwarf.mass / (n - 1), pos, vel).in_com_frame()
    apo, peri = 50 * KPC, 10 * KPC
    a, e = (apo + peri) / 2, (apo - peri) / (apo + peri)
    mu = G * (host + dwarf.mass)
    r, v = orbit_state(mu, a, e, rng.uniform(0, 2 * math.pi), math.pi)  # start at apocentre
    f = dwarf.mass / (host + dwarf.mass)
    bodies = Bodies.concat(Bodies.single("host", host, -f * r, -f * v),
            sat.moved((1 - f) * r, (1 - f) * v)).in_com_frame()
    P = orbital_period(mu, a)
    t_peri = PERIAPSIS_STEP_FRACTION * periapsis_time_scale(G * host, 0.5 * peri)
    return Scenario(bodies, "A dwarf galaxy (2e9 solar masses, 100k stars in equilibrium) on an "
            "eccentric orbit (50 to 10 kpc) around a 2e11 solar-mass host, drawn as a single "
            "point mass. Each pericentre passage strips stars into a leading and a trailing "
            "stream, which wrap around the host like the Sagittarius stream.",
            view_circle(58 * KPC), show_period=P, show_seconds=20, softening=eps,
            dt_max=min(galaxy_dt_max(fld, eps), t_peri), trails=0.0,
            self_gravitating=True, check_time=2 * P, virial_prefix="dwarf-",
            tips=["Each orbit takes ~1.1 billion years; streams grow at every pericentre."])


@scenario("tidal-disruption", "galactic",
        "A star shredded by a black hole: half its debris escapes, half falls back (50k bodies)")
def tidal_disruption(rng, scale):
    m_bh, m_star = 2000 * M_SUN, M_SUN
    star = PlummerDisk(m_star, 0.4 * R_SUN, 2.5 * R_SUN)
    eps = 0.02 * R_SUN
    fld = RadialField([star], eps)
    n = scaled(50_000, scale, 1000)
    pos, vel = sample_hot(rng, n - 1, star, fld)
    blob = Bodies.group("star-", m_star / (n - 1), pos, vel).in_com_frame()
    r_t = R_SUN * (m_bh / m_star) ** (1 / 3)  # tidal radius
    q = r_t / 1.5  # penetration factor beta = 1.5
    t_dyn = math.sqrt((0.4 * R_SUN) ** 3 / (G * m_star))
    bodies = place_parabolic(Bodies.single("black-hole", m_bh), blob, q, 30 * t_dyn)
    a_min = r_t ** 2 / (2 * R_SUN)  # most bound debris
    t_fallback = orbital_period(G * m_bh, a_min)
    return Scenario(bodies.in_com_frame(), "A Sun-like star (50k particles held together only "
            "by their own gravity) on a parabolic orbit around a 2000 solar-mass black hole, "
            "passing 1.5x inside its tidal radius. It is stretched into a stream: about half the "
            "debris is unbound and escapes, the rest returns on eccentric orbits after ~"
            f"{t_fallback / 3600:.0f} hours and wraps around the hole.", view_circle(120 * R_SUN),
            show_period=30 * t_dyn + t_fallback, show_seconds=60, softening=eps,
            dt_max=min(fld.min_orbital_period() / 100,
                    PERIAPSIS_STEP_FRACTION * periapsis_time_scale(G * m_bh, q - 2 * R_SUN)),
            trails=0.3, self_gravitating=True, check_time=30 * t_dyn + t_fallback,
            virial_prefix="star-",
            tips=["The star reaches the black hole after a few hours of simulated time."])


@scenario("cosmic-web", "galactic",
        "A toy 2D universe expands, grows a web of filaments and clusters, and recollapses (1M)")
def cosmic_web(rng, scale):
    """A Maclaurin disk has an in-plane force exactly linear in R, so with a Hubble flow it
    expands homologously like a closed Friedmann universe: a'' = -C / a^2 with
    C = Omega0^2 = 3 pi G M / (4 R0^3). Particles start on a jittered grid (quiet, no Poisson
    noise on large scales) displaced by a Gaussian random field (Zel'dovich-style) that seeds
    the filaments."""
    n_target = scaled(1_000_000, scale, 10_000)
    mass, r0 = 2e15 * M_SUN, 10e3 * KPC
    a_max = 3.0  # expands to 3x its size before turning around
    omega0 = math.sqrt(3 * math.pi * G * mass / (4 * r0 ** 3))
    h0 = math.sqrt(2 * (1 - 1 / a_max)) * omega0  # (a'^2 / 2 - C / a = -C / a_max at a = 1)

    # Jittered grid in the unit disk
    k = math.sqrt(n_target / math.pi)
    g = np.arange(-math.ceil(k), math.ceil(k) + 1)
    q = np.stack(np.meshgrid(g, g), -1).reshape(-1, 2) + rng.uniform(-0.5, 0.5, (len(g) ** 2, 2))
    q = q[np.hypot(*q.T) < k] / k

    # Gaussian random displacement field: delta has equal power on every scale between ~1/2
    # and ~1/8 of the radius (P ~ k^-2, band-limited so the disk keeps a round edge)
    ng, box = 512, 2.4
    kk = 2 * math.pi * np.fft.fftfreq(ng, box / ng)
    kx, ky = np.meshgrid(kk, kk, indexing="ij")
    k2 = kx ** 2 + ky ** 2
    k2[0, 0] = 1.0
    k_lo, k_hi = 2 * math.pi / 0.5, 2 * math.pi / 0.12
    amp = np.sqrt(k2 ** -1.0 * np.exp(-k2 / k_hi ** 2) * (1 - np.exp(-k2 / k_lo ** 2)))
    amp[0, 0] = 0.0
    delta = amp * (rng.standard_normal((ng, ng)) + 1j * rng.standard_normal((ng, ng)))
    psi = [np.real(np.fft.ifft2(1j * kd * delta / k2)) for kd in (kx, ky)]
    rms = math.sqrt(np.mean(psi[0] ** 2 + psi[1] ** 2))
    shift = []
    coords = ((q + box / 2) / box * ng).T
    for comp in psi:
        shift.append(map_coordinates(comp / rms, coords, order=1, mode="wrap"))
    disp = 5.0 / k * np.stack(shift, -1)  # rms displacement: 5 grid spacings

    def to_maclaurin(u):  # uniform unit disk -> Maclaurin disk of radius r0 (same mass fraction)
        r = np.hypot(*u.T)
        rm = np.sqrt(1 - (1 - np.minimum(r, 1) ** 2) ** (2 / 3))
        return u * (r0 * rm / np.maximum(r, 1e-300))[:, None]

    x0, x = to_maclaurin(q), to_maclaurin(q + disp)
    vel = h0 * x + h0 * (x - x0)  # Hubble flow + growing-mode peculiar velocity
    n = len(x)
    bodies = Bodies.group("p", mass / n, x, vel).in_com_frame()
    eps = 2.5 * r0 / k
    t_max = math.sqrt((a_max / 2) ** 3) / omega0 * (math.pi - math.acos(1 - 2 / a_max)
            + math.sin(math.acos(1 - 2 / a_max)))
    clump = math.sqrt((2 * eps) ** 3 / (G * 2000 * mass / n))  # 2000-particle clump, r = 2 eps
    return Scenario(bodies, f"{n:,} particles in a 2D toy universe: a disk (Maclaurin profile, "
            "the 2D analogue of a uniform sphere) expanding with a Hubble flow just below escape "
            "speed and seeded with a Gaussian random field. Gravity pulls the near-uniform "
            "matter into a web of filaments and clumps that merge into ever larger clusters "
            f"while the universe expands to 3x its size (after ~{t_max / (1e9 * YEAR):.0f} "
            "billion years), then everything falls back together into one giant cluster.",
            view_circle(2.2 * r0), show_period=t_max, show_seconds=150, softening=eps,
            dt_max=clump / 30, trails=0.0, self_gravitating=True,
            check_time=0.5 * t_max,
            tips=["Zoom out as the universe expands (it grows to 3x its starting size)."])


# ------------------------------------------------------------------------------------------------
# Planning: algorithm, timestep, softening, view
# ------------------------------------------------------------------------------------------------
@dataclass
class Plan:
    algorithm: str
    theta: float  # value written to the config (diagonal-based for the CPU tree)
    ips: float
    dt: float
    dt_max: float
    dt_limiter: str
    softening_factor: float
    mean_distance: float
    pixel_scale: float


def round_down(x, digits=3):
    """Round down to `digits` significant digits (so timesteps stay below their limits)."""
    if not math.isfinite(x) or x <= 0:
        return x
    exp = math.floor(math.log10(x)) - digits + 1
    return float(f"{math.floor(x / 10 ** exp)}e{exp}")


def round_sig(x, digits=4):
    return float(f"{x:.{digits}g}")


def fmt_time(s):
    for unit, size in (("Gyr", 1e9 * YEAR), ("Myr", MYR), ("kyr", 1e3 * YEAR), ("yr", YEAR),
            ("d", DAY), ("h", 3600.0), ("min", 60.0), ("s", 1.0), ("ms", 1e-3)):
        if abs(s) >= size:
            return f"{s / size:.3g} {unit}"
    return f"{s:.3g} s"


def fmt_dist(m):
    for unit, size, lowest in (("Mpc", 1e3 * KPC, 1e3 * KPC), ("kpc", KPC, 0.5 * KPC),
            ("pc", PC, 0.1 * PC),
            ("AU", AU, 0.001 * AU), ("km", KM, KM)):
        if abs(m) >= lowest:
            return f"{m / size:.3g} {unit}"
    return f"{m:.3g} m"


def plan(sc):
    n = len(sc.bodies)
    algorithm = sc.algorithm or choose_algorithm(n)
    ips = estimate_ips(algorithm, n, sc.theta)
    limits = [(sc.dt_max, "scenario dynamics")]
    limits += [(p.dt_limit, f"{p.secondary} around {p.primary}") for p in sc.pairs]
    dt_max, limiter = min(limits, key=lambda lim: lim[0])
    dt = round_down(min(dt_max, sc.show_period / (sc.show_seconds * ips)))
    if not TIMESTEP_RANGE[0] <= dt <= TIMESTEP_RANGE[1]:
        raise ValueError(f"timestep {dt:g} s outside the simulator's range {TIMESTEP_RANGE}")
    mean_d = mean_pairwise_distance(sc.bodies.pos)
    factor = sc.softening / mean_d if sc.softening else 0.0
    if not SOFTENING_FACTOR_RANGE[0] <= factor <= SOFTENING_FACTOR_RANGE[1]:
        raise ValueError(f"softening factor {factor:g} outside {SOFTENING_FACTOR_RANGE}")
    for p in sc.pairs:  # softening must not touch any orbit that has to hold
        if sc.softening > sc.softening_tolerance * p.a * (1 - p.e):
            raise ValueError(f"softening {fmt_dist(sc.softening)} too large for {p.secondary}'s "
                    f"periapsis {fmt_dist(p.a * (1 - p.e))} around {p.primary}")
    pixel_scale = round_sig(max(sc.view[0] / (VIEW_FILL * RESOLUTION[0] / 2),
            sc.view[1] / (VIEW_FILL * RESOLUTION[1] / 2)))
    if not PIXEL_SCALE_RANGE[0] <= pixel_scale <= PIXEL_SCALE_RANGE[1]:
        raise ValueError(f"pixel scale {pixel_scale:g} m/px outside {PIXEL_SCALE_RANGE}")
    return Plan(algorithm, round_sig(config_theta(algorithm, sc.theta), 3), ips, dt, dt_max,
            limiter, factor, mean_d, pixel_scale)


def make_config(name, sc, p, csv_path):
    n = len(sc.bodies)
    return {
        "IO": {
            # Relative to the config file, which is written next to the CSV
            "universe_infile": csv_path.name,
            "universe_outfile": "output.csv",
            "echo_config": True,
            "echo_bodies": False,
        },
        "Simulation": {
            "timestep": p.dt,
            "iterations": 10_000_000_000,
            "theta": p.theta,
            "algorithm": p.algorithm,
            "softening_factor": p.softening_factor,
            # Only the CPU Barnes-Hut is multithreaded, so other configs don't depend on the
            # machine that generated them
            "threads": min(CPU_THREADS, n) if p.algorithm == BH_CPU else 1,
        },
        "Graphics": {
            "enabled": True,
            "resolution": list(RESOLUTION),
            "fps": 100,
            "vsync": False,
            "pixel_scale": p.pixel_scale,
            "show_grid": False,
            "show_commands_panel": True,
            "show_config_panel": True,
            "show_stats_panel": True,
            "show_action_log_panel": True,
            "panel_update_hz": 1,
            "selection": {"show": True, "show_center_of_mass": True, "follow": False},
            "trails_fade_time_s": sc.trails,
        },
        # Not read by the simulator (unknown keys are ignored): documentation and --verify input
        "Scenario": {
            "name": name,
            "description": sc.description,
            "bodies": n,
            "softening_m": sc.softening,
            "timestep_max_s": round_sig(p.dt_max) if math.isfinite(p.dt_max) else None,
            "timestep_limited_by": p.dt_limiter if math.isfinite(p.dt_max) else None,
            "expected_iterations_per_s": round_sig(p.ips, 3),
            "simulated_seconds_per_second": round_sig(p.dt * p.ips, 3),
            "tips": sc.tips,
            "bound_pairs": [[bp.primary, bp.secondary] for bp in sc.pairs],
        },
    }


def to_json(obj, indent=0, prefix=0):
    """json.dumps(obj, indent=4), except that lists fitting in 100 columns stay on one line.
    `prefix` is the room taken on that line by the key and the trailing comma."""
    inline = json.dumps(obj, ensure_ascii=False)
    if not isinstance(obj, (dict, list)) or not obj or (
            isinstance(obj, list) and indent + prefix + len(inline) <= 100):
        return inline
    pad = " " * (indent + 4)
    if isinstance(obj, dict):
        items = [f"{pad}{json.dumps(k)}: {to_json(v, indent + 4, len(json.dumps(k)) + 3)}"
                for k, v in obj.items()]
        return "{\n" + ",\n".join(items) + "\n" + " " * indent + "}"
    items = [pad + to_json(v, indent + 4, 1) for v in obj]
    return "[\n" + ",\n".join(items) + "\n" + " " * indent + "]"


def validate_bodies(b):
    problems = []
    if not (np.isfinite(b.mass).all() and np.isfinite(b.pos).all() and np.isfinite(b.vel).all()):
        problems.append("non-finite values")
    if (b.mass < 0).any():
        problems.append("negative masses")
    if len(np.unique(b.ids)) != len(b):
        problems.append("duplicate ids")
    if len(np.unique(b.pos, axis=0)) != len(b):
        problems.append("coincident bodies (they wouldn't pull each other)")
    com_p, com_v = b.com()
    if np.hypot(*com_p) > 1e-6 * np.abs(b.pos).max() or np.hypot(*com_v) > 1e-6 * np.abs(
            b.vel).max():
        problems.append("not in the centre-of-mass frame (the system would drift off screen)")
    if problems:
        raise ValueError("invalid bodies: " + "; ".join(problems))


def write_csv(path, b):
    """Full round-trip precision up to 100k bodies, 10 significant digits above (positions then
    stay ~1e-10 of the system size, far below any softening length)."""
    row = "{},{!r},{!r},{!r},{!r},{!r}\n"
    if len(b) > 100_000:
        row = "{},{!r},{:.10g},{:.10g},{:.10g},{:.10g}\n"
    with open(path, "w") as f:
        f.write("id,mass,x,y,vel_x,vel_y\n")
        for i0 in range(0, len(b), 100_000):
            s = slice(i0, i0 + 100_000)
            cols = (b.ids[s].tolist(), b.mass[s].tolist(), b.pos[s, 0].tolist(),
                    b.pos[s, 1].tolist(), b.vel[s, 0].tolist(), b.vel[s, 1].tolist())
            f.write("".join(row.format(*r) for r in zip(*cols)))


def read_csv(path):
    data = np.loadtxt(path, delimiter=",", skiprows=1, usecols=(1, 2, 3, 4, 5), ndmin=2)
    ids = np.loadtxt(path, delimiter=",", skiprows=1, usecols=0, dtype=str, ndmin=1)
    return Bodies(ids, data[:, 0], data[:, 1:3], data[:, 3:5])


# ------------------------------------------------------------------------------------------------
# Verification: run the real simulator and compare its output with the initial conditions
# ------------------------------------------------------------------------------------------------
def total_energy(b, eps):
    return 0.5 * float((b.mass * (b.vel ** 2).sum(1)).sum()) + pair_sum(b, eps, max_targets=len(b))


def subsystem(b, prefix):
    """Bodies whose ids start with `prefix`; without one, all bodies except any single body
    holding more than 1% of the mass (a central star or black hole)."""
    keep = np.char.startswith(b.ids, prefix) if prefix else b.mass < 0.01 * b.total_mass
    idx = np.flatnonzero(keep)
    return Bodies(b.ids[idx], b.mass[idx], b.pos[idx], b.vel[idx])


def lagrangian_radii(b, fractions=(0.1, 0.5, 0.9)):
    com = b.com()[0]
    r = np.hypot(*(b.pos - com).T)
    order = np.argsort(r)
    cum = np.cumsum(b.mass[order]) / b.total_mass
    return [float(r[order][np.searchsorted(cum, f)]) for f in fractions]


def pair_elements(b, bp):
    """Semi-major axis and eccentricity of a (Jacobi) BoundPair in state `b`."""
    def bary(names):
        idx = np.array([b.index(nm) for nm in names])
        m = b.mass[idx]
        return m.sum(), (m[:, None] * b.pos[idx]).sum(0) / m.sum(), (m[:, None] * b.vel[
                idx]).sum(0) / m.sum()

    m_in, p_in, v_in = bary(bp.inner or (bp.primary,))
    m_out, p_out, v_out = bary(bp.outer or (bp.secondary,))
    mu = G * (m_in + m_out)
    dr, dv = p_out - p_in, v_out - v_in
    energy = 0.5 * dv @ dv - mu / math.hypot(*dr)
    if energy >= 0:
        return math.inf, math.inf
    a = -mu / (2 * energy)
    h = dr[0] * dv[1] - dr[1] * dv[0]
    return a, math.sqrt(max(0.0, 1 - h * h / (mu * a)))


def verify(sc, p, cfg, csv_path, duration=None):
    """Run the simulator for `duration` simulated seconds at the largest allowed timestep
    (timestep_max_s, the worst case a user can speed up to) and check the result."""
    if not SIMULATOR.exists():
        raise FileNotFoundError(f"{SIMULATOR} not found: build the simulator first")
    duration = duration or sc.check_time or 3 * sc.show_period
    dt = round_down(p.dt_max) if math.isfinite(p.dt_max) else p.dt
    iterations = max(1, math.ceil(duration / dt))
    print(f"  verify: {iterations:,} iterations of {fmt_time(dt)} = {fmt_time(iterations * dt)} "
            f"simulated, ~{fmt_time(iterations / p.ips)} of wall time on the reference machine")
    with tempfile.TemporaryDirectory(prefix="scenario-verify-") as tmp:
        test_cfg = json.loads(json.dumps(cfg))
        test_cfg["IO"].update(universe_infile=str(csv_path.resolve()),
                universe_outfile=str(Path(tmp) / "out.csv"), echo_config=False)
        test_cfg["Simulation"].update(iterations=iterations, timestep=dt)
        test_cfg["Graphics"]["enabled"] = False  # headless
        cfg_path = Path(tmp) / "config.json"
        cfg_path.write_text(json.dumps(test_cfg, indent=4))
        t0 = time.time()
        run = subprocess.run([str(SIMULATOR), str(cfg_path), "--verbosity", "INFO"],
                cwd=REPO_ROOT, capture_output=True, text=True)
        wall = time.time() - t0
        out_path = Path(tmp) / "out.csv"
        if run.returncode != 0 or not out_path.exists():
            print(run.stdout[-2000:], run.stderr[-2000:])
            raise RuntimeError(f"simulator failed (exit code {run.returncode})")
        final = read_csv(out_path)
    print(f"  verify: simulator finished in {wall:.1f} s ({iterations / wall:,.0f} it/s incl. "
            f"startup)")
    return compare(sc, final, p)


def compare(sc, final, p):
    """Scenario health checks between the initial conditions and the simulator's output."""
    init = sc.bodies
    ok = True
    if not np.isfinite(final.pos).all() or not np.isfinite(final.vel).all():
        print("  FAIL: non-finite positions/velocities")
        return False
    eps = sc.softening
    if len(init) <= 20_000:
        e0, e1 = total_energy(init, eps), total_energy(final, eps)
        drift = abs(e1 / e0 - 1)
        tol = 1e-3 if p.algorithm == ALL_PAIRS else 1e-2
        status = "ok" if drift < tol else "WARN"
        print(f"  {status}: total energy changed by {drift:.2e} (tolerance {tol:g})")
        ok &= drift < tol
    if sc.self_gravitating:
        r0 = lagrangian_radii(subsystem(init, sc.virial_prefix))
        r1 = lagrangian_radii(subsystem(final, sc.virial_prefix))
        label = f" of {sc.virial_prefix}*" if sc.virial_prefix else ""
        print(f"  info: 10/50/90% mass radii{label} " + ", ".join(
                f"{fmt_dist(a)} -> {fmt_dist(b)}" for a, b in zip(r0, r1)))
    worst, failed = 0.0, []
    for bp in sc.pairs:
        a0, _ = pair_elements(init, bp)
        a1, _ = pair_elements(final, bp)
        change = abs(a1 / a0 - 1)
        worst = max(worst, change)
        if not change < 0.1:
            failed.append(f"{bp.secondary} around {bp.primary} (a {fmt_dist(a0)} -> "
                    f"{fmt_dist(a1)})")
    if sc.pairs:
        if failed:
            print(f"  FAIL: {len(failed)}/{len(sc.pairs)} bound pairs broke: " + "; ".join(
                    failed[:8]))
            ok = False
        else:
            print(f"  ok: all {len(sc.pairs)} bound pairs hold (largest semi-major axis change "
                    f"{worst:.2%})")
    for passed, message in (sc.check(init, final) if sc.check else []):
        print(f"  {'ok' if passed else 'FAIL'}: {message}")
        ok &= passed
    return ok


# ------------------------------------------------------------------------------------------------
# Command line
# ------------------------------------------------------------------------------------------------
def generate(name, args):
    fn = SCENARIOS[name][2]
    t0 = time.time()
    sc = fn(scenario_rng(name, args.seed), args.scale)
    validate_bodies(sc.bodies)
    p = plan(sc)
    out_dir = Path(args.out_dir) if args.out_dir else REPO_ROOT / SCENARIOS_DIR
    out_dir.mkdir(parents=True, exist_ok=True)
    csv_path, json_path = out_dir / f"{name}.csv", out_dir / f"{name}.json"
    cfg = make_config(name, sc, p, csv_path)
    write_csv(csv_path, sc.bodies)
    json_path.write_text(to_json(cfg) + "\n", encoding="utf-8")

    n = len(sc.bodies)
    theta = "" if p.algorithm == ALL_PAIRS else f", theta {p.theta:g}"
    print(f"{name}: {n:,} bodies, {p.algorithm}{theta} [{time.time() - t0:.1f} s]")
    if sc.softening:
        print(f"  softening  {fmt_dist(sc.softening)} = {p.softening_factor:.4g} x mean pair "
                f"distance {fmt_dist(p.mean_distance)}")
    else:
        print("  softening  none")
    limit = (f"limit {fmt_time(p.dt_max)} from {p.dt_limiter}" if math.isfinite(p.dt_max)
            else "no limit")
    print(f"  timestep   {fmt_time(p.dt)} ({limit})")
    print(f"  speed      ~{p.ips:,.0f} it/s -> {fmt_time(p.dt * p.ips)} per second; "
            f"{fmt_time(sc.show_period)} takes ~{sc.show_period / (p.dt * p.ips):.3g} s")
    if sc.self_gravitating:
        b = subsystem(sc.bodies, sc.virial_prefix) if sc.virial_prefix else sc.bodies
        label = f" of {sc.virial_prefix}*" if sc.virial_prefix else ""
        print(f"  virial     2T/|sum r.F|{label} = {virial_ratio(b, sc.softening):.3f} "
                "(1 = equilibrium)")
    shown = json_path.relative_to(REPO_ROOT) if json_path.is_relative_to(REPO_ROOT) else json_path
    print(f"  wrote      {shown} (+ .csv, {csv_path.stat().st_size / 1e6:.1f} MB)")
    return sc, p, cfg, csv_path


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
            formatter_class=argparse.RawDescriptionHelpFormatter,
            epilog=__doc__.split("\n\n", 1)[1])
    parser.add_argument("names", nargs="*", help="scenario names or glob patterns (default: all)")
    parser.add_argument("--list", action="store_true", help="list the scenarios and exit")
    parser.add_argument("--scale", type=float, default=1.0,
            help="multiply particle counts (e.g. 0.1 for quick tests; few-body systems ignore it)")
    parser.add_argument("--seed", type=int, default=0, help="random seed (default: 0)")
    parser.add_argument("--out-dir", help=f"output directory (default: {SCENARIOS_DIR})")
    parser.add_argument("--verify", action="store_true",
            help="run the built simulator on each generated scenario at its timestep_max_s "
            "and check that tight orbits hold, energy is conserved, etc.")
    parser.add_argument("--verify-time", type=float,
            help="simulated seconds for --verify (default: per scenario)")
    args = parser.parse_args()

    if args.list:
        width = max(map(len, SCENARIOS))
        for name, (group, summary, _) in SCENARIOS.items():
            print(f"{name:<{width}}  {group:<10}  {summary}")
        return 0

    names = []
    for pattern in args.names or ["*"]:
        hits = [n for n in SCENARIOS if fnmatch.fnmatchcase(n, pattern)]
        if not hits:
            parser.error(f"no scenario matches '{pattern}' (see --list)")
        names += [n for n in hits if n not in names]

    failures = []
    for name in names:
        try:
            sc, p, cfg, csv_path = generate(name, args)
            if args.verify and not verify(sc, p, cfg, csv_path, args.verify_time):
                failures.append(name)
        except Exception as e:  # keep going, report at the end
            print(f"{name}: ERROR: {e}")
            failures.append(name)
    if failures:
        print(f"\n{len(failures)} scenario(s) failed: {', '.join(failures)}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
