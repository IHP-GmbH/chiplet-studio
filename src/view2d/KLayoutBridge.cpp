/**
 * KLayoutBridge.cpp - Implementation
 */

#include "KLayoutBridge.h"
#include <iostream>

#ifdef HAVE_KLAYOUT
#include "dbLayout.h"
#include "dbReader.h"
#include "dbCell.h"
#include "tlStream.h"
#include "tlException.h"
#endif

namespace chiplet {

// Implementation structure (PIMPL pattern)
struct KLayoutBridge::Impl {
    string_type m_path;
    string_type m_format;
    string_type m_top_cell;

#ifdef HAVE_KLAYOUT
    std::unique_ptr<db::Layout> mp_layout;
#endif
};

// Constructor
KLayoutBridge::KLayoutBridge()
    : m_impl(std::make_unique<Impl>())
{
}

// Destructor
KLayoutBridge::~KLayoutBridge() = default;

// Move constructor
KLayoutBridge::KLayoutBridge(KLayoutBridge&&) noexcept = default;

// Move assignment
KLayoutBridge& KLayoutBridge::operator=(KLayoutBridge&&) noexcept = default;

#ifdef HAVE_KLAYOUT
// ============================================================================
// HAVE_KLAYOUT: Full implementation using KLayout libraries
// ============================================================================

bool KLayoutBridge::load_layout(const string_type& path)
{
    try {
        // Create input stream from file path
        tl::InputStream stream(path);

        // Create reader (auto-detects format)
        db::Reader reader(stream);

        // Create new layout
        m_impl->mp_layout = std::make_unique<db::Layout>();

        // Read the layout
        reader.read(*m_impl->mp_layout);

        // Store metadata
        m_impl->m_path = path;
        m_impl->m_format = reader.format();

        // Set top cell to first top-level cell if available
        auto top_it = m_impl->mp_layout->begin_top_down();
        auto top_end = m_impl->mp_layout->end_top_cells();
        if (top_it != top_end) {
            m_impl->m_top_cell = m_impl->mp_layout->cell_name(*top_it);
        }

        return true;

    } catch (const tl::Exception& e) {
        // KLayout exception - reset state and log error
        std::cerr << "KLayoutBridge::load_layout error: " << e.msg() << std::endl;
        m_impl->mp_layout.reset();
        m_impl->m_path.clear();
        m_impl->m_format.clear();
        m_impl->m_top_cell.clear();
        return false;
    } catch (const std::exception& e) {
        // Standard exception
        std::cerr << "KLayoutBridge::load_layout std error: " << e.what() << std::endl;
        m_impl->mp_layout.reset();
        m_impl->m_path.clear();
        m_impl->m_format.clear();
        m_impl->m_top_cell.clear();
        return false;
    }
}

bool KLayoutBridge::is_loaded() const
{
    return m_impl->mp_layout != nullptr;
}

const KLayoutBridge::string_type& KLayoutBridge::path() const
{
    return m_impl->m_path;
}

KLayoutBridge::string_type KLayoutBridge::format() const
{
    return m_impl->m_format;
}

bool KLayoutBridge::set_top_cell(const string_type& cell_name)
{
    if (!m_impl->mp_layout) {
        return false;
    }

    // Find cell by name
    auto cell_index = m_impl->mp_layout->cell_by_name(cell_name.c_str());
    if (!cell_index.first) {
        return false;  // Cell not found
    }

    m_impl->m_top_cell = cell_name;
    return true;
}

const KLayoutBridge::string_type& KLayoutBridge::top_cell() const
{
    return m_impl->m_top_cell;
}

std::vector<KLayoutBridge::string_type> KLayoutBridge::cell_names() const
{
    std::vector<string_type> names;

    if (m_impl->mp_layout) {
        // Iterate through all cell indices
        for (db::cell_index_type ci = 0; ci < m_impl->mp_layout->cells(); ++ci) {
            if (m_impl->mp_layout->is_valid_cell_index(ci)) {
                names.push_back(m_impl->mp_layout->cell_name(ci));
            }
        }
    }

    return names;
}

BoundingBox KLayoutBridge::bounding_box() const
{
    BoundingBox bbox;

    if (!m_impl->mp_layout || m_impl->m_top_cell.empty()) {
        return bbox;
    }

    auto cell_index = m_impl->mp_layout->cell_by_name(m_impl->m_top_cell.c_str());
    if (!cell_index.first) {
        return bbox;
    }

    const db::Cell& cell = m_impl->mp_layout->cell(cell_index.second);
    db::Box db_bbox = cell.bbox();

    if (!db_bbox.empty()) {
        double dbu = m_impl->mp_layout->dbu();
        bbox.x_min = db_bbox.left() * dbu;
        bbox.y_min = db_bbox.bottom() * dbu;
        bbox.x_max = db_bbox.right() * dbu;
        bbox.y_max = db_bbox.top() * dbu;
    }

    return bbox;
}

double KLayoutBridge::dbu() const
{
    if (m_impl->mp_layout) {
        return m_impl->mp_layout->dbu();
    }
    return 0.001;  // Default: 1nm
}

size_t KLayoutBridge::cell_count() const
{
    if (m_impl->mp_layout) {
        return m_impl->mp_layout->cells();
    }
    return 0;
}

size_t KLayoutBridge::layer_count() const
{
    if (m_impl->mp_layout) {
        return m_impl->mp_layout->layers();
    }
    return 0;
}

db::Layout* KLayoutBridge::layout()
{
    return m_impl->mp_layout.get();
}

const db::Layout* KLayoutBridge::layout() const
{
    return m_impl->mp_layout.get();
}

bool klayout_available()
{
    return true;
}

#else
// ============================================================================
// NO HAVE_KLAYOUT: Stub implementation
// ============================================================================

bool KLayoutBridge::load_layout(const string_type& /*path*/)
{
    return false;  // KLayout not available
}

bool KLayoutBridge::is_loaded() const
{
    return false;
}

const KLayoutBridge::string_type& KLayoutBridge::path() const
{
    return m_impl->m_path;
}

KLayoutBridge::string_type KLayoutBridge::format() const
{
    return "";
}

bool KLayoutBridge::set_top_cell(const string_type& /*cell_name*/)
{
    return false;
}

const KLayoutBridge::string_type& KLayoutBridge::top_cell() const
{
    return m_impl->m_top_cell;
}

std::vector<KLayoutBridge::string_type> KLayoutBridge::cell_names() const
{
    return {};
}

BoundingBox KLayoutBridge::bounding_box() const
{
    return BoundingBox();
}

double KLayoutBridge::dbu() const
{
    return 0.001;  // Default: 1nm
}

size_t KLayoutBridge::cell_count() const
{
    return 0;
}

size_t KLayoutBridge::layer_count() const
{
    return 0;
}

bool klayout_available()
{
    return false;
}

#endif // HAVE_KLAYOUT

} // namespace chiplet
