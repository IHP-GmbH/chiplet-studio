// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Netlist.cpp - Implementation of Net, Netlist data structures
 */

#include "Netlist.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>

namespace chiplet {

// --- Net implementation ---

Net::Net() = default;

Net::Net(const string_type& name, NetClass net_class)
    : m_name(name)
    , m_net_class(net_class)
{
}

Net::~Net() = default;

const Net::string_type& Net::name() const
{
    return m_name;
}

NetClass Net::net_class() const
{
    return m_net_class;
}

void Net::set_name(const string_type& name)
{
    m_name = name;
}

void Net::set_net_class(NetClass nc)
{
    m_net_class = nc;
}

void Net::add_connection(const NetConnection& conn)
{
    m_connections.push_back(conn);
}

void Net::add_connection(const std::string& component, const std::string& pin,
                         const std::string& layer)
{
    m_connections.push_back({component, pin, layer});
}

const Net::connection_list_type& Net::connections() const
{
    return m_connections;
}

size_t Net::connection_count() const
{
    return m_connections.size();
}

void Net::clear_connections()
{
    m_connections.clear();
}

bool Net::external() const
{
    return m_external;
}

void Net::set_external(bool e)
{
    m_external = e;
}

// --- Netlist implementation ---

Netlist::Netlist() = default;
Netlist::~Netlist() = default;

void Netlist::add_net(const Net& net)
{
    m_net_index[net.name()] = m_nets.size();
    m_nets.push_back(net);
}

void Netlist::add_net(Net&& net)
{
    m_net_index[net.name()] = m_nets.size();
    m_nets.push_back(std::move(net));
}

const Net* Netlist::net(const string_type& name) const
{
    auto it = m_net_index.find(name);
    if (it != m_net_index.end()) {
        return &m_nets[it->second];
    }
    return nullptr;
}

Net* Netlist::net(const string_type& name)
{
    auto it = m_net_index.find(name);
    if (it != m_net_index.end()) {
        return &m_nets[it->second];
    }
    return nullptr;
}

const Netlist::net_list_type& Netlist::nets() const
{
    return m_nets;
}

size_t Netlist::net_count() const
{
    return m_nets.size();
}

void Netlist::clear()
{
    m_nets.clear();
    m_net_index.clear();
}

std::vector<const Net*> Netlist::nets_by_class(NetClass nc) const
{
    std::vector<const Net*> result;
    for (const auto& net : m_nets) {
        if (net.net_class() == nc) {
            result.push_back(&net);
        }
    }
    return result;
}

size_t Netlist::count_by_class(NetClass nc) const
{
    return static_cast<size_t>(std::count_if(m_nets.begin(), m_nets.end(),
        [nc](const Net& n) { return n.net_class() == nc; }));
}

std::vector<const Net*> Netlist::nets_for_component(const string_type& component_id) const
{
    std::vector<const Net*> result;
    for (const auto& net : m_nets) {
        for (const auto& conn : net.connections()) {
            if (conn.component == component_id) {
                result.push_back(&net);
                break;  // Found at least one connection to this component
            }
        }
    }
    return result;
}

std::vector<const Net*> Netlist::nets_external() const
{
    std::vector<const Net*> result;
    for (const auto& net : m_nets) {
        if (net.external()) {
            result.push_back(&net);
        }
    }
    return result;
}

void Netlist::set_external_netlist_path(const string_type& path)
{
    m_external_netlist_path = path;
}

const Netlist::string_type& Netlist::external_netlist_path() const
{
    return m_external_netlist_path;
}

bool Netlist::import_csv(const string_type& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    // Skip header line
    if (!std::getline(file, line)) {
        return false;
    }

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string net_name, component, pin, layer, net_class_str;

        // Parse CSV: net_name,component,pin,layer,net_class
        if (!std::getline(ss, net_name, ',')) continue;
        if (!std::getline(ss, component, ',')) continue;
        if (!std::getline(ss, pin, ',')) continue;
        std::getline(ss, layer, ',');
        std::getline(ss, net_class_str, ',');

        // Trim whitespace
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(net_name);
        trim(component);
        trim(pin);
        trim(layer);
        trim(net_class_str);

        if (net_name.empty() || component.empty() || pin.empty()) continue;

        // Find or create net
        Net* existing = net(net_name);
        if (existing) {
            existing->add_connection(component, pin, layer);
            if (!net_class_str.empty()) {
                existing->set_net_class(string_to_net_class(net_class_str));
            }
        } else {
            NetClass nc = net_class_str.empty() ? NetClass::Signal : string_to_net_class(net_class_str);
            Net newNet(net_name, nc);
            newNet.add_connection(component, pin, layer);
            add_net(std::move(newNet));
        }
    }

    return true;
}

bool Netlist::export_csv(const string_type& path) const
{
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    // Write header
    file << "net_name,component,pin,layer,net_class\n";

    // Write each connection as a row
    for (const auto& net : m_nets) {
        for (const auto& conn : net.connections()) {
            file << net.name() << ","
                 << conn.component << ","
                 << conn.pin << ","
                 << conn.layer << ","
                 << net_class_to_string(net.net_class()) << "\n";
        }
    }

    return true;
}

int Netlist::map_pattern(const string_type& source_pattern,
                         const string_type& source_component,
                         const string_type& target_component,
                         const string_type& target_pattern)
{
    // Convert glob-style pattern to regex
    // [*] -> capture group for digits, {*} -> backreference
    std::string regex_str = source_pattern;

    // Escape regex special chars except our wildcards
    std::string escaped;
    for (size_t i = 0; i < regex_str.size(); ++i) {
        char c = regex_str[i];
        if (c == '[' && i + 2 < regex_str.size() && regex_str[i+1] == '*' && regex_str[i+2] == ']') {
            escaped += "(\\d+)";
            i += 2;
        } else if (c == '.' || c == '(' || c == ')' || c == '+' || c == '?' || c == '{' || c == '}') {
            escaped += '\\';
            escaped += c;
        } else {
            escaped += c;
        }
    }

    std::regex pattern(escaped);
    int mapped_count = 0;

    for (auto& net : m_nets) {
        std::smatch match;
        std::string name = net.name();
        if (std::regex_match(name, match, pattern)) {
            // Check if this net has a connection to source_component
            bool has_source = false;
            std::string source_pin;
            for (const auto& conn : net.connections()) {
                if (conn.component == source_component) {
                    has_source = true;
                    source_pin = conn.pin;
                    break;
                }
            }

            if (has_source) {
                // Build target pin name by substituting captured groups
                std::string target_pin = target_pattern;
                for (size_t g = 1; g < match.size(); ++g) {
                    std::string placeholder = "{*}";
                    auto pos = target_pin.find(placeholder);
                    if (pos != std::string::npos) {
                        target_pin.replace(pos, placeholder.size(), match[g].str());
                    }
                }

                // Add connection to target component
                net.add_connection(target_component, target_pin);
                ++mapped_count;
            }
        }
    }

    return mapped_count;
}

bool Netlist::empty() const
{
    return m_nets.empty() && m_external_netlist_path.empty();
}

void Netlist::rebuild_index()
{
    m_net_index.clear();
    for (size_t i = 0; i < m_nets.size(); ++i) {
        m_net_index[m_nets[i].name()] = i;
    }
}

// --- String conversion helpers ---

std::string net_class_to_string(NetClass nc)
{
    switch (nc) {
        case NetClass::Power:     return "power";
        case NetClass::Ground:    return "ground";
        case NetClass::Signal:    return "signal";
        case NetClass::DiffPair:  return "diff_pair";
        case NetClass::NC:        return "nc";
        case NetClass::Interface: return "interface";
    }
    return "signal";
}

NetClass string_to_net_class(const std::string& s)
{
    if (s == "power")     return NetClass::Power;
    if (s == "ground")    return NetClass::Ground;
    if (s == "signal")    return NetClass::Signal;
    if (s == "diff_pair") return NetClass::DiffPair;
    if (s == "nc")        return NetClass::NC;
    if (s == "interface") return NetClass::Interface;
    return NetClass::Signal;  // Default
}

} // namespace chiplet
