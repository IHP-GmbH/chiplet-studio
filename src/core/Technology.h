/**
 * Technology.h - Fabrication technology definition
 */

#ifndef CHIPLET_CORE_TECHNOLOGY_H
#define CHIPLET_CORE_TECHNOLOGY_H

#include <string>
#include <vector>
#include <memory>
#include <array>

// Forward declaration (avoid including GDS3D headers in .h)
class GDSProcess;

namespace chiplet {

// Forward declaration
class LayerStackup;

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

    // GDS3D Process Definition (Techfile) support
    /**
     * Load a GDS3D process definition (techfile).
     * @param path Path to .txt techfile (e.g., pdks/ihp-sg13g2/techfile/sg13g2.txt)
     * @return true if loaded successfully
     */
    bool load_process_def(const std::string& path);

    /**
     * Check if process definition is loaded
     */
    bool has_process_def() const;

    /**
     * Get process definition path
     */
    const std::string& process_def_path() const;

    /**
     * Get layer Z position in micrometers.
     * @return Z bottom position (converted from nm to um), 0.0 if layer not found
     */
    double get_layer_z_um(int layer, int datatype) const;

    /**
     * Get layer thickness in micrometers.
     * @return Thickness (converted from nm to um), 1.0 if layer not found
     */
    double get_layer_thickness_um(int layer, int datatype) const;

    /**
     * Get layer color as RGB (0.0-1.0 range).
     * @return {r, g, b}, defaults to {0.5, 0.5, 0.5} if layer not found
     */
    std::array<float, 3> get_layer_color(int layer, int datatype) const;

    /**
     * Create a LayerStackup from the loaded process definition.
     * @return LayerStackup with all layers from techfile
     */
    LayerStackup createStackup() const;

private:
    string_type m_id;
    string_type m_description;
    string_type m_layerPropertiesPath;
    double m_dbu = 0.001;  // Default: 1nm

    // GDS3D process definition
    std::unique_ptr<GDSProcess> m_process;
    std::string m_processDefPath;
};

} // namespace chiplet

#endif // CHIPLET_CORE_TECHNOLOGY_H
