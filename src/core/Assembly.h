/**
 * Assembly.h - Top-level container for chiplet package assembly
 */

#ifndef CHIPLET_CORE_ASSEMBLY_H
#define CHIPLET_CORE_ASSEMBLY_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "ComponentID.h"
#include "Component.h"
#include "Interface.h"
#include "Technology.h"

namespace chiplet {

/**
 * Validation result for assembly.
 */
struct AssemblyValidation {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    void add_error(const std::string& msg) {
        errors.push_back(msg);
        valid = false;
    }

    void add_warning(const std::string& msg) {
        warnings.push_back(msg);
    }

    void merge(const TechnologyValidation& tv, const std::string& context) {
        for (const auto& e : tv.errors) {
            add_error(context + ": " + e);
        }
        for (const auto& w : tv.warnings) {
            add_warning(context + ": " + w);
        }
    }
};

/**
 * Assembly represents a complete chiplet package containing
 * multiple components (dies, interposer, substrate) and their
 * interconnections.
 */
class Assembly {
public:
    // Type definitions
    typedef std::string string_type;
    typedef std::vector<std::unique_ptr<Component>> component_list_type;
    typedef std::vector<std::unique_ptr<Interface>> interface_list_type;
    typedef std::vector<std::unique_ptr<Technology>> technology_list_type;

    // Constructors and destructor
    Assembly();
    ~Assembly();

    // Non-copyable (contains unique_ptr members)
    Assembly(const Assembly&) = delete;
    Assembly& operator=(const Assembly&) = delete;

    // Move operations
    Assembly(Assembly&&) = default;
    Assembly& operator=(Assembly&&) = default;

    // Getters - metadata
    const string_type& name() const;
    const string_type& description() const;
    const string_type& author() const;
    const string_type& created() const;
    const string_type& modified() const;
    const string_type& units() const;

    // Setters - metadata
    void set_name(const string_type& name);
    void set_description(const string_type& desc);
    void set_author(const string_type& author);
    void set_created(const string_type& created);
    void set_modified(const string_type& modified);
    void set_units(const string_type& units);

    // Components
    void add_component(std::unique_ptr<Component> component);
    Component* component(const ComponentID& id) const;
    bool has_component(const ComponentID& id) const;
    bool remove_component(const ComponentID& id);
    const component_list_type& components() const;

    // Interfaces
    void add_interface(std::unique_ptr<Interface> iface);
    Interface* interface(const string_type& id) const;
    const interface_list_type& interfaces() const;

    // Technologies
    void add_technology(std::unique_ptr<Technology> tech);
    Technology* technology(const string_type& id) const;
    const technology_list_type& technologies() const;

    // Validation
    /**
     * Resolve a component's technology reference to a Technology object.
     * @param id ComponentID to resolve technology for
     * @return Pointer to Technology or nullptr if not found or component has no technology
     */
    Technology* resolve_component_technology(const ComponentID& id) const;

    /**
     * Validate the entire assembly.
     * Checks:
     * - All technologies are valid
     * - All component technology references resolve
     * - Component layout paths exist (if specified)
     * @return Validation result with errors and warnings
     */
    AssemblyValidation validate() const;

    /**
     * Quick check if assembly is valid.
     */
    bool is_valid() const;

private:
    string_type m_name;
    string_type m_description;
    string_type m_author;
    string_type m_created;
    string_type m_modified;
    string_type m_units = "um";  // Default to micrometers
    component_list_type m_components;
    std::unordered_map<ComponentID, Component*> m_component_index;  // O(1) lookup
    interface_list_type m_interfaces;
    technology_list_type m_technologies;
};

} // namespace chiplet

#endif // CHIPLET_CORE_ASSEMBLY_H
