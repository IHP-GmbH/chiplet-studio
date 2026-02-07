#!/bin/bash
# run_python_tests.sh - Run Python binding tests
#
# Usage: ./tests/run_python_tests.sh
#        or from Docker: ./scripts/run-docker.sh tests/run_python_tests.sh
#
# Tests the chiplet_studio Python module:
# - Module import and version
# - Assembly creation and manipulation
# - Component creation, modification, and iteration
# - File save/load roundtrip

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Set Python path to find the module
export PYTHONPATH="${PROJECT_DIR}/build/python:${PYTHONPATH}"

echo "=== Chiplet Studio Python Binding Tests ==="
echo ""

python3 << 'EOF'
import sys
import os
import tempfile

print("Test 1: Module import and version...")
try:
    import chiplet_studio as cs
    print(f"  Module imported successfully")
    print(f"  Version: {cs.__version__}")
    assert cs.__version__ == "0.1.0", f"Unexpected version: {cs.__version__}"
    print("  PASS")
except Exception as e:
    print(f"  FAIL: {e}")
    sys.exit(1)

print("")
print("Test 2: Create new assembly...")
try:
    asm = cs.create_assembly()
    asm.name = "Test Assembly"
    asm.description = "Created by Python test"
    asm.author = "Test Runner"
    assert asm.name == "Test Assembly"
    assert asm.description == "Created by Python test"
    assert asm.author == "Test Runner"
    assert asm.component_count == 0
    print(f"  Created: {repr(asm)}")
    print("  PASS")
except Exception as e:
    print(f"  FAIL: {e}")
    sys.exit(1)

print("")
print("Test 3: Create components...")
try:
    # Create a Die
    die = asm.create_component("die_1", "Die", width=5000, height=5000, thickness=100)
    assert die.id == "die_1"
    assert die.type == cs.ComponentType.Die
    assert die.dimensions.width == 5000.0
    assert die.dimensions.height == 5000.0
    assert die.dimensions.thickness == 100.0
    print(f"  Created: {repr(die)}")

    # Create an Interposer
    interposer = asm.create_component("interposer_1", "Interposer", width=10000, height=10000, thickness=200)
    assert interposer.id == "interposer_1"
    assert interposer.type == cs.ComponentType.Interposer
    print(f"  Created: {repr(interposer)}")

    # Create a Substrate
    substrate = asm.create_component("substrate_1", "Substrate", width=20000, height=20000, thickness=500)
    assert substrate.id == "substrate_1"
    assert substrate.type == cs.ComponentType.Substrate
    print(f"  Created: {repr(substrate)}")

    assert asm.component_count == 3
    print("  PASS")
except Exception as e:
    print(f"  FAIL: {e}")
    sys.exit(1)

print("")
print("Test 4: Component lookup...")
try:
    found = asm.component("die_1")
    assert found is not None
    assert found.id == "die_1"
    assert asm.has_component("die_1")
    assert not asm.has_component("nonexistent")
    print("  PASS")
except Exception as e:
    print(f"  FAIL: {e}")
    sys.exit(1)

print("")
print("Test 5: Move components...")
try:
    die = asm.component("die_1")
    assert die.position.x == 0.0
    assert die.position.y == 0.0
    assert die.position.z == 0.0

    # Move by delta
    die.move(100, 200, 50)
    assert die.position.x == 100.0, f"Expected 100, got {die.position.x}"
    assert die.position.y == 200.0, f"Expected 200, got {die.position.y}"
    assert die.position.z == 50.0, f"Expected 50, got {die.position.z}"
    print(f"  After move: {die.position}")

    # Set absolute position
    die.set_position(500, 600, 100)
    assert die.position.x == 500.0
    assert die.position.y == 600.0
    assert die.position.z == 100.0
    print(f"  After set_position: {die.position}")
    print("  PASS")
except Exception as e:
    print(f"  FAIL: {e}")
    sys.exit(1)

print("")
print("Test 6: Component iteration...")
try:
    components = asm.components()
    assert len(components) == 3
    ids = [c.id for c in components]
    assert "die_1" in ids
    assert "interposer_1" in ids
    assert "substrate_1" in ids
    print(f"  Found {len(components)} components: {ids}")
    print("  PASS")
except Exception as e:
    print(f"  FAIL: {e}")
    sys.exit(1)

print("")
print("Test 7: Component metadata...")
try:
    die = asm.component("die_1")
    die.set_metadata("vendor", "Test Vendor")
    die.set_metadata("part_number", "TEST-001")
    assert die.metadata("vendor") == "Test Vendor"
    assert die.metadata("part_number") == "TEST-001"
    assert die.metadata("nonexistent") == ""  # Returns empty for missing keys
    print("  PASS")
except Exception as e:
    print(f"  FAIL: {e}")
    sys.exit(1)

print("")
print("Test 8: Save and load assembly...")
try:
    # Save to temp file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.chiplet', delete=False) as f:
        temp_path = f.name

    cs.save_assembly(asm, temp_path)
    print(f"  Saved to: {temp_path}")

    # Load back
    loaded = cs.load_assembly(temp_path)
    assert loaded.name == "Test Assembly"
    assert loaded.component_count == 3

    # Verify component data preserved
    die_loaded = loaded.component("die_1")
    assert die_loaded is not None
    assert die_loaded.position.x == 500.0
    assert die_loaded.position.y == 600.0
    print(f"  Loaded: {repr(loaded)}")

    # Clean up
    os.unlink(temp_path)
    print("  PASS")
except Exception as e:
    print(f"  FAIL: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

print("")
print("Test 9: Remove component...")
try:
    initial_count = asm.component_count
    result = asm.remove_component("die_1")
    assert result == True
    assert asm.component_count == initial_count - 1
    assert not asm.has_component("die_1")

    # Try to remove non-existent
    result = asm.remove_component("nonexistent")
    assert result == False
    print("  PASS")
except Exception as e:
    print(f"  FAIL: {e}")
    sys.exit(1)

print("")
print("Test 10: Position3D struct...")
try:
    p = cs.Position3D()
    assert p.x == 0.0
    assert p.y == 0.0
    assert p.z == 0.0

    p2 = cs.Position3D(10.0, 20.0, 30.0)
    assert p2.x == 10.0
    assert p2.y == 20.0
    assert p2.z == 30.0

    p2.x = 100.0
    assert p2.x == 100.0

    print(f"  Position: {repr(p2)}")
    print("  PASS")
except Exception as e:
    print(f"  FAIL: {e}")
    sys.exit(1)

print("")
print("=" * 50)
print("All 10 tests passed!")
print("=" * 50)
EOF

echo ""
echo "Python binding tests completed successfully!"
