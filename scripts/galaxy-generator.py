# import numpy as np
# import matplotlib.pyplot as plt

# def generate_galaxy(n_disk=10000, n_bulge=3000, disk_radius=10.0, bulge_radius=3.0, 
#                    disk_height=0.5, mass_disk=1.0, mass_bulge=0.3, 
#                    G=1.0, seed=None):
#     """
#     Generate realistic initial conditions for a 2D galaxy simulation.
    
#     Parameters:
#     - n_disk: Number of disk particles
#     - n_bulge: Number of bulge particles
#     - disk_radius: Radius of the disk
#     - bulge_radius: Radius of the bulge
#     - disk_height: Scale height of the disk (for 2D, this affects vertical velocity)
#     - mass_disk: Total mass of the disk
#     - mass_bulge: Total mass of the bulge
#     - G: Gravitational constant
#     - seed: Random seed for reproducibility
    
#     Returns:
#     - positions: Array of shape (n_disk+n_bulge, 2)
#     - velocities: Array of shape (n_disk+n_bulge, 2)
#     - masses: Array of shape (n_disk+n_bulge,)
#     """
    
#     if seed is not None:
#         np.random.seed(seed)
    
#     # Initialize arrays
#     total_particles = n_disk + n_bulge
#     positions = np.zeros((total_particles, 2))
#     velocities = np.zeros((total_particles, 2))
#     masses = np.zeros(total_particles)
    
#     # Generate disk particles
#     # Radial distribution (exponential profile)
#     r_disk = disk_radius * np.random.exponential(scale=1.0, size=n_disk)
#     r_disk = np.clip(r_disk, 0, disk_radius*3)  # Cutoff at 3 scale lengths
    
#     # Angular distribution
#     theta_disk = 2 * np.pi * np.random.rand(n_disk)
    
#     # Convert to Cartesian coordinates
#     positions[:n_disk, 0] = r_disk * np.cos(theta_disk)
#     positions[:n_disk, 1] = r_disk * np.sin(theta_disk)
    
#     # Disk masses (uniform)
#     masses[:n_disk] = mass_disk / n_disk
    
#     # Generate bulge particles (Plummer sphere projected to 2D)
#     r_bulge = bulge_radius * np.sqrt(np.random.rand(n_bulge)**(-2/3) - 1)
#     r_bulge = np.clip(r_bulge, 0, bulge_radius*10)  # Cutoff at 10 scale radii
    
#     theta_bulge = 2 * np.pi * np.random.rand(n_bulge)
#     positions[n_disk:, 0] = r_bulge * np.cos(theta_bulge)
#     positions[n_disk:, 1] = r_bulge * np.sin(theta_bulge)
    
#     # Bulge masses (uniform)
#     masses[n_disk:] = mass_bulge / n_bulge
    
#     # Calculate circular velocities for disk particles
#     # Using a simple approximation for the combined potential
#     v_circ_disk = np.sqrt(G * (mass_disk * (1 - np.exp(-r_disk/disk_radius) * (1 + r_disk/disk_radius)) 
#                             + mass_bulge * r_disk**2 / (r_disk + bulge_radius)**3) / r_disk)
    
#     # Add random components to velocities (dispersion)
#     v_r_disp = 0.1 * v_circ_disk * np.random.randn(n_disk)
#     v_theta_disp = 0.1 * v_circ_disk * np.random.randn(n_disk)
    
#     # Convert to Cartesian velocities
#     velocities[:n_disk, 0] = -v_circ_disk * np.sin(theta_disk) + v_r_disp * np.cos(theta_disk) - v_theta_disp * np.sin(theta_disk)
#     velocities[:n_disk, 1] = v_circ_disk * np.cos(theta_disk) + v_r_disp * np.sin(theta_disk) + v_theta_disp * np.cos(theta_disk)
    
#     # Add vertical motion for disk (simulating 3D effects in 2D)
#     velocities[:n_disk, 0] += 0.05 * v_circ_disk * np.random.randn(n_disk)
#     velocities[:n_disk, 1] += 0.05 * v_circ_disk * np.random.randn(n_disk)
    
#     # Bulge velocities (isotropic dispersion)
#     v_disp_bulge = np.sqrt(G * mass_bulge / bulge_radius)  # Scale velocity
#     velocities[n_disk:, 0] = v_disp_bulge * np.random.randn(n_bulge)
#     velocities[n_disk:, 1] = v_disp_bulge * np.random.randn(n_bulge)
    
#     return positions, velocities, masses

# def plot_galaxy(positions, velocities, n_disk):
#     """Plot the galaxy initial conditions."""
#     plt.figure(figsize=(12, 5))
    
#     # Position plot
#     plt.subplot(121)
#     plt.scatter(positions[:n_disk, 0], positions[:n_disk, 1], s=1, c='b', label='Disk')
#     plt.scatter(positions[n_disk:, 0], positions[n_disk:, 1], s=1, c='r', label='Bulge')
#     plt.xlabel('x')
#     plt.ylabel('y')
#     plt.title('Positions')
#     plt.axis('equal')
#     plt.legend()
    
#     # Velocity plot
#     plt.subplot(122)
#     plt.quiver(positions[:n_disk, 0], positions[:n_disk, 1], 
#                velocities[:n_disk, 0], velocities[:n_disk, 1], 
#                color='b', scale=100, width=0.002, label='Disk')
#     plt.quiver(positions[n_disk:, 0], positions[n_disk:, 1], 
#                velocities[n_disk:, 0], velocities[n_disk:, 1], 
#                color='r', scale=100, width=0.002, label='Bulge')
#     plt.xlabel('x')
#     plt.ylabel('y')
#     plt.title('Velocities')
#     plt.axis('equal')
#     plt.legend()
    
#     plt.tight_layout()
#     plt.show()

# def save_to_file(positions, velocities, masses, filename='galaxy_initial_conditions.npy'):
#     """Save the initial conditions to a file."""
#     data = {
#         'positions': positions,
#         'velocities': velocities,
#         'masses': masses
#     }
#     np.save(filename, data)
#     print(f"Saved initial conditions to {filename}")

# # Generate and visualize the galaxy
# n_disk = 5000
# n_bulge = 2000
# positions, velocities, masses = generate_galaxy(n_disk=n_disk, n_bulge=n_bulge, seed=42)

# # Plot the initial conditions
# plot_galaxy(positions, velocities, n_disk)

# # Save to file
# save_to_file(positions, velocities, masses)

import numpy as np
import matplotlib.pyplot as plt
from scipy.stats import maxwell
import csv

def generate_galaxy(n_disk=10000, n_bulge=3000, disk_radius=3.0e20, bulge_radius=1.0e20, 
                   disk_height=0.5, mass_disk=1.0e41, mass_bulge=3.0e40, 
                   G=6.67430e-11, seed=None):
    """
    Generate realistic initial conditions for a 2D galaxy simulation in SI units.
    
    Parameters:
    - n_disk: Number of disk particles
    - n_bulge: Number of bulge particles
    - disk_radius: Radius of the disk in meters (~10 kpc)
    - bulge_radius: Radius of the bulge in meters (~3 kpc)
    - disk_height: Scale height of the disk (for 2D, this affects vertical velocity)
    - mass_disk: Total mass of the disk in kg (~5e10 solar masses)
    - mass_bulge: Total mass of the bulge in kg (~1.5e10 solar masses)
    - G: Gravitational constant in m^3 kg^-1 s^-2
    - seed: Random seed for reproducibility
    
    Returns:
    - positions: Array of shape (n_disk+n_bulge, 2) in meters
    - velocities: Array of shape (n_disk+n_bulge, 2) in m/s
    - masses: Array of shape (n_disk+n_bulge,) in kg
    """
    
    if seed is not None:
        np.random.seed(seed)
    
    # Initialize arrays
    total_particles = n_disk + n_bulge
    positions = np.zeros((total_particles, 2))
    velocities = np.zeros((total_particles, 2))
    masses = np.zeros(total_particles)
    
    # Generate disk particles
    # Radial distribution (exponential profile)
    r_disk = disk_radius * np.random.exponential(scale=1.0, size=n_disk)
    r_disk = np.clip(r_disk, 0, disk_radius*3)  # Cutoff at 3 scale lengths
    
    # Angular distribution
    theta_disk = 2 * np.pi * np.random.rand(n_disk)
    
    # Convert to Cartesian coordinates
    positions[:n_disk, 0] = r_disk * np.cos(theta_disk)
    positions[:n_disk, 1] = r_disk * np.sin(theta_disk)
    
    # Disk masses (uniform)
    masses[:n_disk] = mass_disk / n_disk
    
    # Generate bulge particles (Plummer sphere projected to 2D)
    r_bulge = bulge_radius * np.sqrt(np.random.rand(n_bulge)**(-2/3) - 1)
    r_bulge = np.clip(r_bulge, 0, bulge_radius*10)  # Cutoff at 10 scale radii
    
    theta_bulge = 2 * np.pi * np.random.rand(n_bulge)
    positions[n_disk:, 0] = r_bulge * np.cos(theta_bulge)
    positions[n_disk:, 1] = r_bulge * np.sin(theta_bulge)
    
    # Bulge masses (uniform)
    masses[n_disk:] = mass_bulge / n_bulge
    
    # Calculate circular velocities for disk particles
    # Using a simple approximation for the combined potential
    v_circ_disk = np.sqrt(G * (mass_disk * (1 - np.exp(-r_disk/disk_radius) * (1 + r_disk/disk_radius)) 
                            + mass_bulge * r_disk**2 / (r_disk + bulge_radius)**3) / r_disk)
    
    # Add random components to velocities (dispersion)
    v_r_disp = 0.1 * v_circ_disk * np.random.randn(n_disk)
    v_theta_disp = 0.1 * v_circ_disk * np.random.randn(n_disk)
    
    # Convert to Cartesian velocities
    velocities[:n_disk, 0] = -v_circ_disk * np.sin(theta_disk) + v_r_disp * np.cos(theta_disk) - v_theta_disp * np.sin(theta_disk)
    velocities[:n_disk, 1] = v_circ_disk * np.cos(theta_disk) + v_r_disp * np.sin(theta_disk) + v_theta_disp * np.cos(theta_disk)
    
    # Add vertical motion for disk (simulating 3D effects in 2D)
    velocities[:n_disk, 0] += 0.05 * v_circ_disk * np.random.randn(n_disk)
    velocities[:n_disk, 1] += 0.05 * v_circ_disk * np.random.randn(n_disk)
    
    # Bulge velocities (isotropic dispersion)
    v_disp_bulge = np.sqrt(G * mass_bulge / bulge_radius)  # Scale velocity
    velocities[n_disk:, 0] = v_disp_bulge * np.random.randn(n_bulge)
    velocities[n_disk:, 1] = v_disp_bulge * np.random.randn(n_bulge)
    
    return positions, velocities, masses

def save_to_csv(positions, velocities, masses, filename='galaxy_initial_conditions.csv'):
    """Save the initial conditions to a CSV file with ID,mass,x,y,vel_x,vel_y format."""
    with open(filename, 'w', newline='') as csvfile:
        writer = csv.writer(csvfile)
        
        # Write header
        writer.writerow(['ID', 'mass(kg)', 'x(m)', 'y(m)', 'vel_x(m/s)', 'vel_y(m/s)'])
        
        # Write data
        for i in range(len(masses)):
            writer.writerow([
                i+1,  # ID starting from 1
                masses[i],
                positions[i, 0],
                positions[i, 1],
                velocities[i, 0],
                velocities[i, 1]
            ])
    
    print(f"Saved initial conditions to {filename}")

# Generate the galaxy with realistic SI units
# Parameters adjusted for Milky Way-like galaxy:
# - Disk radius ~30 kpc (3e20 m)
# - Bulge radius ~10 kpc (1e20 m)
# - Disk mass ~5e10 solar masses (1e41 kg)
# - Bulge mass ~1.5e10 solar masses (3e40 kg)
n_disk = 1000
n_bulge = 400
positions, velocities, masses = generate_galaxy(
    n_disk=n_disk, 
    n_bulge=n_bulge, 
    disk_radius=3.0e20, 
    bulge_radius=1.0e20,
    mass_disk=1.0e41,
    mass_bulge=3.0e40,
    seed=42
)

# Save to CSV file
save_to_csv(positions, velocities, masses)

# Print some statistics for verification
print("\nSimulation Statistics:")
print(f"Total particles: {len(masses)}")
print(f"  Disk particles: {n_disk}")
print(f"  Bulge particles: {n_bulge}")
print(f"Total mass: {masses.sum():.2e} kg")
print(f"  Disk mass: {masses[:n_disk].sum():.2e} kg")
print(f"  Bulge mass: {masses[n_disk:].sum():.2e} kg")
print(f"Position range: x [{positions[:, 0].min():.2e}, {positions[:, 0].max():.2e}] m")
print(f"               y [{positions[:, 1].min():.2e}, {positions[:, 1].max():.2e}] m")
print(f"Velocity range: vx [{velocities[:, 0].min():.2e}, {velocities[:, 0].max():.2e}] m/s")
print(f"               vy [{velocities[:, 1].min():.2e}, {velocities[:, 1].max():.2e}] m/s")