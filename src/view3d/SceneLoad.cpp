// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SceneLoad.h"

#include "CoordFrame.h"
#include "GdsPlacement.h"
#include "MeshBuilder.h"
#include "TileChoice.h"
#include "core/Assembly.h"
#include "core/Component.h"
#include "core/GenericLayers.h"
#include "core/Technology.h"

#include <QDebug>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace chiplet {

LayerSheets loadLayerSheets(const Assembly& assembly)
{
    LayerSheets out;
    // Load .lyp files for each technology
    for (const auto& techPtr : assembly.technologies()) {
        const std::string& techId = techPtr->id();
        const std::string& lypPath = techPtr->layer_properties_path();
        if (!lypPath.empty()) {
            LayerPropertiesFile lyp;
            if (lyp.load(lypPath)) {
                size_t layerCount = lyp.layer_count();
                out.props[techId] = std::move(lyp);
                qDebug() << "Loaded" << layerCount << "layers from"
                         << QString::fromStdString(lypPath);
            } else {
                qWarning() << "Failed to load layer properties:"
                           << QString::fromStdString(lypPath)
                           << "-" << QString::fromStdString(lyp.error());
                // A missing file is already a technology note; this is the
                // file that is there and does not read.
                std::error_code ec;
                if (std::filesystem::is_regular_file(lypPath, ec)) {
                    out.notes.push_back(
                        techId + " layer sheet could not be read for 3D (" + lypPath
                        + ": " + lyp.error() + "). 3D uses generated colours.");
                }
            }
        }
    }
    return out;
}

const LayerPropertiesFile* findSheet(const std::map<std::string, LayerPropertiesFile>& sheets,
                                     const std::string& techId)
{
    if (techId.empty()) {
        return nullptr;
    }
    auto it = sheets.find(techId);
    return it == sheets.end() ? nullptr : &it->second;
}

RenderStackup resolveRenderStackup(const Assembly& assembly, const std::string& techId,
                                   const LayerPropertiesFile* lyp)
{
    // The ladder (stackup file, else the .lyp, else the generic interposer
    // stack) and, on the interposer's technology, the interconnect bodies
    // merged on the file's plane: drawnStackup, the resolver and merge that
    // seating uses, so a die's bumps are drawn where it seats.
    const DrawnStackup drawn = drawnStackup(&assembly, techId, lyp);
    RenderStackup out;
    out.stackup = drawn.stackup;
    out.modeled = drawn.modeled;
    out.label = stackupLabel(drawn.resolved, lyp != nullptr);
    qDebug() << "3D stackup for" << QString::fromStdString(techId) << "is"
             << QString::fromStdString(out.label) << "with" << out.stackup.layerCount()
             << "layers," << drawn.bodiesMerged << "of them interconnect bodies";
    return out;
}

const RenderStackup& cachedRenderStackup(std::map<std::string, RenderStackup>& cache,
                                         const Assembly& assembly, const std::string& techId,
                                         const LayerPropertiesFile* lyp)
{
    auto it = cache.find(techId);
    if (it == cache.end()) {
        it = cache.emplace(techId, resolveRenderStackup(assembly, techId, lyp)).first;
    }
    return it->second;
}

ExtractionConfig standardExtractionConfig(const LayerStackup& stackup)
{
    ExtractionConfig config;
    config.max_polygons_per_layer = 10000;  // Limit for performance
    config.min_polygon_area = 0.1;  // Skip tiny polygons (0.1 um^2)

    // Draw via/contact layers with their individual cuts, exempt from the area
    // cull above. A via cut array (interposer Via4 = 0.19um cuts = 0.036 um^2)
    // sits below min_polygon_area, so a normal extract would delete the whole
    // M4<->M5 connector while the larger TopVia1/TopVia2 cuts survive -- the
    // reported "M4 has no pillars to M5". The real cuts are drawn as-is (not
    // fused) so the view stays faithful to the physical via array. Two
    // mechanisms, both fed from the resolved stackup:
    //   via_layers    proactive: classify connectors by name (contains "via"
    //                 or "con" -- covers Via*/TopVia*/Cont/Contact/mcon/licon
    //                 across the bundled PDKs).
    //   modeled_layers backstop: every modeled layer. If min_polygon_area ever
    //                 empties one the name rule missed, the extractor draws its
    //                 cuts anyway, so no modeled connector can vanish whatever a
    //                 future PDK names it.
    for (const auto& elev : stackup.sortedLayers()) {
        config.modeled_layers.insert(elev.key());
        std::string ln = elev.name;
        std::transform(ln.begin(), ln.end(), ln.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ln.find("via") != std::string::npos ||
            ln.find("con") != std::string::npos) {
            config.via_layers.insert(elev.key());
        }
    }
    return config;
}

// Black-box / no-LYP chiplet: the fallback stackup does not model the die's
// own layers, so LayerMeshBuilder (which drops layers absent from the stackup)
// would render it empty. Augment a LOCAL copy of the stackup so Detailed mode
// shows a real box instead of a flat 1 um plane:
//   - outline (206/0) -> full component thickness, "outline" role (die body)
//   - pads    (205/0) -> a thin flat plane (decal) on the active face, "pad" role
// so the body height matches Transparent mode and the pads sit flush on the
// face (flipZ moves them to the bottom for flip-chip). Unknown commercial layers
// (no canonical 205/206) become full-thickness
// slabs. The role names drive the black-box colors (blue body / yellow pads;
// configs/stackups/colors/generic/blackbox.yaml, with a hardcoded fallback in
// LayerMeshBuilder).
void augmentStackupForBlackBox(LayerStackup& stackup,
                               const std::map<LayerKey, LayerPolygons>& polygons,
                               const Component& comp)
{
    double dieT = comp.dimensions().thickness;
    if (dieT <= 0.0) {
        dieT = 200.0;  // matches the Transparent-mode default thickness (um)
    }
    const LayerKey outlineKey(GenericLayers::OUTLINE_LAYER,
                              GenericLayers::OUTLINE_DATATYPE);
    const LayerKey padKey(GenericLayers::PAD_LAYER, GenericLayers::PAD_DATATYPE);
    const bool hasOutline = polygons.find(outlineKey) != polygons.end();
    // Pads render as a thin flat plane flush on the die's active face -- a decal,
    // not a raised cap (1 um on a ~200 um die reads as a plane). flipZ moves it to
    // the bottom face for flip-chip dies.
    const double padPlaneUm = 1.0;
    double slabTop = 0.0;
    for (const auto& entry : polygons) {
        const LayerKey& key = entry.first;
        if (stackup.find(key)) {
            continue;  // already modeled (real stackup layer) -- leave untouched
        }
        if (key == outlineKey) {
            stackup.addLayer(key.layer, key.datatype, 0.0, dieT,
                             GenericLayers::OUTLINE_ROLE);
        } else if (key == padKey) {
            const double z = hasOutline ? dieT : 0.0;  // flat plane on the active face, else be the body
            const double t = hasOutline ? padPlaneUm : dieT;
            stackup.addLayer(key.layer, key.datatype, z, t, GenericLayers::PAD_ROLE);
        } else {
            stackup.addLayer(key.layer, key.datatype, slabTop, dieT, "blackbox");
            slabTop += dieT;
        }
    }
}

QMatrix4x4 componentTransform(const Component& comp)
{
    // Apply component position as transform
    // Coordinate mapping: chiplet X -> 3D X, chiplet Y -> 3D Z, chiplet Z -> 3D Y
    QMatrix4x4 transform;
    transform.setToIdentity();
    const ScenePosition scenePos = sceneFromChiplet(comp.position());
    transform.translate(scenePos.x, scenePos.y, scenePos.z);
    // Flip-chip: mirror X for face-down dies
    if (comp.orientation() == Orientation::FaceDown) {
        transform.scale(-1.0f, 1.0f, 1.0f);
    }
    return transform;
}

std::map<QString, ComponentMesh> buildBoxMeshes(const Assembly& assembly,
                                                const std::map<std::string, LayerPropertiesFile>& sheets)
{
    std::map<QString, ComponentMesh> meshes;
    const auto& components = assembly.components();

    // Extract GDS bounding boxes for components with layouts.
    // This ensures box meshes match the actual GDS footprint in all
    // abstraction modes (Wireframe, Transparent, Solid).
    std::map<QString, GDSBoundingBox> gdsBounds;
    for (const auto& comp : components) {
        if (!comp || comp->layout_path().empty()) continue;
        GDSBoundingBox bbox = GDSLayerExtractor::extractBoundingBox(
            comp->layout_path(), comp->top_cell());
        if (bbox.is_valid()) {
            gdsBounds[QString::fromStdString(comp->id())] = bbox;
        }
    }

    // Build non-instanced box meshes for ALL components.
    // For components with GDS layouts, use GDS bounding box for dimensions
    // so that all abstraction levels show the correct footprint.
    for (const auto& comp : components) {
        if (!comp) continue;

        const LayerPropertiesFile* lyp = findSheet(sheets, comp->technology());

        QString compId = QString::fromStdString(comp->id());
        auto bboxIt = gdsBounds.find(compId);

        ComponentMesh mesh;
        if (bboxIt != gdsBounds.end()) {
            // GDS-derived box: match the layout footprint
            const GDSBoundingBox& bbox = bboxIt->second;
            const auto& pos = comp->position();
            auto dims = comp->dimensions();

            // Use GDS width/height, keep thickness from component
            if (dims.thickness <= 0) {
                dims.thickness = 200.0;  // Default die thickness
            }

            // Convert to mm (same as MeshBuilder)
            float w = static_cast<float>(bbox.width() / 1000.0);
            float d = static_cast<float>(dims.thickness / 1000.0);
            float h = static_cast<float>(bbox.height() / 1000.0);

            // Match the detailed path's actual GDS origin/centre and X mirror.
            // buildBox takes the +Z face, rather than the horizontal centre.
            const ScenePosition scenePos = gdsBoxCenter(
                bbox, pos, comp->anchor(), comp->orientation());
            float offsetX = scenePos.x;
            float offsetY = MeshBuilder::anchoredOffsetY(*comp, scenePos.y, d);  // stack-anchored elevation
            float offsetZ = scenePos.z + h / 2.0f;

            mesh = MeshBuilder::buildBox(w, d, h, offsetX, offsetY, offsetZ);
            mesh.setColor(MeshBuilder::colorForComponent(*comp, lyp));
        } else {
            // No GDS: use YAML dimensions (existing behavior)
            mesh = MeshBuilder::buildComponentMesh(*comp, lyp);
        }

        meshes.emplace(compId, std::move(mesh));
    }

    qDebug() << "Built" << meshes.size() << "box meshes ("
             << gdsBounds.size() << " from GDS extents)";
    return meshes;
}

StandardBuild buildStandardGeometry(const Component& comp,
                                    const std::map<LayerKey, LayerPolygons>& polygons,
                                    const LayerPropertiesFile* lyp,
                                    const RenderStackup& render)
{
    StandardBuild out;
    out.stackup = render.stackup;

    // Black-box / no-LYP chiplet (commercial / closed PDK node): the fallback
    // stackup does not model this die's layers, so LayerMeshBuilder would skip
    // them and the component would render empty in 3D. Augment a LOCAL copy of
    // the stackup (the cached one is untouched) so the die body + pads render
    // with sensible height and role-based colors. See augmentStackupForBlackBox.
    //
    // Only for a genuine black box: when a real PDK stackup was resolved
    // (render.modeled -- e.g. a supported-PDK import that carries no .lyp), the
    // die's real layers already render at correct elevations and unmodeled
    // auxiliary layers (fillers, text, recognition) must be skipped, NOT turned
    // into stacked full-thickness slabs that explode the stack vertically.
    if (!lyp && !render.modeled) {
        augmentStackupForBlackBox(out.stackup, polygons, comp);
    }

    // Layer color comes from the technology's .lyp (the single color source for
    // supported PDKs; resolved per layer in LayerMeshBuilder). The only scheme
    // we still load is the generic black-box fallback for a no-.lyp die: it
    // ships the blue body + yellow pads keyed by the canonical outline/pad
    // roles (user-editable). LayerMeshBuilder also carries a hardcoded
    // blue/yellow fallback if this file is missing.
    if (!lyp) {
        std::string gp = BlenderGDSConfigs::genericColorSchemePath();
        LayerColorScheme scheme;
        if (!gp.empty() && scheme.loadFromYAML(gp)) {
            out.colorScheme = std::move(scheme);
        }
    }

    // Build 3D geometry from polygons
    // For flip-chip (FaceDown) dies, invert layer z-positions so that
    // the topmost metal (e.g. TopMetal2) sits at z=0 and lower metals
    // stack upward, matching the physical face-down orientation.
    out.flipZ = (comp.orientation() == Orientation::FaceDown);
    out.beolTop = out.flipZ ? out.stackup.totalHeight() : 0.0;
    // Mesh anchor convention is now schema-driven (per
    // coord_frame_contract.md §2): each component declares
    // `anchor: gds_origin` or `anchor: bbox_center`. Legacy files
    // without the field default to BboxCenter (the parser warns).
    LayerMeshBuilder meshBuilder;
    out.geometry = meshBuilder.build(
        polygons, out.stackup, lyp,
        out.colorScheme ? &*out.colorScheme : nullptr,
        1.0, out.flipZ, out.beolTop, comp.anchor());
    out.geometry.componentId = QString::fromStdString(comp.id());
    return out;
}

std::optional<StandardComponent> loadStandardComponent(const Component& comp,
                                                       const LayerPropertiesFile* lyp,
                                                       const RenderStackup& render,
                                                       double filterPercent)
{
#ifdef HAVE_KLAYOUT
    const QString compId = QString::fromStdString(comp.id());
    const std::string& layoutPath = comp.layout_path();
    if (layoutPath.empty()) {
        qDebug() << "Component" << compId << "has no layout path, using box mode";
        return std::nullopt;
    }

    // Extract the layer geometry directly from the GDS file.
    qDebug() << "Building layer geometry for" << compId << "from" << QString::fromStdString(layoutPath);

    GDSLayerExtractor extractor;
    const ExtractionConfig config = standardExtractionConfig(render.stackup);
    extractor.setConfig(config);

    // Pass the component's top_cell to extract from the correct cell
    auto polygons = extractor.extractFromFile(layoutPath, comp.top_cell());

    if (polygons.empty()) {
        qWarning() << "No polygons extracted from" << QString::fromStdString(layoutPath)
                   << "(top_cell:" << QString::fromStdString(comp.top_cell()) << ")";
        return std::nullopt;
    }

    qDebug() << "Extracted" << extractor.lastLayerCount() << "layers,"
             << extractor.lastPolygonCount() << "polygons from" << compId;

    StandardComponent out;
    // Kept for shape filtering (avoids re-extracting from GDS)
    out.polygons = polygons;
    out.areaStats = ShapeFilter::computeStatistics(polygons);
    out.tally = extractor.lastTally();

    // Apply the component's shape filter (if the user previously set one, e.g.
    // .chiplet reload preserves the slider value).
    if (filterPercent > 0.0 && out.areaStats.total_polygons > 0) {
        double threshold = ShapeFilter::thresholdFromPercentage(filterPercent, out.areaStats);
        polygons = ShapeFilter::filter(polygons, threshold);
    }

    StandardBuild built = buildStandardGeometry(comp, polygons, lyp, render);

    // Never a silent skip: name the layers the stackup places nowhere.
    out.note = unmodeledNote(comp.id(), built.geometry.unmodeled, render.label);
    // Nor a silent limit: for each placed layer, what 3D draws against what
    // the GDS holds, counted before any shape filter the user sets.
    out.counts = shapeCounts(out.tally, config, built.stackup, LayerMeshBuilder());

    built.geometry.transform = componentTransform(comp);

    // What the fast view reads: the GDS layers placed here, each where and in
    // the colour the meshes above have it.
    out.fast.componentId = comp.id();
    out.fast.layoutPath = layoutPath;
    out.fast.topCell = comp.top_cell();
    const bool proceduralSubstrate = built.stackup.find(40, 0) != nullptr;
    for (const auto& [key, t] : out.tally) {
        if (proceduralSubstrate && key == LayerKey(40, 0)) {
            continue;
        }
        LayerPlacement place = LayerMeshBuilder::placeLayer(
            key, built.stackup, lyp, built.colorScheme ? &*built.colorScheme : nullptr,
            built.flipZ, built.beolTop);
        if (place.placed) {
            out.fast.layers.emplace_back(key, std::move(place));
        }
    }

    out.stackup = std::move(built.stackup);
    out.geometry = std::move(built.geometry);
    return out;
#else
    Q_UNUSED(comp);
    Q_UNUSED(lyp);
    Q_UNUSED(render);
    Q_UNUSED(filterPercent);
    return std::nullopt;
#endif
}

StandardScene loadStandardScene(const Assembly& assembly,
                                const std::map<std::string, LayerPropertiesFile>& sheets,
                                const std::map<QString, double>& filterPercent)
{
    StandardScene scene;
    const auto& components = assembly.components();
    if (components.empty()) {
        return scene;
    }

    scene.boxes = buildBoxMeshes(assembly, sheets);

    // Layer geometry for every component (the per-component render mode
    // then decides what is actually drawn).
    for (const auto& comp : components) {
        if (!comp) continue;
#ifdef HAVE_KLAYOUT
        const QString compId = QString::fromStdString(comp->id());
        if (comp->layout_path().empty()) {
            qDebug() << "Component" << compId << "has no layout path, using box mode";
            continue;
        }
        const std::string& techId = comp->technology();
        const LayerPropertiesFile* lyp = findSheet(sheets, techId);
        const RenderStackup& render = cachedRenderStackup(scene.stackups, assembly, techId, lyp);
        auto filter = filterPercent.find(compId);
        auto loaded = loadStandardComponent(*comp, lyp, render,
                                            filter == filterPercent.end() ? 0.0 : filter->second);
        if (loaded) {
            scene.components.emplace(compId, std::move(*loaded));
        }
#endif
    }

    if (!scene.components.empty()) {
        qDebug() << "Built layer geometry for" << scene.components.size() << "components";
    }
    return scene;
}

void SceneBounds::add(const Component3DGeometry& geometry)
{
    for (const auto& layer : geometry.layers) {
        // Every corner, not only the translation: the face-down mirror
        // reverses extrema.
        const AA_BOUNDING_BOX world = transformedMeshBounds(layer.mesh.boundingBox(),
                                                            geometry.transform);
        if (m_empty) {
            m_box = world;
            m_empty = false;
        } else {
            m_box.AddBounds(world);
        }
    }
}

void SceneBounds::add(const ComponentMesh& mesh)
{
    if (m_empty) {
        m_box = mesh.boundingBox();
        m_empty = false;
    } else {
        m_box.AddBounds(mesh.boundingBox());
    }
}

AA_BOUNDING_BOX SceneBounds::box() const
{
    if (m_empty) {
        AA_BOUNDING_BOX box;
        VECTOR3D mins(-50, -50, -10);
        VECTOR3D maxes(50, 50, 10);
        box.SetFromMinsMaxes(mins, maxes);
        return box;
    }
    return m_box;
}

FastComponent loadFastComponent(const FastSource& source, std::size_t& recordBudget)
{
    FastComponent out;
    out.componentId = source.componentId;
    std::set<LayerKey> keys;
    for (const auto& [key, place] : source.layers) {
        keys.insert(key);
    }
    std::map<LayerKey, LayerRecords> records =
        extractRecordsFromFile(source.layoutPath, source.topCell, keys, recordBudget);
    for (const auto& [key, place] : source.layers) {
        auto it = records.find(key);
        if (it == records.end()) {
            continue;
        }
        FastLayer layer;
        layer.key = key;
        layer.place = place;
        layer.tiles = tileLayer(std::move(it->second));
        if (std::string note = preparedNote(source.componentId, key.layer, key.datatype,
                                            place.name, layer.tiles.shapes, layer.tiles.inGds);
            !note.empty()) {
            out.notes.push_back(std::move(note));
        }
        out.layers.push_back(std::move(layer));
    }
    std::stable_sort(out.layers.begin(), out.layers.end(),
                     [](const FastLayer& a, const FastLayer& b) {
                         return a.place.zBottom < b.place.zBottom;
                     });
    return out;
}

FastSource fillSource(const FastSource& source)
{
    FastSource out = source;
    out.layers.clear();
    for (const auto& [key, place] : source.layers) {
        if (place.fill) out.layers.emplace_back(key, place);
    }
    return out;
}

FastComponent loadFillTiles(const FastSource& source, std::size_t& recordBudget)
{
    FastComponent out = loadFastComponent(fillSource(source), recordBudget);
    out.fillOnly = true;
    return out;
}

std::array<float, 16> fastLayerModel(const QMatrix4x4& transform,
                                     const QVector2D& centeringOffsetMm, float spreadMm)
{
    // The standard meshes' vertices: ((x - cx) mm, z mm, -(y - cy) mm).
    QMatrix4x4 frame(0.001f, 0.0f,    0.0f, -centeringOffsetMm.x(),
                     0.0f,   0.0f,    0.001f, 0.0f,
                     0.0f,  -0.001f,  0.0f,  centeringOffsetMm.y(),
                     0.0f,   0.0f,    0.0f,  1.0f);
    QMatrix4x4 model = transform;
    model.translate(0.0f, spreadMm, 0.0f);
    model = model * frame;
    std::array<float, 16> out;
    std::copy(model.constData(), model.constData() + 16, out.begin());
    return out;
}

FastFrame fastFrame(const std::vector<FastFramePart>& parts,
                    const std::function<bool(const std::string& componentId,
                                             const FastLayer& layer)>& layerShown,
                    float spreadFactor)
{
    FastFrame frame;
    for (const FastFramePart& part : parts) {
        if (!part.fast || !part.geometry) continue;
        const Component3DGeometry& g = *part.geometry;
        std::map<double, int> tops;
        for (const FastLayer& layer : part.fast->layers) {
            // As the standard view spreads a mesh: by its own z_bottom (mm).
            const float spreadMm =
                spreadFactor == 1.0f
                    ? 0.0f
                    : float(layer.place.zBottom * 0.001) * (spreadFactor - 1.0f);
            TileLayerInput in;
            in.tiles = &layer.tiles;
            in.model = fastLayerModel(g.transform, g.centeringOffset, spreadMm);
            in.zBottom = float(layer.place.zBottom);
            in.zTop = float(layer.place.zBottom + layer.place.thickness);
            in.visible = part.shown && !(part.hideSubstrate && layer.place.name == "Substrate")
                      && layerShown(part.fast->componentId, layer);
            frame.inputs.push_back(in);
            frame.refs.push_back({part.fast->componentId, &layer});
            frame.coverRanks.push_back(tops[layer.place.zBottom + layer.place.thickness]++);
        }
    }
    return frame;
}

std::map<std::string, std::string> coverageNotes(const std::vector<FastFrameLayer>& refs,
                                                 const TileChoice& choice)
{
    struct Sum {
        std::size_t layers = 0, coverage = 0, of = 0;
    };
    std::map<std::string, Sum> sums;
    for (std::size_t i = 0; i < refs.size() && i < choice.layers.size(); ++i) {
        const LayerBuckets& b = choice.layers[i];
        if (b.coverage == 0) continue;
        Sum& s = sums[refs[i].componentId];
        ++s.layers;
        s.coverage += b.coverage;
        s.of += refs[i].layer->tiles.inGds;
    }
    std::map<std::string, std::string> out;
    for (const auto& [id, s] : sums) {
        out[id] = fastCoverageNote(id, s.layers, s.coverage, s.of);
    }
    return out;
}

std::map<std::string, std::string> fillCoverageNotes(const std::vector<FastFrameLayer>& refs,
                                                     const TileChoice& choice)
{
    struct Sum {
        std::size_t layers = 0, shapes = 0;
    };
    std::map<std::string, Sum> sums;
    for (std::size_t i = 0; i < refs.size() && i < choice.layers.size(); ++i) {
        const LayerBuckets& b = choice.layers[i];
        if (!refs[i].layer->place.fill || b.coverage == 0) continue;
        Sum& s = sums[refs[i].componentId];
        ++s.layers;
        s.shapes += b.coverage;
    }
    std::map<std::string, std::string> out;
    for (const auto& [id, s] : sums) {
        out[id] = fillCoverageNote(id, s.layers, s.shapes);
    }
    return out;
}

std::vector<LayerRow> fastLayerRows(std::vector<LayerRow> standard, const FastComponent& fast,
                                    const std::map<LayerKey, LayerBuckets>& buckets,
                                    bool fillOnly)
{
    std::map<LayerKey, const FastLayer*> prepared;
    for (const FastLayer& layer : fast.layers) {
        if (!fillOnly || layer.place.fill) prepared[layer.key] = &layer;
    }
    auto count = [&](LayerRow& row, const FastLayer& layer) {
        row.meshed = true;
        row.count.clear();
        row.countNote.clear();
        auto b = buckets.find(layer.key);
        if (b == buckets.end()) return;
        row.count = fastRowLabel(layer.tiles.inGds, b->second);
        if (b->second.coverage > 0) row.countNote = coverageRowNote();
    };
    std::set<LayerKey> listed;
    for (LayerRow& row : standard) {
        listed.insert(row.key);
        if (auto it = prepared.find(row.key); it != prepared.end()) count(row, *it->second);
    }
    for (const FastLayer& layer : fast.layers) {
        if (listed.count(layer.key) || !prepared.count(layer.key)) continue;
        LayerRow row;
        row.key = layer.key;
        row.name = layer.place.name;
        row.fill = layer.place.fill;
        count(row, layer);
        standard.push_back(std::move(row));
    }
    return standard;
}

std::vector<std::string> standardNotes(const Assembly& assembly,
                                       const std::vector<std::string>& sheetNotes,
                                       const std::map<QString, std::string>& componentNotes,
                                       const std::map<QString, std::vector<ShapeCount>>& counts)
{
    std::vector<std::string> out(sheetNotes.begin(), sheetNotes.end());
    for (const auto& comp : assembly.components()) {
        const QString id = QString::fromStdString(comp->id());
        auto it = componentNotes.find(id);
        if (it != componentNotes.end()) {
            out.push_back(it->second);
        }
        auto c = counts.find(id);
        if (c != counts.end()) {
            for (const ShapeCount& count : c->second) {
                if (count.fill) continue;
                if (std::string n = shapeCountNote(comp->id(), count); !n.empty()) {
                    out.push_back(std::move(n));
                }
            }
        }
    }
    return out;
}

std::vector<std::string> fastNotes(ShapeMode mode, const Assembly& assembly,
                                   const std::vector<std::string>& sheetNotes,
                                   const std::map<QString, std::string>& componentNotes,
                                   const std::map<QString, FastComponent>& fast,
                                   const std::map<std::string, std::string>& coverage)
{
    std::vector<std::string> out;
    if (std::string m = shapeModeNote(mode); !m.empty()) {
        out.push_back(std::move(m));
    }
    out.insert(out.end(), sheetNotes.begin(), sheetNotes.end());
    for (const auto& comp : assembly.components()) {
        const QString id = QString::fromStdString(comp->id());
        if (auto it = componentNotes.find(id); it != componentNotes.end()) {
            out.push_back(it->second);
        }
        if (auto f = fast.find(id); f != fast.end()) {
            out.insert(out.end(), f->second.notes.begin(), f->second.notes.end());
        }
        if (auto c = coverage.find(comp->id()); c != coverage.end()) {
            out.push_back(c->second);
        }
    }
    return out;
}

std::vector<UndetailedPart> undetailedParts(const Assembly& assembly,
                                            const std::map<QString, Component3DGeometry>& geometry)
{
    std::vector<UndetailedPart> out;
    for (const auto& comp : assembly.components()) {
        const RenderMode mode = comp->render_mode();
        if (mode == RenderMode::Detailed || mode == RenderMode::DetailedNoSubstrate
            || mode == RenderMode::Hidden) {
            continue;
        }
        auto g = geometry.find(QString::fromStdString(comp->id()));
        if (g == geometry.end() || g->second.layers.empty()) {
            continue;
        }
        UndetailedPart part;
        part.componentId = comp->id();
        part.fill = std::any_of(g->second.layers.begin(), g->second.layers.end(),
                                [](const LayerMesh& m) { return m.fill; });
        part.faceDown = comp->orientation() == Orientation::FaceDown;
        out.push_back(std::move(part));
    }
    return out;
}

std::string detailNote(ShapeMode mode, bool showFill, const std::vector<UndetailedPart>& parts)
{
    std::string names;
    std::size_t n = 0;
    bool faceDown = false;
    for (const UndetailedPart& part : parts) {
        if (mode == ShapeMode::Standard && !(showFill && part.fill)) {
            continue;
        }
        names += (n++ ? ", " : "") + part.componentId;
        faceDown = faceDown || part.faceDown;
    }
    if (n == 0) {
        return {};
    }
    std::string note = "3D: " + names + (n == 1 ? " is" : " are")
                     + " not drawn Detailed, so " + (n == 1 ? "its" : "their")
                     + " layers, fill and 3D Shapes do not show. Right-click "
                     + (n == 1 ? "it" : "them") + " in Hierarchy, Render Mode, Detailed";
    if (faceDown) {
        note += "; a face-down die needs Detailed (no Si bulk) to show them from above";
    }
    return note + ".";
}

std::vector<std::string> viewNotes(const ViewNotesState& state, const Assembly& assembly,
                                   const std::vector<std::string>& sheetNotes,
                                   const std::map<QString, std::string>& componentNotes,
                                   const std::map<QString, std::vector<ShapeCount>>& counts,
                                   const std::map<QString, FastComponent>& fast,
                                   const std::map<std::string, std::string>& coverage)
{
    std::vector<std::string> out;
    if (state.mode == ShapeMode::Standard) {
        out = standardNotes(assembly, sheetNotes, componentNotes, counts);
        if (state.showFill && !state.fillPrepared) {
            out.push_back(fillReadingNote());
        } else if (state.showFill) {
            for (const auto& comp : assembly.components()) {
                if (auto f = fast.find(QString::fromStdString(comp->id())); f != fast.end()) {
                    for (const FastLayer& l : f->second.layers) {
                        if (!l.place.fill) continue;
                        if (std::string n = preparedNote(comp->id(), l.key.layer, l.key.datatype,
                                                         l.place.name, l.tiles.shapes,
                                                         l.tiles.inGds);
                            !n.empty()) {
                            out.push_back(std::move(n));
                        }
                    }
                    if (f->second.layers.empty()) {
                        // A read that failed says so.
                        out.insert(out.end(), f->second.notes.begin(), f->second.notes.end());
                    }
                }
                if (auto c = coverage.find(comp->id()); c != coverage.end()) {
                    out.push_back(c->second);
                }
            }
        }
    } else if (!state.prepared) {
        out = {shapeModeNote(state.mode), shapePreparingNote(state.mode)};
        for (std::string& n : standardNotes(assembly, sheetNotes, componentNotes, counts)) {
            out.push_back(std::move(n));
        }
    } else {
        out = fastNotes(state.mode, assembly, sheetNotes, componentNotes, fast, coverage);
        if (state.mode == ShapeMode::Every && state.everyFrameSeconds >= 0.0 && !out.empty()) {
            out.front() = everyShapeNote(state.everyFrameSeconds);
        }
    }
    if (std::string d = detailNote(state.mode, state.showFill, state.undetailed); !d.empty()) {
        out.insert(out.begin(), std::move(d));
    }
    return out;
}

} // namespace chiplet
