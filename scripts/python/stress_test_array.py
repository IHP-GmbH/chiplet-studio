"""
Stress Test: Create 100 chiplets in a 10x10 grid.
Tests: Python API, Assembly management, Technology loading, GDS3D stackup.

Run from GUI Script Console or via test_integration_final.cpp
"""
import chiplet_studio as cs


def main():
    """Create a 10x10 grid of dies."""
    # Get current assembly from GUI context
    asm = cs.get_current_assembly()
    asm.name = "Python Stress Test"

    # Create 10x10 grid of dies
    for y in range(10):
        for x in range(10):
            comp_id = f"Die_{x}_{y}"

            # Create component with dimensions (um)
            die = asm.create_component(
                comp_id,
                "Die",
                width=1000.0,      # 1mm
                height=1000.0,     # 1mm
                thickness=50.0     # 50um
            )

            # Position in grid (2mm spacing)
            die.set_position(x * 2000.0, y * 2000.0, 0.0)

            # Set technology (triggers GDS3D stackup loading)
            die.set_technology("ihp-sg13g2")

    print(f"Created {asm.component_count} components")
    return True


if __name__ == "__main__":
    main()
