/**
 * IOPad.h - External I/O pad on a component (e.g. wire-bond pad on the interposer)
 *
 * The pad is a flat data class describing the geometry and electrical
 * association of one external pin. The IOClass field selects the
 * rendering branch in the GDS pipeline (wire_bond now; flipped_bump and
 * tsv_bump reserved for follow-up work).
 */

#ifndef CHIPLET_CORE_IOPAD_H
#define CHIPLET_CORE_IOPAD_H

#include <string>

namespace chiplet {

/**
 * Class of external I/O pad. Pivots downstream rendering and DRC.
 */
enum class IOClass {
    WireBond,
    FlippedBump,
    TSVBump
};

/**
 * 2D pad position in micrometers (interposer-global coordinates).
 *
 * Intentionally a separate type from Position3D so this header has no
 * dependency on Component.h (avoids a circular include with Component
 * which holds a vector<IOPad>).
 */
struct IOPadPosition {
    double x = 0.0;
    double y = 0.0;
};

/**
 * Pad geometric size in micrometers (axis-aligned rectangle).
 */
struct IOPadSize {
    double x = 0.0;
    double y = 0.0;
};

/**
 * IOPad represents a single external interface pad attached to a
 * Component (typically the interposer).
 */
class IOPad {
public:
    typedef std::string string_type;
    typedef IOPadPosition position_type;
    typedef IOPadSize size_type;

    IOPad();
    IOPad(const string_type& id, IOClass io_class);

    const string_type& id() const;
    void set_id(const string_type& id);

    IOClass io_class() const;
    void set_io_class(IOClass cls);

    const string_type& net() const;
    void set_net(const string_type& net);

    const position_type& position() const;
    void set_position(const position_type& pos);

    const size_type& size() const;
    void set_size(const size_type& sz);

    const string_type& layer() const;
    void set_layer(const string_type& layer);

private:
    string_type m_id;
    IOClass m_io_class = IOClass::WireBond;
    string_type m_net;
    position_type m_position;
    size_type m_size;
    string_type m_layer = "TopMetal2";
};

// String conversion helpers
std::string io_class_to_string(IOClass cls);
IOClass string_to_io_class(const std::string& s);

} // namespace chiplet

#endif // CHIPLET_CORE_IOPAD_H
