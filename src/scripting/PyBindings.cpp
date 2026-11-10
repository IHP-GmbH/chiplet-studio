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
#include "formats/ChipletSynth.h"

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

// Interface and Technology are owned by the Assembly (unique_ptr lists), exactly
// like Component, so they need the same re-resolve-by-id discipline. Without it,
// asm.interface(id)/technology(id) dangle when the GUI reloads, and the plural
// getters previously returned Assembly-owned raw pointers that pybind would take
// ownership of and double-free on garbage collection.
struct InterfaceHandle {
    std::shared_ptr<Assembly> sp;
    Assembly* borrowed = nullptr;
    std::string id;

    Assembly* assembly() const {
        if (sp) return sp.get();
        if (borrowed && borrowed == g_scriptAssembly) return borrowed;
        throw std::runtime_error("Interface's assembly is no longer valid");
    }
    Interface& ref() const {
        Interface* i = assembly()->interface(id);
        if (!i) {
            throw std::runtime_error("Interface '" + id + "' no longer exists");
        }
        return *i;
    }
};

struct TechnologyHandle {
    std::shared_ptr<Assembly> sp;
    Assembly* borrowed = nullptr;
    std::string id;

    Assembly* assembly() const {
        if (sp) return sp.get();
        if (borrowed && borrowed == g_scriptAssembly) return borrowed;
        throw std::runtime_error("Technology's assembly is no longer valid");
    }
    // const: every Python-side Technology property is def_property_readonly,
    // so the binding never needed a mutable handle to begin with.
    const Technology& ref() const {
        const Technology* t = assembly()->technology(id);
        if (!t) {
            throw std::runtime_error("Technology '" + id + "' no longer exists");
        }
        return *t;
    }
};

InterfaceHandle makeInterfaceHandle(const AssemblyHandle& a, const std::string& id) {
    InterfaceHandle h;
    h.sp = a.sp;
    h.borrowed = a.borrowed;
    h.id = id;
    return h;
}

TechnologyHandle makeTechnologyHandle(const AssemblyHandle& a, const std::string& id) {
    TechnologyHandle h;
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
        // Read-only, canonical string, no setter. The cross-reader check
        // asserts the anchor an import writes through BOTH readers, and without
        // this the C++ side could not see the field at all while the PyYAML
        // side could. A string rather than the enum on purpose: the document
        // carries a string and that is what the other side compares against, so
        // an enum here would have the two sides asserting different things.
        .def_property_readonly("anchor", [](const ComponentHandle& h) {
            return anchor_to_string(h.ref().anchor());
        })
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

    // Technology class (handle that re-resolves by ID; see TechnologyHandle)
    py::class_<TechnologyHandle>(m, "Technology")
        .def_property_readonly("id", [](const TechnologyHandle& h) { return h.ref().id(); })
        .def_property_readonly("description", [](const TechnologyHandle& h) { return h.ref().description(); })
        .def_property_readonly("layer_properties_path", [](const TechnologyHandle& h) { return h.ref().layer_properties_path(); })
        .def_property_readonly("dbu", [](const TechnologyHandle& h) { return h.ref().dbu(); })
        .def("__repr__", [](const TechnologyHandle& h) {
            return "<Technology '" + h.ref().id() + "'>";
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
        // Interface access (handles re-resolve by ID; never hand out raw
        // assembly-owned pointers, so Python can neither dangle nor double-free)
        .def("interface", [](const AssemblyHandle& h, const std::string& id) -> py::object {
            if (!h.ref().interface(id)) {
                return py::none();
            }
            return py::cast(makeInterfaceHandle(h, id));
        }, py::arg("id"), "Get interface by ID (None if it does not exist)")
        .def("interfaces", [](const AssemblyHandle& h) {
            std::vector<InterfaceHandle> result;
            for (const auto& i : h.ref().interfaces()) {
                result.push_back(makeInterfaceHandle(h, i->id()));
            }
            return result;
        }, "Get all interfaces")
        // Technology access
        .def("technology", [](const AssemblyHandle& h, const std::string& id) -> py::object {
            if (!h.ref().technology(id)) {
                return py::none();
            }
            return py::cast(makeTechnologyHandle(h, id));
        }, py::arg("id"), "Get technology by ID (None if it does not exist)")
        .def("technologies", [](const AssemblyHandle& h) {
            std::vector<TechnologyHandle> result;
            for (const auto& t : h.ref().technologies()) {
                result.push_back(makeTechnologyHandle(h, t->id()));
            }
            return result;
        }, "Get all technologies")
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

    // InterfaceType enum: this studio's MAPPING of interfaces[].type, which is
    // not the same thing as the format's vocabulary. A document may legally
    // declare a type with no member here.
    py::enum_<InterfaceType>(m, "InterfaceType")
        .value("MicroBump", InterfaceType::MicroBump)
        .value("CopperPillar", InterfaceType::CopperPillar)
        .value("TSV", InterfaceType::TSV)
        .value("WireBond", InterfaceType::WireBond)
        .value("SolderBump", InterfaceType::SolderBump)
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

    // Interface class (handle that re-resolves by ID; endpoints/physical are
    // returned BY VALUE so a held copy never dangles into a freed Interface)
    py::class_<InterfaceHandle>(m, "Interface")
        .def_property_readonly("id", [](const InterfaceHandle& h) { return h.ref().id(); })
        // No `type` attribute, on purpose, and this is the Python half of the
        // same decision the C++ type makes by refusing operator== against the
        // enum. Binding `type` to std::optional<InterfaceType> would let
        // `iface.type == InterfaceType.MicroBump` keep working and quietly
        // answer False for a type this studio does not map, which is the bug
        // being removed. Making it RAISE instead was considered and rejected:
        // its trigger is a document that does not exist in the corpus today
        // (every interface type in the ecosystem is micro_bump, copper_pillar
        // or wire_bond, all mapped), so the rule would sit unexercised and
        // break somebody months from now instead of at the moment they write
        // the line. Removing the name breaks every call site immediately, which
        // is the property the shape was chosen for. Nothing in the ecosystem
        // reads this attribute today; measured before removing it.
        .def_property_readonly("type_name",
            [](const InterfaceHandle& h) { return h.ref().type().declared(); })
        .def_property_readonly("mapped_type",
            [](const InterfaceHandle& h) { return h.ref().type().mapped(); })
        .def_property_readonly("from_endpoint", [](const InterfaceHandle& h) { return h.ref().from(); })
        .def_property_readonly("to_endpoint", [](const InterfaceHandle& h) { return h.ref().to(); })
        .def_property_readonly("physical", [](const InterfaceHandle& h) { return h.ref().physical(); })
        .def("__repr__", [](const InterfaceHandle& h) {
            // The declared string, so a repr of an unmapped interface is
            // informative rather than a hole.
            return "<Interface '" + h.ref().id() + "' type='" +
                   h.ref().type().declared() + "'>";
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

    // FlowEngine class (QObject, non-copyable). Python constructs it with no Qt
    // parent via init<>(), so Python owns it: use the default unique_ptr holder
    // (py::nodelete would leak every Python-created engine).
    py::class_<FlowEngine>(m, "FlowEngine")
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

    // GDS import, bound so that the cross-reader check can drive the REAL
    // producer rather than something shaped like its output.
    //
    // Why it is exposed at all. tests/cross_reader_check.py is the only place in
    // this repository that reads a written .chiplet with a YAML implementation
    // other than the writer's own. Until this binding existed it could only read
    // a document it had itself loaded and re-saved, which establishes agreement
    // about a document RESEMBLING an import, not about what an import writes.
    // The alternatives were worse: a byte-pinned golden splits one claim across
    // two independently schedulable ctest entries, only one of which
    // (chiplet_cross_reader) is pinned in ci/gate_required_tests.txt, and it
    // asserts text where the proposition is about what a reader resolves the
    // text to.
    //
    // Second caller, and the one that keeps this signature honest:
    // src/ui/MainWindow.cpp:704.
    //
    // No py::arg default on any spec field, deliberately. thicknessUm defaults
    // to 200.0 in ChipletSynth.h; repeating that here would pin the BINDING's
    // number, so a later change to the spec or to the import dialog would leave
    // the check green on a value nothing produces any more. py::init<> means
    // every default comes from the C++ default constructor.
    py::class_<SingleGdsImportSpec>(m, "SingleGdsImportSpec")
        .def(py::init<>())
        .def_readwrite("gds_path", &SingleGdsImportSpec::gdsPath)
        .def_readwrite("top_cell", &SingleGdsImportSpec::topCell)
        .def_readwrite("width_um", &SingleGdsImportSpec::widthUm)
        .def_readwrite("height_um", &SingleGdsImportSpec::heightUm)
        .def_readwrite("thickness_um", &SingleGdsImportSpec::thicknessUm)
        .def_readwrite("tech_id", &SingleGdsImportSpec::techId)
        .def_readwrite("custom_lyp", &SingleGdsImportSpec::customLyp)
        .def_readwrite("custom_stackup", &SingleGdsImportSpec::customStackup)
        .def_readwrite("assembly_name", &SingleGdsImportSpec::assemblyName)
        .def_readwrite("component_id", &SingleGdsImportSpec::componentId);

    py::class_<ImportWriteResult> importWriteResult(m, "ImportWriteResult");
    py::enum_<ImportWriteResult::Outcome>(importWriteResult, "Outcome")
        .value("Written", ImportWriteResult::Outcome::Written)
        .value("WrittenToFallback", ImportWriteResult::Outcome::WrittenToFallback)
        .value("Refused", ImportWriteResult::Outcome::Refused)
        .value("Cancelled", ImportWriteResult::Outcome::Cancelled)
        .value("Failed", ImportWriteResult::Outcome::Failed);
    importWriteResult
        .def_readonly("outcome", &ImportWriteResult::outcome)
        .def_readonly("path", &ImportWriteResult::path)
        .def_readonly("message", &ImportWriteResult::message)
        .def("__repr__", [](const ImportWriteResult& r) {
            return "<ImportWriteResult " + r.path + ">";
        });

    // The outcome is part of the return value and not folded into an exception,
    // because a caller has to be able to tell Written from WrittenToFallback:
    // both carry a path, and only one of them is the path that was asked for.
    m.def("write_single_gds_import",
          [](const SingleGdsImportSpec& spec, const std::string& path) {
              // chooseAnotherName is left unset on purpose. An occupied
              // destination cancels rather than landing the document somewhere
              // the caller never named.
              return writeSingleGdsImport(spec, path, {});
          },
          py::arg("spec"), py::arg("path"),
          "Write a single-GDS import to `path`. The destination must be fresh; "
          "an occupied one cancels.");

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
