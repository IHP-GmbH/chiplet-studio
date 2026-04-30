/**
 * IOPad.cpp - External I/O pad implementation
 */

#include "IOPad.h"
#include <stdexcept>

namespace chiplet {

IOPad::IOPad() = default;

IOPad::IOPad(const string_type& id, IOClass io_class)
    : m_id(id), m_io_class(io_class) {}

const IOPad::string_type& IOPad::id() const { return m_id; }
void IOPad::set_id(const string_type& id) { m_id = id; }

IOClass IOPad::io_class() const { return m_io_class; }
void IOPad::set_io_class(IOClass cls) { m_io_class = cls; }

const IOPad::string_type& IOPad::net() const { return m_net; }
void IOPad::set_net(const string_type& net) { m_net = net; }

const IOPad::position_type& IOPad::position() const { return m_position; }
void IOPad::set_position(const position_type& pos) { m_position = pos; }

const IOPad::size_type& IOPad::size() const { return m_size; }
void IOPad::set_size(const size_type& sz) { m_size = sz; }

const IOPad::string_type& IOPad::layer() const { return m_layer; }
void IOPad::set_layer(const string_type& layer) { m_layer = layer; }

std::string io_class_to_string(IOClass cls)
{
    switch (cls) {
        case IOClass::WireBond:    return "wire_bond";
        case IOClass::FlippedBump: return "flipped_bump";
        case IOClass::TSVBump:     return "tsv_bump";
    }
    return "wire_bond";
}

IOClass string_to_io_class(const std::string& s)
{
    if (s == "wire_bond")    return IOClass::WireBond;
    if (s == "flipped_bump") return IOClass::FlippedBump;
    if (s == "tsv_bump")     return IOClass::TSVBump;
    throw std::runtime_error("Unknown IOClass: " + s);
}

} // namespace chiplet
