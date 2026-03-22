/**
 * PyBindings.cpp - pybind11 bindings for chiplet_studio Python module
 *
 * Exposes the core Chiplet Studio API to Python scripts:
 * - Assembly, Component, Position3D, etc.
 * - Factory functions for creating components
 *
 * This is the standalone Python module for external scripts.
 * For embedded Python in the GUI, see ScriptEngine.cpp.
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/operators.h>

#include "core/Assembly.h"
#include "core/Component.h"
#include "core/Technology.h"
#include "core/Interface.h"
#include "formats/ChipletFormat.h"

namespace py = pybind11;
using namespace chiplet;

// Global assembly pointer for Python script access
// When used as standalone module: defined here (null, get_current_assembly() will throw)
// When embedded in GUI: defined in ScriptEngine.cpp and set via set_assembly()
namespace chiplet {
#ifdef CHIPLET_STANDALONE_PYMODULE
    Assembly* g_scriptAssembly = nullptr;
#else
    extern Assembly* g_scriptAssembly;
#endif
}

PYBIND11_MODULE(chiplet_studio, m) {
    m.doc() = "Chiplet Studio Python API for chiplet assembly design";

    // Position3D struct
    py::class_<Position3D>(m, "Position3D")
        .def(py::init<>())
        .def(py::init([](double x, double y, double z) {
            Position3D p;
            p.x = x;
            p.y = y;
            p.z = z;
            return p;
        }), py::arg("x") = 0.0, py::arg("y") = 0.0, py::arg("z") = 0.0)
        .def_readwrite("x", &Position3D::x)
        .def_readwrite("y", &Position3D::y)
        .def_readwrite("z", &Position3D::z)
        .def("__repr__", [](const Position3D& p) {
            return "Position3D(" + std::to_string(p.x) + ", " +
                   std::to_string(p.y) + ", " + std::to_string(p.z) + ")";
        });

    // Rotation3D struct
    py::class_<Rotation3D>(m, "Rotation3D")
        .def(py::init<>())
        .def(py::init([](double z) {
            Rotation3D r;
            r.z = z;
            return r;
        }), py::arg("z") = 0.0)
        .def_readwrite("z", &Rotation3D::z)
        .def("__repr__", [](const Rotation3D& r) {
            return "Rotation3D(z=" + std::to_string(r.z) + ")";
        });

    // Dimensions3D struct
    py::class_<Dimensions3D>(m, "Dimensions3D")
        .def(py::init<>())
        .def(py::init([](double w, double h, double t) {
            Dimensions3D d;
            d.width = w;
            d.height = h;
            d.thickness = t;
            return d;
        }), py::arg("width") = 0.0, py::arg("height") = 0.0, py::arg("thickness") = 0.0)
        .def_readwrite("width", &Dimensions3D::width)
        .def_readwrite("height", &Dimensions3D::height)
        .def_readwrite("thickness", &Dimensions3D::thickness)
        .def("__repr__", [](const Dimensions3D& d) {
            return "Dimensions3D(" + std::to_string(d.width) + " x " +
                   std::to_string(d.height) + " x " + std::to_string(d.thickness) + ")";
        });

    // ComponentType enum
    py::enum_<ComponentType>(m, "ComponentType")
        .value("Die", ComponentType::Die)
        .value("DieArray", ComponentType::DieArray)
        .value("Interposer", ComponentType::Interposer)
        .value("Substrate", ComponentType::Substrate)
        .export_values();

    // Component class
    py::class_<Component>(m, "Component")
        .def_property_readonly("id", &Component::id)
        .def_property("name",
            [](const Component& c) { return c.name(); },
            [](Component& c, const std::string& name) { c.set_name(name); })
        .def_property_readonly("type", &Component::type)
        .def_property_readonly("technology", &Component::technology)
        .def_property_readonly("layout_path", &Component::layout_path)
        .def_property_readonly("top_cell", &Component::top_cell)
        .def_property_readonly("position", &Component::position)
        .def_property_readonly("rotation", &Component::rotation)
        .def_property_readonly("dimensions", &Component::dimensions)
        .def_property_readonly("is_array", &Component::is_array)
        .def("metadata", &Component::metadata, py::arg("key"))
        .def("set_metadata", &Component::set_metadata, py::arg("key"), py::arg("value"))
        // Move method - direct modification (for standalone Python module)
        .def("move", [](Component& c, double dx, double dy, double dz) {
            Position3D pos = c.position();
            pos.x += dx;
            pos.y += dy;
            pos.z += dz;
            c.set_position(pos);
        }, py::arg("dx"), py::arg("dy"), py::arg("dz"),
           "Move component by delta")
        .def("set_position", [](Component& c, double x, double y, double z) {
            Position3D pos;
            pos.x = x;
            pos.y = y;
            pos.z = z;
            c.set_position(pos);
        }, py::arg("x"), py::arg("y"), py::arg("z"),
           "Set absolute position")
        .def("set_technology", &Component::set_technology, py::arg("tech_id"),
             "Set the technology ID for this component")
        .def("__repr__", [](const Component& c) {
            std::string typeStr;
            switch (c.type()) {
                case ComponentType::Die: typeStr = "Die"; break;
                case ComponentType::DieArray: typeStr = "DieArray"; break;
                case ComponentType::Interposer: typeStr = "Interposer"; break;
                case ComponentType::Substrate: typeStr = "Substrate"; break;
            }
            return "<Component '" + c.id() + "' type=" + typeStr + ">";
        });

    // Technology class
    py::class_<Technology>(m, "Technology")
        .def_property_readonly("id", &Technology::id)
        .def_property_readonly("description", &Technology::description)
        .def_property_readonly("layer_properties_path", &Technology::layer_properties_path)
        .def_property_readonly("dbu", &Technology::dbu)
        .def("__repr__", [](const Technology& t) {
            return "<Technology '" + t.id() + "'>";
        });

    // Assembly class (non-copyable due to unique_ptr members)
    // Note: We use unique_ptr with nodelete as holder because:
    // 1. Assembly contains unique_ptr members, making it non-copyable
    // 2. For borrowed assemblies (from get_current_assembly), we use reference policy
    // 3. For owned assemblies, Python code should use save_assembly() before discarding
    py::class_<Assembly, std::unique_ptr<Assembly, py::nodelete>>(m, "Assembly")
        .def(py::init<>())
        .def_property("name",
            [](const Assembly& a) { return a.name(); },
            [](Assembly& a, const std::string& name) { a.set_name(name); })
        .def_property("description",
            [](const Assembly& a) { return a.description(); },
            [](Assembly& a, const std::string& desc) { a.set_description(desc); })
        .def_property("author",
            [](const Assembly& a) { return a.author(); },
            [](Assembly& a, const std::string& author) { a.set_author(author); })
        .def_property_readonly("units", &Assembly::units)
        // Component access
        .def("component", &Assembly::component, py::return_value_policy::reference,
             py::arg("id"), "Get component by ID")
        .def("has_component", &Assembly::has_component, py::arg("id"))
        .def_property_readonly("component_count", [](const Assembly& a) {
            return a.components().size();
        })
        .def("components", [](Assembly& a) {
            std::vector<Component*> result;
            for (const auto& c : a.components()) {
                result.push_back(c.get());
            }
            return result;
        }, py::return_value_policy::reference, "Get all components")
        // Interface access
        .def("interface", &Assembly::interface, py::return_value_policy::reference,
             py::arg("id"), "Get interface by ID")
        .def("interfaces", [](Assembly& a) {
            std::vector<Interface*> result;
            for (const auto& i : a.interfaces()) {
                result.push_back(i.get());
            }
            return result;
        }, py::return_value_policy::reference, "Get all interfaces")
        // Technology access
        .def("technology", &Assembly::technology, py::return_value_policy::reference,
             py::arg("id"), "Get technology by ID")
        .def("technologies", [](Assembly& a) {
            std::vector<Technology*> result;
            for (const auto& t : a.technologies()) {
                result.push_back(t.get());
            }
            return result;
        }, py::return_value_policy::reference, "Get all technologies")
        // Component creation
        .def("create_component", [](Assembly& a, const std::string& id,
                                    const std::string& typeStr,
                                    double width, double height, double thickness) {
            ComponentType type = ComponentType::Die;
            if (typeStr == "Die") type = ComponentType::Die;
            else if (typeStr == "DieArray") type = ComponentType::DieArray;
            else if (typeStr == "Interposer") type = ComponentType::Interposer;
            else if (typeStr == "Substrate") type = ComponentType::Substrate;
            else throw std::invalid_argument("Invalid component type: " + typeStr);

            auto comp = std::make_unique<Component>(id, type);
            Dimensions3D dims;
            dims.width = width;
            dims.height = height;
            dims.thickness = thickness;
            comp->set_dimensions(dims);

            Component* ptr = comp.get();
            a.add_component(std::move(comp));
            return ptr;
        }, py::return_value_policy::reference,
           py::arg("id"), py::arg("type"), py::arg("width") = 1000.0,
           py::arg("height") = 1000.0, py::arg("thickness") = 100.0,
           "Create a new component and add it to the assembly")
        .def("remove_component", &Assembly::remove_component, py::arg("id"),
             "Remove a component by ID")
        // Validation
        .def("is_valid", &Assembly::is_valid)
        .def("__repr__", [](const Assembly& a) {
            return "<Assembly '" + a.name() + "' with " +
                   std::to_string(a.components().size()) + " components>";
        });

    // InterfaceType enum
    py::enum_<InterfaceType>(m, "InterfaceType")
        .value("MicroBump", InterfaceType::MicroBump)
        .value("CopperPillar", InterfaceType::CopperPillar)
        .value("TSV", InterfaceType::TSV)
        .value("WireBond", InterfaceType::WireBond)
        .export_values();

    // InterfaceEndpoint struct
    py::class_<InterfaceEndpoint>(m, "InterfaceEndpoint")
        .def(py::init<>())
        .def_readwrite("component", &InterfaceEndpoint::component)
        .def_readwrite("surface", &InterfaceEndpoint::surface)
        .def_readwrite("port_layer", &InterfaceEndpoint::portLayer)
        .def("__repr__", [](const InterfaceEndpoint& ep) {
            return "<InterfaceEndpoint " + ep.component + ":" + ep.surface + ">";
        });

    // InterfacePhysical struct
    py::class_<InterfacePhysical>(m, "InterfacePhysical")
        .def(py::init<>())
        .def_readwrite("pitch", &InterfacePhysical::pitch)
        .def_readwrite("diameter", &InterfacePhysical::diameter)
        .def_readwrite("height", &InterfacePhysical::height)
        .def("__repr__", [](const InterfacePhysical& p) {
            return "<InterfacePhysical pitch=" + std::to_string(p.pitch) +
                   " diameter=" + std::to_string(p.diameter) +
                   " height=" + std::to_string(p.height) + ">";
        });

    // Interface class
    py::class_<Interface>(m, "Interface")
        .def_property_readonly("id", &Interface::id)
        .def_property_readonly("type", &Interface::type)
        .def_property_readonly("from_endpoint", &Interface::from,
             py::return_value_policy::reference)
        .def_property_readonly("to_endpoint", &Interface::to,
             py::return_value_policy::reference)
        .def_property_readonly("physical", &Interface::physical,
             py::return_value_policy::reference)
        .def("__repr__", [](const Interface& i) {
            return "<Interface '" + i.id() + "'>";
        });

    // ChipletFormat for loading/saving
    py::class_<ChipletFormat>(m, "ChipletFormat")
        .def(py::init<>())
        .def("load", &ChipletFormat::load, py::arg("path"),
             "Load assembly from .chiplet file")
        .def("save", &ChipletFormat::save, py::arg("assembly"), py::arg("path"),
             "Save assembly to .chiplet file");

    // Global functions
    m.def("load_assembly", [](const std::string& path) {
        ChipletFormat format;
        return format.load(path);
    }, py::arg("path"), "Load an assembly from a .chiplet file");

    m.def("create_assembly", []() {
        return std::make_unique<Assembly>();
    }, "Create a new empty assembly");

    m.def("save_assembly", [](Assembly& assembly, const std::string& path) {
        ChipletFormat format;
        format.save(assembly, path);
    }, py::arg("assembly"), py::arg("path"), "Save an assembly to a .chiplet file");

    // Get the current assembly from the embedded GUI context
    // This allows Python scripts running inside Chiplet Studio to access
    // the same Assembly object that C++ is manipulating
    m.def("get_current_assembly", []() -> Assembly* {
        if (!g_scriptAssembly) {
            throw std::runtime_error("No active assembly - ensure ScriptEngine::set_assembly() was called");
        }
        return g_scriptAssembly;
    }, py::return_value_policy::reference,
       "Get the currently active Assembly from the GUI context");

    // Version info
    m.attr("__version__") = "0.1.0";
    m.attr("__doc__") = R"doc(
Chiplet Studio Python API

Example usage:
    import chiplet_studio as cs

    # Load an existing assembly
    asm = cs.load_assembly("my_design.chiplet")

    # Or create a new one
    asm = cs.create_assembly()
    asm.name = "My Design"

    # Create a new component
    die = asm.create_component("die_1", "Die", width=5000, height=5000, thickness=100)

    # Move it
    die.move(100, 200, 0)

    # List all components
    for comp in asm.components():
        print(f"{comp.id}: {comp.position}")

    # Save
    cs.save_assembly(asm, "output.chiplet")
)doc";
}
