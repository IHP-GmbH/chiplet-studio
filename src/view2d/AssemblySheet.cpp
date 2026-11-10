// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AssemblySheet.h"

#include "core/Assembly.h"
#include "core/Component.h"
#include "core/TechResolve.h"
#include "core/Technology.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#ifdef HAVE_KLAYOUT
#include "dbCell.h"
#include "dbLayout.h"
#endif

namespace chiplet {

namespace {

const SheetTech* sheetOf(const std::map<std::string, SheetTech>& sheets, const std::string& tech)
{
    auto it = sheets.find(tech);
    return it == sheets.end() ? nullptr : &it->second;
}

bool names(const std::map<std::string, SheetTech>& sheets, const std::string& tech,
           const LayerNum& key)
{
    const SheetTech* s = sheetOf(sheets, tech);
    return s && s->names.count(key);
}

bool marksFill(const std::map<std::string, SheetTech>& sheets, const std::string& tech,
               const LayerNum& key)
{
    const SheetTech* s = sheetOf(sheets, tech);
    return s && s->fill.count(key);
}

SheetSpec sheetSpec(const Assembly& assembly, const std::string& techId)
{
    const Technology* tech = assembly.technology(techId);
    SheetSpec spec;
    spec.techId = techId;
    spec.lypPath = resolveTechnologySheet(tech, techId).path;
    for (const LayerKey& key : fillerKeys(resolveTechnologyStackup(tech, techId).stackup)) {
        spec.fill.insert(LayerNum(key.layer, key.datatype));
    }
    return spec;
}

void addSheet(LoadSpec& spec, const Assembly& assembly, const std::string& techId)
{
    if (techId.empty()) return;
    for (const auto& s : spec.sheets) {
        if (s.techId == techId) return;
    }
    spec.sheets.push_back(sheetSpec(assembly, techId));
}

std::string joined(const std::vector<std::string>& items)
{
    std::string out;
    for (const auto& item : items) {
        if (!out.empty()) out += ", ";
        out += item;
    }
    return out;
}

} // namespace

std::vector<ComposedGroup> composeAssemblySheet(
    const std::vector<LayerUse>& uses,
    const std::vector<std::string>& carrierTechs,
    const std::map<std::string, SheetTech>& sheets)
{
    std::map<std::string, std::vector<ComposedRow>> byGroup;

    for (const LayerUse& use : uses) {
        ComposedRow row;
        row.key = use.key;

        // The carrier side's owner is whichever carrier sheet names the layer,
        // else the interposer's technology.
        std::string carrierOwner;
        if (use.carrier) {
            for (const auto& tech : carrierTechs) {
                if (names(sheets, tech, use.key)) {
                    carrierOwner = tech;
                    break;
                }
            }
            if (carrierOwner.empty() && !carrierTechs.empty()) {
                carrierOwner = carrierTechs.front();
            }
            row.owners.push_back(carrierOwner.empty() ? "carrier" : carrierOwner);
        }
        for (const auto& tech : use.dieTechs) {
            if (std::find(row.owners.begin(), row.owners.end(), tech) == row.owners.end()) {
                row.owners.push_back(tech);
            }
        }

        // Style: the carrier sheet first, then each die technology's.
        if (use.carrier && names(sheets, carrierOwner, use.key)) {
            row.styleTech = carrierOwner;
        } else {
            for (const auto& tech : use.dieTechs) {
                if (names(sheets, tech, use.key)) {
                    row.styleTech = tech;
                    break;
                }
            }
        }

        // Fill if any technology with shapes on it says so. On the carrier
        // side every carrier technology counts, not only the one styling it.
        if (use.carrier) {
            for (const auto& tech : carrierTechs) {
                row.fill = row.fill || marksFill(sheets, tech, use.key);
            }
        }
        for (const auto& tech : use.dieTechs) {
            row.fill = row.fill || marksFill(sheets, tech, use.key);
        }

        bool sheetVisible = true;
        if (!row.styleTech.empty()) {
            const SheetTech& s = *sheetOf(sheets, row.styleTech);
            row.name = s.names.at(use.key);
            auto v = s.visible.find(use.key);
            sheetVisible = v == s.visible.end() || v->second;
        } else {
            row.name = "(default styling)";
        }
        row.visible = sheetVisible && !row.fill;
        if (row.fill) {
            row.name += " (fill)";
        }

        std::string group;
        if (row.owners.size() > 1) {
            group = kSharedGroup;
            row.name += " (shared: " + joined(row.owners) + ")";
        } else if (use.carrier) {
            group = carrierOwner.empty() ? kUnassignedGroup : carrierOwner;
        } else if (!use.dieTechs.empty()) {
            group = *use.dieTechs.begin();
        } else {
            continue;  // no shapes on either side: nothing to list
        }
        byGroup[group].push_back(std::move(row));
    }

    std::vector<ComposedGroup> out;
    auto take = [&](const std::string& title) {
        auto it = byGroup.find(title);
        if (it == byGroup.end()) return;
        ComposedGroup g{title, std::move(it->second)};
        std::sort(g.rows.begin(), g.rows.end(),
                  [](const ComposedRow& a, const ComposedRow& b) { return a.key < b.key; });
        out.push_back(std::move(g));
        byGroup.erase(it);
    };
    for (const auto& tech : carrierTechs) {
        take(tech);
    }
    take(kSharedGroup);
    std::vector<std::string> rest;
    for (const auto& entry : byGroup) {
        if (entry.first != kUnassignedGroup) rest.push_back(entry.first);
    }
    for (const auto& tech : rest) {  // std::map order: by id
        take(tech);
    }
    take(kUnassignedGroup);
    return out;
}

LoadSpec assemblyLoadSpec(const Assembly& assembly, bool carrierOnly)
{
    LoadSpec spec;
    for (const auto& comp : assembly.components()) {
        if (comp->type() == ComponentType::Interposer && !comp->technology().empty()) {
            spec.carrierTechs.push_back(comp->technology());
            break;
        }
    }
    const std::string& interconnect = assembly.interconnect_adapter();
    if (!interconnect.empty() && assembly.technology(interconnect)
        && std::find(spec.carrierTechs.begin(), spec.carrierTechs.end(), interconnect)
               == spec.carrierTechs.end()) {
        spec.carrierTechs.push_back(interconnect);
    }
    for (const auto& tech : spec.carrierTechs) {
        addSheet(spec, assembly, tech);
    }
    if (carrierOnly) {
        return spec;
    }
    for (const auto& comp : assembly.components()) {
        if (comp->type() == ComponentType::Interposer || comp->technology().empty()) continue;
        spec.dies.push_back({comp->id(), comp->technology()});
        addSheet(spec, assembly, comp->technology());
    }
    return spec;
}

std::string carrierFillSource(const Assembly& assembly, const std::string& shownPath)
{
    for (const auto& comp : assembly.components()) {
        if (comp->type() != ComponentType::Interposer || comp->layout_path().empty()) continue;
        const std::filesystem::path own(comp->layout_path());
        std::error_code ec;
        if (!std::filesystem::is_regular_file(own, ec)) return {};
        if (std::filesystem::equivalent(own, shownPath, ec)) return {};
        return comp->layout_path();
    }
    return {};
}

LoadSpec componentLoadSpec(const Assembly& assembly, const std::string& componentId)
{
    LoadSpec spec;
    const Component* comp = assembly.component(componentId);
    if (!comp || comp->technology().empty()) {
        return spec;
    }
    spec.carrierTechs.push_back(comp->technology());
    addSheet(spec, assembly, comp->technology());
    return spec;
}

#ifdef HAVE_KLAYOUT
Occupancy measureOccupancy(const db::Layout& layout, unsigned int topCell,
                           const std::vector<DiePrefix>& dies)
{
    Occupancy out;
    layout.update();

    std::map<db::cell_index_type, std::string> dieCellTech;
    std::set<std::string> matched;
    for (auto c = layout.begin(); c != layout.end(); ++c) {
        const std::string name = layout.cell_name(c->cell_index());
        for (const auto& die : dies) {
            if (name.rfind(die.componentId + "_", 0) == 0) {
                dieCellTech[c->cell_index()] = die.techId;
                matched.insert(die.componentId);
                break;
            }
        }
    }
    for (const auto& die : dies) {
        if (!matched.count(die.componentId)) {
            out.unmatchedDies.push_back(die.componentId);
        }
    }

    std::vector<std::pair<unsigned int, LayerNum>> layers;
    for (unsigned int li = 0; li < layout.layers(); ++li) {
        if (!layout.is_valid_layer(li)) continue;
        const db::LayerProperties& lp = layout.get_properties(li);
        if (lp.layer < 0 || lp.datatype < 0) continue;
        layers.emplace_back(li, LayerNum(lp.layer, lp.datatype));
    }

    std::map<LayerNum, LayerUse> acc;
    std::set<db::cell_index_type> seen;
    std::vector<db::cell_index_type> stack{topCell};
    while (!stack.empty()) {
        const db::cell_index_type ci = stack.back();
        stack.pop_back();
        if (!seen.insert(ci).second) continue;
        const db::Cell& cell = layout.cell(ci);

        auto die = dieCellTech.find(ci);
        if (die != dieCellTech.end()) {
            for (const auto& [li, key] : layers) {
                if (!cell.bbox(li).empty()) {
                    acc[key].dieTechs.insert(die->second);
                }
            }
            continue;  // its subtree is the die's, whatever else instantiates it
        }
        for (const auto& [li, key] : layers) {
            if (!cell.shapes(li).empty()) {
                acc[key].carrier = true;
            }
        }
        for (auto cc = cell.begin_child_cells(); !cc.at_end(); ++cc) {
            stack.push_back(*cc);
        }
    }

    for (auto& [key, use] : acc) {
        use.key = key;
        out.uses.push_back(std::move(use));
    }
    return out;
}
#endif

} // namespace chiplet
