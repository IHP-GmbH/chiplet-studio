/**
 * Technology.h - Fabrication technology definition
 */

#ifndef CHIPLET_CORE_TECHNOLOGY_H
#define CHIPLET_CORE_TECHNOLOGY_H

#include <string>
#include <vector>

namespace chiplet {

/**
 * Validation result for technology resources.
 */
struct TechnologyValidation {
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
};

/**
 * Technology represents a fabrication technology with its
 * layer properties and database units.
 */
class Technology {
public:
    // Type definitions
    typedef std::string string_type;

    // Constructors and destructor
    Technology(const string_type& id);
    ~Technology();

    // Getters - identity
    const string_type& id() const;

    // Getters/Setters - properties
    void set_description(const string_type& desc);
    const string_type& description() const;

    void set_layer_properties_path(const string_type& path);
    const string_type& layer_properties_path() const;

    void set_dbu(double dbu);
    double dbu() const;

    // Validation methods
    /**
     * Check if the layer properties file exists.
     * @return true if path is empty or file exists
     */
    bool layer_properties_exist() const;

    /**
     * Validate the technology configuration.
     * Checks if referenced files exist.
     * @return Validation result with errors and warnings
     */
    TechnologyValidation validate() const;

    /**
     * Quick check if technology is valid (no errors).
     */
    bool is_valid() const;

private:
    string_type m_id;
    string_type m_description;
    string_type m_layerPropertiesPath;
    double m_dbu = 0.001;  // Default: 1nm
};

} // namespace chiplet

#endif // CHIPLET_CORE_TECHNOLOGY_H
