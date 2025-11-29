/**
 * KLayoutBridge.h - Bridge to KLayout for 2D layout visualization
 */

#ifndef CHIPLET_VIEW2D_KLAYOUTBRIDGE_H
#define CHIPLET_VIEW2D_KLAYOUTBRIDGE_H

#include <string>
#include <memory>
#include <vector>

#ifdef HAVE_KLAYOUT
// Forward declarations for KLayout types
namespace db {
    class Layout;
    class Cell;
}
#endif

namespace chiplet {

/**
 * BoundingBox - Simple 3D bounding box (in micrometers)
 */
struct BoundingBox {
    double x_min = 0.0;
    double y_min = 0.0;
    double x_max = 0.0;
    double y_max = 0.0;

    double width() const { return x_max - x_min; }
    double height() const { return y_max - y_min; }
    bool is_valid() const { return x_max > x_min && y_max > y_min; }
};

/**
 * KLayoutBridge provides access to KLayout's layout database capabilities.
 * When HAVE_KLAYOUT is not defined, provides stub implementation.
 */
class KLayoutBridge {
public:
    // Type definitions
    typedef std::string string_type;

    // Constructors and destructor
    KLayoutBridge();
    ~KLayoutBridge();

    // Disable copy (owns KLayout resources)
    KLayoutBridge(const KLayoutBridge&) = delete;
    KLayoutBridge& operator=(const KLayoutBridge&) = delete;

    // Move semantics
    KLayoutBridge(KLayoutBridge&&) noexcept;
    KLayoutBridge& operator=(KLayoutBridge&&) noexcept;

    /**
     * Load a layout file (GDS/OASIS - auto-detected).
     * @param path Path to layout file
     * @return true on success, false on error
     */
    bool load_layout(const string_type& path);

    /**
     * Check if a layout is loaded.
     */
    bool is_loaded() const;

    /**
     * Get the file path of the loaded layout.
     */
    const string_type& path() const;

    /**
     * Get the detected format (e.g., "GDS2", "OASIS").
     */
    string_type format() const;

    /**
     * Set the top cell by name.
     * @param cell_name Name of the cell to use as top
     * @return true if cell found
     */
    bool set_top_cell(const string_type& cell_name);

    /**
     * Get the current top cell name.
     */
    const string_type& top_cell() const;

    /**
     * Get list of all cell names in the layout.
     */
    std::vector<string_type> cell_names() const;

    /**
     * Get the bounding box of the top cell (in micrometers).
     */
    BoundingBox bounding_box() const;

    /**
     * Get the database unit (in micrometers).
     */
    double dbu() const;

    /**
     * Get number of cells in layout.
     */
    size_t cell_count() const;

    /**
     * Get number of layers used.
     */
    size_t layer_count() const;

#ifdef HAVE_KLAYOUT
    /**
     * Access the underlying KLayout Layout object.
     * Only available when HAVE_KLAYOUT is defined.
     */
    db::Layout* layout();
    const db::Layout* layout() const;
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/**
 * Check if KLayout integration is available at compile time.
 */
bool klayout_available();

} // namespace chiplet

#endif // CHIPLET_VIEW2D_KLAYOUTBRIDGE_H
