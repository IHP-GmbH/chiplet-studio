// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

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
#include "core/flow/FlowStep.h"
#include "core/flow/FlowEngine.h"
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

namespace {

// --- Lifetime-safe Python handles -------------------------------------------
//
// The previous bindings handed raw Assembly*/Component* to Python. Removing a
// component (Assembly::remove_component) destroys it while Python still holds
// the pointer, and any later attribute access is a use-after-free that crashes
// the whole process. We wrap Assembly and Component in handles that:
//   * keep the owning Assembly alive (shared_ptr) for Python-created/loaded
//     assemblies, and
//   * re-resolve the Component by ID on every access, raising a clean Python
//     exception instead of dereferencing freed memory.
// A "borrowed" handle (get_current_assembly) holds no ownership and is only
// valid while it still matches the active g_scriptAssembly.

struct AssemblyHandle {
    std::shared_ptr<Assembly> sp;   // owns the assembly (create/load)
    Assembly* borrowed = nullptr;   // non-owning (get_current_assembly)

    Assembly* get() const {
        if (sp) return sp.get();
        if (borrowed && borrowed == g_scriptAssembly) return borrowed;
        throw std::runtime_error(
            "Assembly is no longer valid (it was closed or replaced)");
    }
    Assembly& ref() const { return *get(); }
};

struct ComponentHandle {
    std::shared_ptr<Assembly> sp;   // keep owning assembly alive
    Assembly* borrowed = nullptr;
    std::string id;

    Assembly* assembly() const {
        if (sp) return sp.get();
        if (borrowed && borrowed == g_scriptAssembly) return borrowed;
        throw std::runtime_error("Component's assembly is no longer valid");
    }
    Component& ref() const {
        Component* c = assembly()->component(id);
        if (!c) {
            throw std::runtime_error("Component '" + id + "' no longer exists");
        }
        return *c;
    }
};

ComponentHandle makeComponentHandle(const AssemblyHandle& a, const std::string& id) {
    ComponentHandle h;
    h.sp = a.sp;
    h.borrowed = a.borrowed;
    h.id = id;
    return h;
}

} // namespace

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

    // Component class (handle that re-resolves by ID; see ComponentHandle)
    py::class_<ComponentHandle>(m, "Component")
        .def_property_readonly("id", [](const ComponentHandle& h) { return h.ref().id(); })
        .def_property("name",
            [](const ComponentHandle& h) { return h.ref().name(); },
            [](ComponentHandle& h, const std::string& name) { h.ref().set_name(name); })
        .def_property_readonly("type", [](const ComponentHandle& h) { return h.ref().type(); })
        .def_property_readonly("technology", [](const ComponentHandle& h) { return h.ref().technology(); })
        .def_property_readonly("layout_path", [](const ComponentHandle& h) { return h.ref().layout_path(); })
        .def_property_readonly("top_cell", [](const ComponentHandle& h) { return h.ref().top_cell(); })
        .def_property_readonly("position", [](const ComponentHandle& h) { return h.ref().position(); })
        .def_property_readonly("rotation", [](const ComponentHandle& h) { return h.ref().rotation(); })
        .def_property_readonly("dimensions", [](const ComponentHandle& h) { return h.ref().dimensions(); })
        .def_property_readonly("is_array", [](const ComponentHandle& h) { return h.ref().is_array(); })
        .def("metadata", [](const ComponentHandle& h, const std::string& key) { return h.ref().metadata(key); },
             py::arg("key"))
        .def("set_metadata", [](ComponentHandle& h, const std::string& key, const std::string& value) {
            h.ref().set_metadata(key, value);
        }, py::arg("key"), py::arg("value"))
        // Move method - direct modification (for standalone Python module)
        .def("move", [](ComponentHandle& h, double dx, double dy, double dz) {
            Component& c = h.ref();
            Position3D pos = c.position();
            pos.x += dx;
            pos.y += dy;
            pos.z += dz;
            c.set_position(pos);
        }, py::arg("dx"), py::arg("dy"), py::arg("dz"),
           "Move component by delta")
        .def("set_position", [](ComponentHandle& h, double x, double y, double z) {
            Position3D pos;
            pos.x = x;
            pos.y = y;
            pos.z = z;
            h.ref().set_position(pos);
        }, py::arg("x"), py::arg("y"), py::arg("z"),
           "Set absolute position")
        .def("set_technology", [](ComponentHandle& h, const std::string& techId) {
            h.ref().set_technology(techId);
        }, py::arg("tech_id"),
             "Set the technology ID for this component")
        .def("__repr__", [](const ComponentHandle& h) {
            const Component& c = h.ref();
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

    // Assembly class (AssemblyHandle: owns via shared_ptr, or borrows the active
    // GUI assembly). Component access returns ComponentHandle, never a raw
    // pointer, so removing a component can never dangle a Python reference.
    py::class_<AssemblyHandle>(m, "Assembly")
        .def(py::init([]() {
            AssemblyHandle h;
            h.sp = std::make_shared<Assembly>();
            return h;
        }))
        .def_property("name",
            [](const AssemblyHandle& h) { return h.ref().name(); },
            [](AssemblyHandle& h, const std::string& name) { h.ref().set_name(name); })
        .def_property("description",
            [](const AssemblyHandle& h) { return h.ref().description(); },
            [](AssemblyHandle& h, const std::string& desc) { h.ref().set_description(desc); })
        .def_property("author",
            [](const AssemblyHandle& h) { return h.ref().author(); },
            [](AssemblyHandle& h, const std::string& author) { h.ref().set_author(author); })
        .def_property_readonly("units", [](const AssemblyHandle& h) { return h.ref().units(); })
        // Component access
        .def("component", [](const AssemblyHandle& h, const std::string& id) -> py::object {
            if (!h.ref().has_component(id)) {
                return py::none();
            }
            return py::cast(makeComponentHandle(h, id));
        }, py::arg("id"), "Get component by ID (None if it does not exist)")
        .def("has_component", [](const AssemblyHandle& h, const std::string& id) {
            return h.ref().has_component(id);
        }, py::arg("id"))
        .def_property_readonly("component_count", [](const AssemblyHandle& h) {
            return h.ref().components().size();
        })
        .def("components", [](const AssemblyHandle& h) {
            std::vector<ComponentHandle> result;
            for (const auto& c : h.ref().components()) {
                result.push_back(makeComponentHandle(h, c->id()));
            }
            return result;
        }, "Get all components")
        // Interface access (returned objects are owned by the assembly; keep the
        // assembly handle alive for as long as Python keeps them)
        .def("interface", [](const AssemblyHandle& h, const std::string& id) {
            return h.ref().interface(id);
        }, py::return_value_policy::reference_internal, py::arg("id"), "Get interface by ID")
        .def("interfaces", [](const AssemblyHandle& h) {
            std::vector<Interface*> result;
            for (const auto& i : h.ref().interfaces()) {
                result.push_back(i.get());
            }
            return result;
        }, py::keep_alive<0, 1>(), "Get all interfaces")
        // Technology access
        .def("technology", [](const AssemblyHandle& h, const std::string& id) {
            return h.ref().technology(id);
        }, py::return_value_policy::reference_internal, py::arg("id"), "Get technology by ID")
        .def("technologies", [](const AssemblyHandle& h) {
            std::vector<Technology*> result;
            for (const auto& t : h.ref().technologies()) {
                result.push_back(t.get());
            }
            return result;
        }, py::keep_alive<0, 1>(), "Get all technologies")
        // Component creation
        .def("create_component", [](AssemblyHandle& h, const std::string& id,
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

            h.ref().add_component(std::move(comp));
            return makeComponentHandle(h, id);
        }, py::arg("id"), py::arg("type"), py::arg("width") = 1000.0,
           py::arg("height") = 1000.0, py::arg("thickness") = 100.0,
           "Create a new component and add it to the assembly")
        .def("remove_component", [](AssemblyHandle& h, const std::string& id) {
            return h.ref().remove_component(id);
        }, py::arg("id"), "Remove a component by ID")
        // Validation
        .def("is_valid", [](const AssemblyHandle& h) { return h.ref().is_valid(); })
        .def("__repr__", [](const AssemblyHandle& h) {
            const Assembly& a = h.ref();
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

    // StepStatus enum
    py::enum_<StepStatus>(m, "StepStatus")
        .value("Pending", StepStatus::Pending)
        .value("Running", StepStatus::Running)
        .value("Success", StepStatus::Success)
        .value("Error", StepStatus::Error)
        .value("Skipped", StepStatus::Skipped)
        .export_values();

    // FlowStep struct
    py::class_<FlowStep>(m, "FlowStep")
        .def(py::init<>())
        .def_readwrite("id", &FlowStep::id)
        .def_readwrite("name", &FlowStep::name)
        .def_readwrite("tool_path", &FlowStep::tool_path)
        .def_readwrite("interpreter", &FlowStep::interpreter)
        .def_readwrite("args", &FlowStep::args)
        .def_readwrite("input_files", &FlowStep::input_files)
        .def_readwrite("output_files", &FlowStep::output_files)
        .def_readwrite("depends_on", &FlowStep::depends_on)
        .def_property("status",
            [](const FlowStep& s) { return step_status_to_string(s.status); },
            [](FlowStep& s, const std::string& v) { s.status = step_status_from_string(v); })
        .def_property_readonly("exit_code", [](const FlowStep& s) { return s.exit_code; })
        .def_property_readonly("log", [](const FlowStep& s) { return s.log; })
        .def("__repr__", [](const FlowStep& s) {
            return "<FlowStep '" + s.id + "' status=" + step_status_to_string(s.status) + ">";
        });

    // FlowEngine class (QObject, non-copyable)
    py::class_<FlowEngine, std::unique_ptr<FlowEngine, py::nodelete>>(m, "FlowEngine")
        .def(py::init<>())
        .def("add_step", &FlowEngine::add_step, py::arg("step"))
        .def("remove_step", &FlowEngine::remove_step, py::arg("id"))
        .def("step", [](FlowEngine& e, const std::string& id) -> FlowStep {
            // Return a copy: FlowEngine::step() points into a std::vector that
            // add_step/remove_step reallocate, so a borrowed pointer would
            // dangle. Raise cleanly if the step does not exist.
            FlowStep* s = e.step(id);
            if (!s) {
                throw std::runtime_error("No flow step with id '" + id + "'");
            }
            return *s;
        }, py::arg("id"))
        .def("steps", [](const FlowEngine& e) { return e.steps(); })
        .def("step_count", &FlowEngine::step_count)
        .def("clear_steps", &FlowEngine::clear_steps)
        .def("run_step", &FlowEngine::run_step, py::arg("id"))
        .def("run_all", &FlowEngine::run_all)
        .def("cancel", &FlowEngine::cancel)
        .def("is_running", &FlowEngine::is_running)
        .def("topological_sort", &FlowEngine::topological_sort)
        .def("set_working_directory", &FlowEngine::set_working_directory, py::arg("dir"))
        .def("set_environment", &FlowEngine::set_environment,
             py::arg("key"), py::arg("value"))
        .def_property_readonly("working_directory",
            [](const FlowEngine& e) { return e.working_directory(); })
        .def("__repr__", [](const FlowEngine& e) {
            return "<FlowEngine with " + std::to_string(e.step_count()) + " steps>";
        });

    // ChipletFormat for loading/saving
    py::class_<ChipletFormat>(m, "ChipletFormat")
        .def(py::init<>())
        .def("load", [](ChipletFormat& fmt, const std::string& path) {
            AssemblyHandle h;
            h.sp = std::shared_ptr<Assembly>(fmt.load(path));
            return h;
        }, py::arg("path"), "Load assembly from .chiplet file")
        .def("save", [](ChipletFormat& fmt, const AssemblyHandle& h, const std::string& path) {
            fmt.save(h.ref(), path);
        }, py::arg("assembly"), py::arg("path"), "Save assembly to .chiplet file");

    // Global functions
    m.def("load_assembly", [](const std::string& path) {
        ChipletFormat format;
        AssemblyHandle h;
        h.sp = std::shared_ptr<Assembly>(format.load(path));
        return h;
    }, py::arg("path"), "Load an assembly from a .chiplet file");

    m.def("create_assembly", []() {
        AssemblyHandle h;
        h.sp = std::make_shared<Assembly>();
        return h;
    }, "Create a new empty assembly");

    m.def("save_assembly", [](const AssemblyHandle& h, const std::string& path) {
        ChipletFormat format;
        format.save(h.ref(), path);
    }, py::arg("assembly"), py::arg("path"), "Save an assembly to a .chiplet file");

    // Get the current assembly from the embedded GUI context.
    // Returns a borrowed handle: it stays valid only while it still matches the
    // active g_scriptAssembly, so it raises (instead of dangling) once the GUI
    // closes or replaces the assembly.
    m.def("get_current_assembly", []() {
        if (!g_scriptAssembly) {
            throw std::runtime_error("No active assembly - ensure ScriptEngine::set_assembly() was called");
        }
        AssemblyHandle h;
        h.borrowed = g_scriptAssembly;
        return h;
    }, "Get the currently active Assembly from the GUI context");

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
