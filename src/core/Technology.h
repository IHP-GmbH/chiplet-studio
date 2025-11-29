/**
 * Technology.h - Fabrication technology definition
 */

#ifndef CHIPLET_CORE_TECHNOLOGY_H
#define CHIPLET_CORE_TECHNOLOGY_H

#include <string>

namespace chiplet {

/**
 * Technology represents a fabrication technology with its
 * layer properties and database units.
 */
class Technology {
public:
    Technology(const std::string& id);
    ~Technology();

    const std::string& id() const;

    void setDescription(const std::string& desc);
    const std::string& description() const;

    void setLayerPropertiesPath(const std::string& path);
    const std::string& layerPropertiesPath() const;

    void setDbu(double dbu);
    double dbu() const;

private:
    std::string m_id;
    std::string m_description;
    std::string m_layerPropertiesPath;
    double m_dbu = 0.001;  // Default: 1nm
};

} // namespace chiplet

#endif // CHIPLET_CORE_TECHNOLOGY_H
