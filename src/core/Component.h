/**
 * Component.h - Base class for assembly components (Die, Interposer, Substrate)
 */

#ifndef CHIPLET_CORE_COMPONENT_H
#define CHIPLET_CORE_COMPONENT_H

#include <string>

namespace chiplet {

/**
 * 3D position in micrometers
 */
struct Position3D {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/**
 * Rotation in degrees (around Z axis for now)
 */
struct Rotation3D {
    double z = 0.0;
};

/**
 * Dimensions in micrometers
 */
struct Dimensions3D {
    double width = 0.0;
    double height = 0.0;
    double thickness = 0.0;
};

/**
 * Component types
 */
enum class ComponentType {
    Die,
    DieArray,
    Interposer,
    Substrate
};

/**
 * Component is the base class for all assembly elements.
 */
class Component {
public:
    Component(const std::string& id, ComponentType type);
    virtual ~Component();

    const std::string& id() const;
    ComponentType type() const;

    void setTechnology(const std::string& techId);
    const std::string& technology() const;

    void setLayoutPath(const std::string& path);
    const std::string& layoutPath() const;

    void setTopCell(const std::string& cell);
    const std::string& topCell() const;

    void setPosition(const Position3D& pos);
    const Position3D& position() const;

    void setRotation(const Rotation3D& rot);
    const Rotation3D& rotation() const;

    void setDimensions(const Dimensions3D& dims);
    const Dimensions3D& dimensions() const;

private:
    std::string m_id;
    ComponentType m_type;
    std::string m_technology;
    std::string m_layoutPath;
    std::string m_topCell;
    Position3D m_position;
    Rotation3D m_rotation;
    Dimensions3D m_dimensions;
};

} // namespace chiplet

#endif // CHIPLET_CORE_COMPONENT_H
