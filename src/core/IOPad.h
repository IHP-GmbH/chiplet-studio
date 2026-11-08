// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

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

#include <optional>
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
 * 2D pad position in micrometers, in the canonical frame defined in
 * docs/coord_frame_contract.md in IHP-GmbH/chiplet-system-spec
 * (interposer-local, GDS-bbox-corner of the parent interposer's top_cell,
 * geometric center of the pad). io_pads inherit the interposer's frame and do
 * not declare an `anchor` field of their own.
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

    // The pad's declared io_class, or nullopt when the .chiplet omitted it.
    //
    // `io_class` and `layer` are OPTIONAL in the schema (io_pad.required is
    // exactly id and position), and an absent one is not the same statement as
    // a present one. Validation rule 8 is vacuous for a pad with no io_class
    // and skips a pad whose layer differs from the interface's port layer, so
    // filling either in turns "unspecified" into "declared" and makes the rule
    // bind on a document whose author never said so. That is why this is an
    // optional and not a defaulted field: the writer can only emit what the
    // author wrote, and a future consumer has to choose its own fallback out
    // loud rather than inherit one silently from here.
    const std::optional<IOClass>& io_class() const;
    void set_io_class(IOClass cls);

    const string_type& net() const;
    void set_net(const string_type& net);

    const position_type& position() const;
    void set_position(const position_type& pos);

    const size_type& size() const;
    void set_size(const size_type& sz);

    // The pad's declared layer, or nullopt when the .chiplet omitted it. See
    // the note on io_class(); the same reasoning applies, and the layer half
    // is the one rule 8 uses to decide whether a pad and an interface meet at
    // all.
    const std::optional<string_type>& layer() const;
    void set_layer(const string_type& layer);

private:
    string_type m_id;
    std::optional<IOClass> m_io_class;
    string_type m_net;
    position_type m_position;
    size_type m_size;
    std::optional<string_type> m_layer;
};

// String conversion helpers
std::string io_class_to_string(IOClass cls);
IOClass string_to_io_class(const std::string& s);

} // namespace chiplet

#endif // CHIPLET_CORE_IOPAD_H
