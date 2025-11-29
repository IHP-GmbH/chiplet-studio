/**
 * Assembly.h - Top-level container for chiplet package assembly
 */

#ifndef CHIPLET_CORE_ASSEMBLY_H
#define CHIPLET_CORE_ASSEMBLY_H

#include <string>
#include <vector>
#include <memory>
#include "Component.h"
#include "Interface.h"
#include "Technology.h"

namespace chiplet {

/**
 * Assembly represents a complete chiplet package containing
 * multiple components (dies, interposer, substrate) and their
 * interconnections.
 */
class Assembly {
public:
    Assembly();
    ~Assembly();

    // Metadata
    void setName(const std::string& name);
    const std::string& name() const;

    void setDescription(const std::string& desc);
    const std::string& description() const;

    // Components
    void addComponent(std::unique_ptr<Component> component);
    Component* component(const std::string& id) const;
    const std::vector<std::unique_ptr<Component>>& components() const;

    // Interfaces
    void addInterface(std::unique_ptr<Interface> iface);
    Interface* interface(const std::string& id) const;
    const std::vector<std::unique_ptr<Interface>>& interfaces() const;

    // Technologies
    void addTechnology(std::unique_ptr<Technology> tech);
    Technology* technology(const std::string& id) const;

private:
    std::string m_name;
    std::string m_description;
    std::vector<std::unique_ptr<Component>> m_components;
    std::vector<std::unique_ptr<Interface>> m_interfaces;
    std::vector<std::unique_ptr<Technology>> m_technologies;
};

} // namespace chiplet

#endif // CHIPLET_CORE_ASSEMBLY_H
