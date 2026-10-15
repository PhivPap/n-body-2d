# n-body-2d

A real-time 2D gravitational N-body simulator, written in C++20 and CUDA and drawn with SFML 3. It
handles anything from three stars on a figure-eight orbit to two galaxies of a million stars
merging. While it runs, you can pan, zoom, select and follow bodies, and speed time up or slow it
down.

<img width="2516" height="1379" alt="image" src="https://github.com/user-attachments/assets/b7ceffbb-125b-4a91-85f6-2d990c0b97b0" />

- **Three force algorithms:** exact all-pairs summation, a multithreaded CPU Barnes-Hut quadtree,
  and a GPU Barnes-Hut that runs every step on the graphics card. On an RTX 5070 Ti the GPU
  version simulates a million bodies at several hundred steps per second.
- **24 ready-made scenarios**, from the Solar System with its moons to Saturn's rings, colliding
  galaxies and a toy cosmic web. A generator script sets each one up in equilibrium and picks its
  algorithm, timestep and softening.
- Fading trails, box or click selection with a centre-of-mass marker, and a follow mode.
- Named bodies (planets, moons, stars, black holes) are labelled and drawn by mass. Hover any body
  for its name, mass and orbit; the inspector panel shows the selection, and a single body's
  orbital elements and predicted orbit.

## Requirements

- Linux on x86-64.
- CMake 3.24 or newer and a C++20 compiler (GCC or Clang).
- For the GPU algorithm: an NVIDIA GPU and the CUDA Toolkit 12 or newer. The CUDA code is compiled
  for the card that CMake finds (or for `-DNBODY_CUDA_ARCHITECTURES=...`). Without CUDA (or with
  `-DNBODY_CUDA=OFF`) the build leaves the GPU algorithm out.
- The development packages SFML needs on Linux: X11, Xrandr, Xcursor, Xi, udev and OpenGL (see
  [SFML's build guide](https://www.sfml-dev.org/tutorials/3.0/getting-started/build-from-source/)).
  SFML downloads its other dependencies, such as FreeType, the first time CMake runs.
- To generate more scenarios: Python 3.10 or newer with numpy and scipy.

## Build

```sh
git clone --recursive https://github.com/PhivPap/n-body-2d.git
cd n-body-2d
cmake -S . -B build
cmake --build build -j
```

If you cloned without `--recursive`, run `git submodule update --init --recursive` first. The
build is an optimized release build for your own CPU (`-march=native`) unless you ask for
another build type, e.g. `-DCMAKE_BUILD_TYPE=Debug`.

### Tests

```sh
ctest --test-dir build --output-on-failure   # after building, ~10 s
```

The tests check config parsing, CSV files, and the physics of every algorithm: a circular orbit
stays circular, the Barnes-Hut trees with `theta` 0 match All Pairs, pausing doesn't change the
result, and massless or coincident bodies are handled. They also check the viewer's orbital
elements (against the elements the Solar System was generated from), named bodies and labels.
The GPU algorithm's tests need a CUDA build (`-E gpu` skips them). If CMake finds Python 3 with
numpy and scipy, the `scenarios` tests also run the scenario generator's `--verify` on a few
scenarios.

## Run

```sh
./build/n-body-2d scenarios/solar-system.json
```

The repository includes one scenario: the Solar System, with the planets and 26 of their moons,
dwarf planets, large asteroids and Halley's comet. To look at a planet's moons, right-drag a box
around the planet, press F to follow it and zoom in with the mouse wheel.

- The simulation starts right away. Close the window (or press Ctrl+C in the terminal) to stop.
- With `Graphics.enabled` set to `false` in the config it runs without a window, logging its
  progress, until it reaches `iterations` or you press Ctrl+C.
- On exit, the final state of every body is written to the config's `universe_outfile` (here
  `scenarios/output.csv`, replaced on every run), in the same format as the input.
- Paths in a config are relative to the config file, so you can start the simulator from any
  directory.
- `--verbosity INFO` (or `WARNING`, `ERROR`) prints less; the default is `DEBUG`.

## Controls

| Input               | Action                                                     |
| ------------------- | ---------------------------------------------------------- |
| Space               | Pause or resume                                            |
| Left / Right        | Decrease / increase the timestep by 10%                    |
| Ctrl + Left / Right | Shorter / longer trails                                    |
| Up / Down           | Bigger / smaller bodies                                    |
| Mouse wheel         | Zoom                                                       |
| Left-drag           | Pan                                                        |
| Right-click         | Select the body under the cursor, or clear the selection   |
| Right-drag          | Select the bodies in a box                                 |
| S                   | Show or hide the selection                                 |
| D                   | Show or hide the selection's centre of mass                |
| F                   | Follow the selection                                       |
| C                   | Centre the view on the selection                           |
| O                   | Show or hide the selection's predicted orbit               |
| L                   | Show or hide the names of named bodies                     |
| M                   | Draw named bodies by mass, or all the same size            |
| G                   | Show or hide the grid                                      |
| F1 / F2 / F3 / F4 / F5 | Show or hide the commands, config, stats, message and inspector panels |

## Named bodies and the inspector

A body is *named* unless its id numbers it in a population: an id's group is the id without a
trailing number (and a `-` or `_` before it), and bodies in groups of at most 64 are named. So
`Io`, `Kepler-16b` and the 37 S-stars `S1` to `S37` are named, but not the 250,000 `ring-N`
particles of Saturn's rings. The 1024 heaviest named bodies are kept.

- Named bodies are drawn above the trails as markers 2 to 12 px wider than the other bodies, by
  mass, and labelled with their ids where there is room, heaviest first.
- Hovering over any body shows its id, mass and what it orbits (of several bodies under the
  cursor, the heaviest); right-clicking it selects it, and right-clicking empty space clears the
  selection.
- The inspector panel shows a single selected body's position, velocity and orbital elements
  around its attractor, and for several bodies their count, total mass, centre of mass and its
  velocity. The attractor is the body's primary, the heavier named body with the largest m/d³ (the
  Earth for the Moon, although the Sun pulls the Moon harder), merged with the named bodies that
  orbit that primary inside the body's distance: Nix orbits Pluto and Charon together, and
  Kepler-16b both of its suns.
- The predicted orbit is the conic of those elements, with its periapsis and apoapsis. Bodies
  selected together have none, since they may be far apart.
- While the simulation runs, velocities lag positions by half a step, so the elements carry a tiny
  error. They are exact while it's paused.

## Scenarios

Generate the other scenarios with [scripts/scenario-generator.py](scripts/scenario-generator.py):

```sh
pip install numpy scipy
python3 scripts/scenario-generator.py --list                   # list them
python3 scripts/scenario-generator.py                          # all of them (~4 min, 330 MB)
python3 scripts/scenario-generator.py saturn-rings 'galaxy-*'  # or just some
./build/n-body-2d scenarios/saturn-rings.json
```

With [uv](https://docs.astral.sh/uv/) installed, `uv run scripts/scenario-generator.py` installs
numpy and scipy by itself.

Each scenario is a pair of files in `scenarios/`: `<name>.csv` holds the bodies and `<name>.json`
is the config that runs them. The `Scenario` block in the config describes the scenario, gives
tips, and states `timestep_max_s`: the largest timestep at which its tightest orbits still hold.
You can speed a scenario up with the Right arrow until you reach it.

| Scenario                  | Bodies | What you see                                                         |
| ------------------------- | -----: | -------------------------------------------------------------------- |
| `three-body-figure-eight` |      3 | Three suns chasing each other on a figure-eight                      |
| `three-body-butterfly`, `-moth`, `-yin-yang`, `-dragonfly`, `-bumblebee` | 3 | Unstable periodic three-body orbits: they repeat for a few periods, then fall into chaos |
| `maxwell-ring-stable`     |     37 | 36 moonlets sharing one circular orbit, light enough to stay stable  |
| `maxwell-ring-unstable`   |     37 | The same ring with heavier moonlets, which breaks up into clumps     |
| `trappist-1`              |      8 | Seven Earth-sized planets in a resonant chain around a red dwarf     |
| `solar-system`            |     44 | The Sun, planets, 26 major moons, dwarf planets and Halley's comet  |
| `sgr-a-star`              |     38 | The S-stars whipping around the Milky Way's central black hole       |
| `kepler-16`               |   1.5k | A planet and a debris ring around a pair of suns (a real "Tatooine") |
| `saturn-rings`            |   250k | Saturn's rings with their gaps, shepherd moons and major moons       |
| `binary-black-hole`       |   100k | Two supermassive black holes clearing a cavity in a disk             |
| `solar-system-belts`      |    50k | The planets with the asteroid belt, Hildas, Trojans and Kuiper belt  |
| `stellar-flyby`           |    90k | A passing star tears through two debris disks                        |
| `protoplanetary-disk`     |   200k | A massive young disk around a star grows spiral arms                 |
| `galaxy-spiral`           |   300k | A disk galaxy that grows its own spiral arms                         |
| `galaxy-barred`           |   300k | A heavy disk that forms a central bar and two arms                   |
| `galaxy-whirlpool`        |   400k | A companion galaxy flies by and raises two grand-design arms (M51)   |
| `galaxy-collision`        |     1M | Two spiral galaxies merge and throw out long tidal tails (Antennae)  |
| `tidal-stream`            |   100k | A dwarf galaxy torn into long streams by a massive host              |
| `tidal-disruption`        |    50k | A star shredded by a black hole: half its debris escapes             |
| `cosmic-web`              |     1M | A toy 2D universe expands, grows filaments and clusters, then recollapses |

Generator options: `--scale 0.1` makes 10 times fewer particles for quick tests, `--seed N` gives
a different random realization, and `--verify` runs the simulator on each scenario at its
`timestep_max_s` and checks that its orbits hold and its energy is conserved.

Scenarios with more than 300 bodies use the GPU algorithm. On a build without CUDA, set their
`algorithm` to `"Barnes-Hut"` and `threads` to the number of threads your CPU runs.

## Configuration

A config is a JSON file with three sections. Every key below is required, except that the
softening takes one of two keys; keys the simulator doesn't know are ignored.

**`IO`**

| Key                | Meaning                                    |
| ------------------ | ------------------------------------------ |
| `universe_infile`  | CSV with the bodies to start from          |
| `universe_outfile` | CSV that the final state is written to on exit |
| `echo_config`      | Print the config at startup                |
| `echo_bodies`      | Print every body at startup                |

**`Simulation`**

| Key                | Meaning                                                                       |
| ------------------ | ----------------------------------------------------------------------------- |
| `algorithm`        | `"All Pairs"`, `"Barnes-Hut"` (CPU) or `"Barnes-Hut GPU"`, in any letter case |
| `timestep`         | Simulated seconds per step at startup                                         |
| `iterations`       | Stop and exit after this many steps                                           |
| `theta`            | Barnes-Hut opening angle, a cell's side over its distance: smaller is more accurate and slower |
| `softening_length_m` | Plummer softening length in metres (0 for none)                             |
| `softening_factor` | Or: the softening length as a fraction of the mean distance between bodies, from 0 (none) to 0.2 |
| `threads`          | Threads for the CPU Barnes-Hut (1 to 256), at most one per 400 bodies          |

**`Graphics`**

| Key                     | Meaning                                                    |
| ----------------------- | ---------------------------------------------------------- |
| `resolution`            | Window size in pixels, e.g. `[1600, 900]`                  |
| `fps`                   | Frame-rate limit                                           |
| `vsync`                 | Vertical sync                                              |
| `pixel_scale`           | Metres per pixel at startup, i.e. the initial zoom         |
| `show_grid`             | Show the grid at startup                                   |
| `show_commands_panel`, `show_config_panel`, `show_stats_panel`, `show_action_log_panel`, `show_inspector_panel` | Panels shown at startup |
| `panel_update_hz`       | How often the panels refresh                               |
| `selection`             | `show`, `show_center_of_mass`, `follow` and `show_orbit`: the selection settings at startup |
| `named_bodies`          | `show_labels` and `scale_by_mass`: how named bodies are drawn at startup |
| `trails_fade_time_s`    | Seconds a trail takes to fade (0 for no trails)            |
| `enabled`               | Open a window. `false` runs headless until `iterations` or Ctrl+C |

## Universe files

A universe is a CSV file with a header line, then one body per line: `id,mass,x,y,vel_x,vel_y`, in
SI units (kg, m, m/s). Columns are read by position, so the header names don't matter, and `id` is
any label. Ids must be unique and values finite. Masses can be zero: massless bodies feel gravity
but don't pull anything.

## Good to know

- **One timestep for everything.** Every body moves with the same fixed timestep, so the fastest
  orbit limits how large it can be (a few hundred steps per orbit). That's why each scenario stays
  at one scale: a planet with moons inside a galaxy would need billions of steps per galactic
  orbit.
- **Softening** is the Plummer length `softening_length_m`, or `softening_factor` × the mean
  distance between bodies, computed once at startup. It keeps close encounters in particle systems
  such as galaxies and disks from blowing up, but it also weakens any orbit closer than a few
  softening lengths, so planetary systems use none.
- **Playback speed** is steps per second × timestep, so it depends on your hardware. The generated
  timesteps are tuned for a Ryzen 9 9950X3D and an RTX 5070 Ti. On a slower machine a scenario
  plays slower, and you can use the Right arrow to speed it up, up to its `timestep_max_s`.

## License

MIT, see [LICENSE](LICENSE).
