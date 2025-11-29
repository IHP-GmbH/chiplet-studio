/**
 * Interface.h - Physical connections between components
 */

#ifndef CHIPLET_CORE_INTERFACE_H
#define CHIPLET_CORE_INTERFACE_H

#include <string>

namespace chiplet {

/**
 * Interface types
 */
enum class InterfaceType {
    MicroBump,      // Die to interposer (fine pitch)
    CopperPillar,   // Interposer to substrate
    TSV,            // Through-silicon via
    WireBond        // Wire bonding
};

/**
 * Physical parameters for an interface (all in micrometers)
 */
struct InterfacePhysical {
    double pitch = 0.0;
    double diameter = 0.0;
    double height = 0.0;
};

/**
 * Connection endpoint
 */
struct InterfaceEndpoint {
    std::string component;
    std::string surface;    // "top" or "bottom"
    std::string portLayer;  // Layer in GDS defining pads
};

/**
 * Interface represents a physical connection between two components.
 */
class Interface {
public:
    Interface(const std::string& id, InterfaceType type);
    ~Interface();

    const std::string& id() const;
    InterfaceType type() const;

    void setFrom(const InterfaceEndpoint& from);
    const InterfaceEndpoint& from() const;

    void setTo(const InterfaceEndpoint& to);
    const InterfaceEndpoint& to() const;

    void setPhysical(const InterfacePhysical& physical);
    const InterfacePhysical& physical() const;

private:
    std::string m_id;
    InterfaceType m_type;
    InterfaceEndpoint m_from;
    InterfaceEndpoint m_to;
    InterfacePhysical m_physical;
};

} // namespace chiplet

#endif // CHIPLET_CORE_INTERFACE_H
