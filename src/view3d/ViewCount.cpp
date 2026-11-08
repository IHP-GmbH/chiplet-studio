// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ViewCount.h"

#include "GdsPlacement.h"
#include "core/Assembly.h"
#include "core/Component.h"

#include <QMatrix4x4>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace chiplet {

namespace {

std::string keyText(const LayerKey& key)
{
    return std::to_string(key.layer) + "/" + std::to_string(key.datatype);
}

std::string modeName(ShapeMode mode)
{
    switch (mode) {
    case ShapeMode::Standard:
        return "standard";
    case ShapeMode::Fast:
        return "fast";
    case ShapeMode::Every:
        return "every";
    }
    return "standard";
}

std::string cameraName(ViewCountCamera camera)
{
    switch (camera) {
    case ViewCountCamera::None:
        return "none";
    case ViewCountCamera::Fit:
        return "fit";
    case ViewCountCamera::Mid:
        return "mid";
    case ViewCountCamera::Close:
        return "close";
    case ViewCountCamera::Explicit:
        return "explicit";
    }
    return "none";
}

bool detailed(const Component& comp)
{
    return comp.render_mode() == RenderMode::Detailed
        || comp.render_mode() == RenderMode::DetailedNoSubstrate;
}

bool contains(const std::vector<std::string>& lines, const std::string& line)
{
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}

AA_BOUNDING_BOX geometryBounds(const Component3DGeometry& g, bool& any)
{
    AA_BOUNDING_BOX box;
    any = false;
    for (const LayerMesh& layer : g.layers) {
        const AA_BOUNDING_BOX wb = transformedMeshBounds(layer.mesh.boundingBox(), g.transform);
        if (!any) {
            box = wb;
            any = true;
        } else {
            box.AddBounds(wb);
        }
    }
    return box;
}

// Every component's layers and every box: what the fit camera frames.
AA_BOUNDING_BOX sceneBox(const StandardScene& scene)
{
    SceneBounds bounds;
    for (const auto& [id, c] : scene.components) {
        bounds.add(c.geometry);
    }
    for (const auto& [id, mesh] : scene.boxes) {
        bounds.add(mesh);
    }
    return bounds.box();
}

bool inBox(const VECTOR3D& p, const AA_BOUNDING_BOX& b)
{
    return p.x >= b.mins.x && p.x <= b.maxes.x && p.y >= b.mins.y && p.y <= b.maxes.y
        && p.z >= b.mins.z && p.z <= b.maxes.z;
}

// Whether the segment from a to b passes through the inside of the box. Its
// faces do not count, so a target on a top face is not hidden by that box.
bool segmentCrossesBox(const VECTOR3D& a, const VECTOR3D& b, const AA_BOUNDING_BOX& box)
{
    const double p[3] = {a.x, a.y, a.z};
    const double q[3] = {b.x, b.y, b.z};
    const double lo[3] = {box.mins.x, box.mins.y, box.mins.z};
    const double hi[3] = {box.maxes.x, box.maxes.y, box.maxes.z};
    double t0 = 0.0, t1 = 1.0;
    for (int k = 0; k < 3; ++k) {
        const double d = q[k] - p[k];
        if (d == 0.0) {
            if (!(p[k] > lo[k] && p[k] < hi[k])) return false;
            continue;
        }
        double ta = (lo[k] - p[k]) / d, tb = (hi[k] - p[k]) / d;
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (!(t0 < t1)) return false;
    }
    return true;
}

// The close camera's target: a 64 x 64 grid of cell centres over the top face.
constexpr int kCloseGrid = 64;

// Look at the top face of the assembly's first interposer (without one, of its
// first component with a box) from the camera's direction and distance. The
// face is the x and z extent of the component's boxes at the higher of their
// tops. The target is the first grid point, nearest the face's centre first
// and equal distances by x then z ascending, from which the eye is outside
// every box and the line to the target crosses none.
bool placeClose(const Assembly& assembly, const StandardScene& scene, Camera& camera,
                std::string& error, CloseCell* cell)
{
    const std::vector<ComponentBox> boxes = componentBoxes(scene);
    std::string face;
    for (int pass = 0; pass < 2 && face.empty(); ++pass) {
        for (const auto& comp : assembly.components()) {
            if (pass == 0 && comp->type() != ComponentType::Interposer) continue;
            const bool hasBox = std::any_of(boxes.begin(), boxes.end(),
                [&comp](const ComponentBox& b) { return b.id == comp->id(); });
            if (hasBox) {
                face = comp->id();
                break;
            }
        }
    }
    if (face.empty()) {
        error = "no component with a box for the close camera";
        return false;
    }
    float x0 = 0, x1 = 0, z0 = 0, z1 = 0, top = 0;
    bool first = true;
    for (const ComponentBox& b : boxes) {
        if (b.id != face) continue;
        if (first) {
            x0 = b.box.mins.x, x1 = b.box.maxes.x, z0 = b.box.mins.z, z1 = b.box.maxes.z;
            top = b.box.maxes.y;
            first = false;
        } else {
            x0 = std::min(x0, b.box.mins.x), x1 = std::max(x1, b.box.maxes.x);
            z0 = std::min(z0, b.box.mins.z), z1 = std::max(z1, b.box.maxes.z);
            top = std::max(top, b.box.maxes.y);
        }
    }
    const float w = x1 - x0, h = z1 - z0;

    struct Point {
        double distance;  // squared, from the face's centre, in units of half a cell
        int i, j;
    };
    std::vector<Point> points;
    points.reserve(kCloseGrid * kCloseGrid);
    for (int i = 0; i < kCloseGrid; ++i) {
        for (int j = 0; j < kCloseGrid; ++j) {
            const double dx = double(2 * i + 1 - kCloseGrid) * w;
            const double dz = double(2 * j + 1 - kCloseGrid) * h;
            points.push_back({dx * dx + dz * dz, i, j});
        }
    }
    std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) {
        if (a.distance != b.distance) return a.distance < b.distance;
        if (a.i != b.i) return a.i < b.i;
        return a.j < b.j;
    });
    int tried = 0;
    for (const Point& pt : points) {
        const VECTOR3D target(x0 + w * float(2 * pt.i + 1) / float(2 * kCloseGrid), top,
                              z0 + h * float(2 * pt.j + 1) / float(2 * kCloseGrid));
        camera.setTarget(target);
        const VECTOR3D eye = camera.position();
        ++tried;
        const bool clear = std::none_of(boxes.begin(), boxes.end(),
            [&](const ComponentBox& b) {
                return inBox(eye, b.box) || segmentCrossesBox(eye, target, b.box);
            });
        if (clear) {
            if (cell) *cell = {face, pt.i, pt.j, tried};
            return true;
        }
    }
    error = "no point on " + face + "'s top face gives the close camera a clear view";
    return false;
}

}  // namespace

std::vector<ComponentBox> componentBoxes(const StandardScene& scene)
{
    std::vector<ComponentBox> out;
    for (const auto& [id, c] : scene.components) {
        bool any = false;
        const AA_BOUNDING_BOX box = geometryBounds(c.geometry, any);
        if (any) out.push_back({id.toStdString(), "layers", box});
    }
    for (const auto& [id, mesh] : scene.boxes) {
        out.push_back({id.toStdString(), "body", mesh.boundingBox()});
    }
    return out;
}

bool viewCountCamera(const Assembly& assembly, const StandardScene& scene,
                     const ViewCountOptions& options, Camera& camera, std::string& error,
                     CloseCell* cell)
{
    const float aspect = float(options.width) / float(options.height);
    const AA_BOUNDING_BOX all = sceneBox(scene);

    camera = Camera();
    switch (options.camera) {
    case ViewCountCamera::None:
        return true;
    case ViewCountCamera::Fit:
        camera.fitToBox(all, aspect);
        return true;
    case ViewCountCamera::Mid: {
        for (const auto& comp : assembly.components()) {
            if (comp->type() != ComponentType::Die) continue;
            auto it = scene.components.find(QString::fromStdString(comp->id()));
            if (it == scene.components.end()) continue;
            bool any = false;
            const AA_BOUNDING_BOX box = geometryBounds(it->second.geometry, any);
            if (!any) continue;
            camera.setOrientation(kObliqueYaw, kObliquePitch);
            camera.fitToBox(box, aspect);
            return true;
        }
        error = "no die with layer geometry for the mid camera";
        return false;
    }
    case ViewCountCamera::Close:
        camera.setOrientation(kObliqueYaw, kObliquePitch);
        camera.fitToBox(all, aspect);
        camera.setDistance(camera.distance() / 10.0f);
        return placeClose(assembly, scene, camera, error, cell);
    case ViewCountCamera::Explicit: {
        const float dx = options.eye[0] - options.target[0];
        const float dy = options.eye[1] - options.target[1];
        const float dz = options.eye[2] - options.target[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!(d > 0)) {
            error = "the eye is on the target";
            return false;
        }
        const float pitch = std::asin(dy / d) * 180.0f / 3.14159265358979f;
        const float yaw = std::atan2(dx, dz) * 180.0f / 3.14159265358979f;
        camera.setOrientation(yaw, pitch);
        camera.setTarget(VECTOR3D(options.target[0], options.target[1], options.target[2]));
        camera.setDistance(d);
        return true;
    }
    }
    return true;
}

TileCamera tileCamera(const Camera& camera, int width, int height)
{
    const float aspect = float(width) / float(height);
    const MATRIX4X4 v = camera.viewMatrix();
    const MATRIX4X4 p = camera.projectionMatrix(aspect);
    QMatrix4x4 view, projection;
    for (int i = 0; i < 16; ++i) {
        view.data()[i] = v.GetEntry(i);
        projection.data()[i] = p.GetEntry(i);
    }
    const QMatrix4x4 vp = projection * view;
    TileCamera out;
    std::copy(vp.constData(), vp.constData() + 16, out.viewProjection.begin());
    const VECTOR3D eye = camera.position();
    out.eye = {eye.x, eye.y, eye.z};
    out.fovYDegrees = camera.fov();
    out.viewportHeight = height;
    return out;
}

std::optional<RenderMode> renderModeNamed(const std::string& name)
{
    static const std::map<std::string, RenderMode> named = {
        {"hidden", RenderMode::Hidden},
        {"wireframe", RenderMode::Wireframe},
        {"transparent", RenderMode::Transparent},
        {"solid", RenderMode::Solid},
        {"detailed", RenderMode::Detailed},
        {"detailed-no-si", RenderMode::DetailedNoSubstrate},
    };
    const auto it = named.find(name);
    if (it == named.end()) return std::nullopt;
    return it->second;
}

bool setRenderModes(Assembly& assembly, const std::map<std::string, std::string>& modes,
                    std::string& error)
{
    std::vector<std::pair<Component*, RenderMode>> set;
    for (const auto& [id, name] : modes) {
        Component* comp = assembly.component(id);
        if (!comp) {
            error = "no component " + id;
            return false;
        }
        const std::optional<RenderMode> mode = renderModeNamed(name);
        if (!mode) {
            error = "no render mode " + name;
            return false;
        }
        set.emplace_back(comp, *mode);
    }
    for (const auto& [comp, mode] : set) comp->set_render_mode(mode);
    return true;
}

ViewCountReport viewCount(const Assembly& assembly, const ViewCountOptions& options)
{
    const LayerSheets sheets = loadLayerSheets(assembly);
    return viewCountScene(assembly, loadStandardScene(assembly, sheets.props, {}), sheets.notes,
                          options);
}

ViewCountReport viewCountScene(const Assembly& assembly, StandardScene scene,
                               const std::vector<std::string>& sheetNotes,
                               const ViewCountOptions& options)
{
    ViewCountReport report;
    auto& out = report.lines;
    auto fail = [&report](std::string why) { report.failures.push_back(std::move(why)); };
    auto finish = [&report]() {
        report.lines.push_back(report.failures.empty()
                                   ? "check ok"
                                   : "check FAILED (" + std::to_string(report.failures.size())
                                         + ")");
        for (const std::string& f : report.failures) report.lines.push_back("fail " + f);
        return report;
    };

    out.push_back("viewcount shapes=" + modeName(options.mode) + " camera="
                  + cameraName(options.camera) + " viewport=" + std::to_string(options.width)
                  + "x" + std::to_string(options.height) + " fill="
                  + (options.fill ? "on" : "off"));

    std::map<QString, std::string> componentNotes;
    std::map<QString, std::vector<ShapeCount>> counts;
    for (auto& [id, c] : scene.components) {
        if (!c.note.empty()) componentNotes[id] = c.note;
        counts[id] = c.counts;
        setFillVisible(c.geometry, options.fill);
    }

    Camera camera;
    TileCamera view;
    const bool withCamera = options.camera != ViewCountCamera::None;
    if (withCamera) {
        std::string error;
        CloseCell cell;
        if (!viewCountCamera(assembly, scene, options, camera, error, &cell)) {
            fail("camera: " + error);
            return finish();
        }
        view = tileCamera(camera, options.width, options.height);
        const VECTOR3D e = camera.position(), t = camera.target();
        char buf[256];
        std::snprintf(buf, sizeof buf,
                      "camera eye=%.6g,%.6g,%.6g target=%.6g,%.6g,%.6g distance=%.6g mm",
                      e.x, e.y, e.z, t.x, t.y, t.z, camera.distance());
        out.push_back(buf);
        if (options.camera == ViewCountCamera::Close) {
            std::snprintf(buf, sizeof buf, "close cell=%d,%d of %dx%d on %s's top face, tried=%d",
                          cell.i, cell.j, kCloseGrid, kCloseGrid, cell.face.c_str(), cell.tried);
            out.push_back(buf);
        }
        // The box the fit camera frames, and each component's layers and body,
        // to tell whether the eye is in one.
        auto boxLine = [&out, &e](const std::string& what, const AA_BOUNDING_BOX& b) {
            char line[256];
            std::snprintf(line, sizeof line,
                          "%s box min=%.6g,%.6g,%.6g max=%.6g,%.6g,%.6g mm; eye %s it",
                          what.c_str(), b.mins.x, b.mins.y, b.mins.z, b.maxes.x, b.maxes.y,
                          b.maxes.z, inBox(e, b) ? "inside" : "outside");
            out.push_back(line);
        };
        boxLine("scene", sceneBox(scene));
        for (const ComponentBox& b : componentBoxes(scene)) {
            boxLine("component " + b.id + " " + b.kind, b.box);
        }
    }

    // Components the view draws as a box only.
    for (const auto& comp : assembly.components()) {
        const QString id = QString::fromStdString(comp->id());
        if (scene.components.count(id)) continue;
        if (comp->layout_path().empty()) {
            out.push_back("component " + comp->id() + ": no layout, drawn as a box");
        } else {
            out.push_back("component " + comp->id() + ": no shapes read from "
                          + comp->layout_path());
            fail(comp->id() + ": its layout gave 3D no shapes, and no note says so");
        }
    }

    if (options.mode == ShapeMode::Standard) {
        // As the view: fill is never its standard mesh. With fill on, every
        // fill shape is read and drawn as coverage tiles.
        std::size_t budget = options.recordBudget;
        std::map<QString, FastComponent> fill;
        std::vector<FastFrameLayer> refs;
        std::vector<TileLayerInput> inputs;
        TileChoice choice;
        std::map<std::string, std::string> fillCoverage;
        if (options.fill) {
            std::vector<FastFramePart> parts;
            for (const auto& comp : assembly.components()) {
                const QString id = QString::fromStdString(comp->id());
                auto it = scene.components.find(id);
                if (it == scene.components.end() || fillSource(it->second.fast).layers.empty()) {
                    continue;
                }
                fill.emplace(id, loadFillTiles(it->second.fast, budget));
            }
            for (const auto& comp : assembly.components()) {
                const QString id = QString::fromStdString(comp->id());
                auto f = fill.find(id);
                if (f == fill.end()) continue;
                parts.push_back({&f->second, &scene.components.at(id).geometry, detailed(*comp),
                                 comp->render_mode() == RenderMode::DetailedNoSubstrate});
            }
            if (withCamera) {
                FastFrame frame = fastFrame(
                    parts, [](const std::string&, const FastLayer& l) { return l.place.fill; },
                    1.0f);
                refs = std::move(frame.refs);
                inputs = std::move(frame.inputs);
                TileChoiceMemory memory;
                choice = chooseTiles(inputs, view, tileChoiceParams(ShapeMode::Standard), memory);
                fillCoverage = fillCoverageNotes(refs, choice);
            }
        }
        ViewNotesState state;
        state.showFill = options.fill;
        state.fillPrepared = true;
        const std::vector<std::string> notes = viewNotes(state, assembly, sheetNotes,
                                                         componentNotes, counts, fill, fillCoverage);
        std::map<const FastLayer*, std::size_t> choiceIndex;
        for (std::size_t i = 0; i < refs.size(); ++i) choiceIndex[refs[i].layer] = i;

        for (const auto& comp : assembly.components()) {
            auto it = scene.components.find(QString::fromStdString(comp->id()));
            if (it == scene.components.end()) continue;
            const StandardComponent& c = it->second;
            const std::string& id = comp->id();
            out.push_back("component " + id + " cell=" + comp->top_cell() + " gds_layers="
                          + std::to_string(c.tally.size()));
            std::map<LayerKey, const ShapeCount*> byKey;
            for (const ShapeCount& sc : c.counts) byKey[sc.key] = &sc;
            std::map<LayerKey, const FastLayer*> fillByKey;
            if (auto f = fill.find(it->first); f != fill.end()) {
                for (const FastLayer& l : f->second.layers) fillByKey[l.key] = &l;
            }
            const std::set<LayerKey> unmodeled(c.geometry.unmodeled.begin(),
                                               c.geometry.unmodeled.end());
            std::size_t fillLayers = 0, fillShapes = 0;
            for (const auto& [key, t] : c.tally) {
                const std::string k = id + " " + keyText(key);
                if (auto sc = byKey.find(key); sc != byKey.end() && sc->second->fill) {
                    const ShapeCount& n = *sc->second;
                    if (!options.fill) {
                        out.push_back("layer " + k + " " + n.name + " fill gds="
                                      + std::to_string(n.inGds) + " hidden (fill off)");
                        continue;
                    }
                    auto l = fillByKey.find(key);
                    if (l == fillByKey.end()) {
                        out.push_back("missing " + k + " gds=" + std::to_string(n.inGds));
                        fail(k + ": fill is on, but the fill read returned nothing for it");
                        continue;
                    }
                    const FastLayer& fl = *l->second;
                    const TiledLayer& tl = fl.tiles;
                    out.push_back("layer " + k + " " + n.name + " fill gds="
                                  + std::to_string(n.inGds) + " prepared="
                                  + std::to_string(tl.shapes) + " boxes="
                                  + std::to_string(tl.boxes.size()) + " others="
                                  + std::to_string(tl.shapes - tl.boxes.size()) + " trapezoids="
                                  + std::to_string(tl.traps.size()) + " tiles="
                                  + std::to_string(tl.tiles.size()));
                    if (tl.inGds != n.inGds) {
                        fail(k + ": the fill reader counts " + std::to_string(tl.inGds)
                             + " shapes, the standard one " + std::to_string(n.inGds));
                    }
                    if (tl.shapes < n.inGds
                        && !contains(notes, preparedNote(id, key.layer, key.datatype, n.name,
                                                         tl.shapes, n.inGds))) {
                        fail(k + ": " + std::to_string(n.inGds - tl.shapes)
                             + " fill shapes not prepared and no note names them");
                    }
                    if (withCamera) {
                        const LayerBuckets& b = choice.layers[choiceIndex.at(&fl)];
                        out.push_back("buckets " + k + " shape=" + std::to_string(b.shape)
                                      + " coverage=" + std::to_string(b.coverage)
                                      + " outside=" + std::to_string(b.outside)
                                      + " hidden=" + std::to_string(b.hidden)
                                      + " shape_records=" + std::to_string(b.shapeRecords)
                                      + " coverage_tiles=" + std::to_string(b.coverageTiles));
                        out.push_back("row " + k + " " + n.name + ": "
                                      + fastRowLabel(n.inGds, b));
                        if (b.total() != tl.shapes) {
                            fail(k + ": the buckets add up to " + std::to_string(b.total())
                                 + ", " + std::to_string(tl.shapes) + " were prepared");
                        }
                        if (b.shape > 0) {
                            fail(k + ": " + std::to_string(b.shape)
                                 + " fill shapes drawn as shapes; Standard draws fill as "
                                   "coverage tiles only");
                        }
                        if (b.coverage > 0) {
                            ++fillLayers;
                            fillShapes += b.coverage;
                        }
                    }
                    continue;
                }
                if (auto sc = byKey.find(key); sc != byKey.end()) {
                    const ShapeCount& n = *sc->second;
                    out.push_back("layer " + k + " " + n.name
                                  + (n.borrowed ? " by-layer-number" : "")
                                  + " gds=" + std::to_string(n.inGds)
                                  + " drawn=" + std::to_string(n.drawn)
                                  + " under_area=" + std::to_string(n.underArea)
                                  + " past_read=" + std::to_string(n.pastRead)
                                  + " via_skipped=" + std::to_string(n.viaSkipped)
                                  + " past_mesh=" + std::to_string(n.pastMesh));
                    const std::size_t parts =
                        n.drawn + n.underArea + n.pastRead + n.viaSkipped + n.pastMesh;
                    if (parts != n.inGds) {
                        fail(k + ": drawn and left out add up to " + std::to_string(parts)
                             + ", the GDS holds " + std::to_string(n.inGds));
                    }
                    if (n.drawn < n.inGds && !contains(notes, shapeCountNote(id, n))) {
                        fail(k + ": " + std::to_string(n.inGds - n.drawn)
                             + " shapes left out and no note names them");
                    }
                    continue;
                }
                out.push_back("unplaced " + k + " gds=" + std::to_string(t.inGds));
                if (!unmodeled.count(key) || c.note.empty() || !contains(notes, c.note)) {
                    fail(k + ": " + std::to_string(t.inGds)
                         + " shapes placed nowhere and no note names them");
                }
            }
            if (fillLayers > 0) {
                const std::string line = fillCoverageNote(id, fillLayers, fillShapes);
                out.push_back("coverage " + id + " fill_layers=" + std::to_string(fillLayers)
                              + " shapes=" + std::to_string(fillShapes));
                if (!contains(notes, line)) {
                    fail(id + ": " + std::to_string(fillShapes)
                         + " fill shapes drawn as coverage tiles and the notes do not say so: "
                         + line);
                }
            }
            if (withCamera) {
                for (const LayerMesh& mesh : c.geometry.layers) {
                    if (!standardMeshDrawn(mesh) || !detailed(*comp)) continue;
                    if (comp->render_mode() == RenderMode::DetailedNoSubstrate
                        && mesh.name == "Substrate") {
                        continue;
                    }
                    const AA_BOUNDING_BOX wb =
                        transformedMeshBounds(mesh.mesh.boundingBox(), c.geometry.transform);
                    const float lo[3] = {wb.mins.x, wb.mins.y, wb.mins.z};
                    const float hi[3] = {wb.maxes.x, wb.maxes.y, wb.maxes.z};
                    if (!worldBoxInView(view.viewProjection, lo, hi)) continue;
                    std::string note;
                    if (auto sc = byKey.find(mesh.key); sc != byKey.end()) {
                        note = shapeCountNote(id, *sc->second);
                    }
                    out.push_back("in-view " + id + " " + keyText(mesh.key) + " " + mesh.name
                                  + ": " + (note.empty() ? std::string("all drawn") : note));
                }
            }
        }
        if (options.fill && withCamera) {
            const auto shown = std::count_if(inputs.begin(), inputs.end(),
                                             [](const TileLayerInput& in) { return in.visible; });
            out.push_back("frame fill layers_shown=" + std::to_string(shown) + " of "
                          + std::to_string(inputs.size())
                          + " shape_records=" + std::to_string(choice.shapeRecords)
                          + " coverage_tiles=" + std::to_string(choice.coverageTiles));
        }
        for (const std::string& n : notes) out.push_back("note " + n);
    } else {
        std::size_t budget = options.recordBudget;
        std::map<QString, FastComponent> fast;
        for (const auto& comp : assembly.components()) {
            const QString id = QString::fromStdString(comp->id());
            auto it = scene.components.find(id);
            if (it == scene.components.end()) continue;
            fast.emplace(id, loadFastComponent(it->second.fast, budget));
        }

        std::vector<FastFrameLayer> refs;
        std::vector<TileLayerInput> inputs;
        TileChoice choice;
        std::map<std::string, std::string> coverage;
        if (withCamera) {
            std::vector<FastFramePart> parts;
            for (const auto& comp : assembly.components()) {
                const QString id = QString::fromStdString(comp->id());
                auto f = fast.find(id);
                if (f == fast.end()) continue;
                parts.push_back({&f->second, &scene.components.at(id).geometry, detailed(*comp),
                                 comp->render_mode() == RenderMode::DetailedNoSubstrate});
            }
            // As setFillVisible does for the standard meshes: the fill switch
            // decides a fill layer, not its stackup flag.
            FastFrame frame = fastFrame(
                parts,
                [&options](const std::string&, const FastLayer& layer) {
                    return layer.place.fill ? options.fill : layer.place.visible;
                },
                1.0f);
            refs = std::move(frame.refs);
            inputs = std::move(frame.inputs);
            TileChoiceMemory memory;
            choice = chooseTiles(inputs, view, tileChoiceParams(options.mode), memory);
            coverage = coverageNotes(refs, choice);
        }
        const std::vector<std::string> notes =
            fastNotes(options.mode, assembly, sheetNotes, componentNotes, fast, coverage);

        std::map<const FastLayer*, std::size_t> choiceIndex;
        for (std::size_t i = 0; i < refs.size(); ++i) choiceIndex[refs[i].layer] = i;

        for (const auto& comp : assembly.components()) {
            const QString qid = QString::fromStdString(comp->id());
            auto f = fast.find(qid);
            if (f == fast.end()) continue;
            const StandardComponent& c = scene.components.at(qid);
            const std::string& id = comp->id();
            out.push_back("component " + id + " cell=" + comp->top_cell() + " gds_layers="
                          + std::to_string(c.tally.size()));
            std::map<LayerKey, const FastLayer*> byKey;
            for (const FastLayer& l : f->second.layers) byKey[l.key] = &l;
            std::set<LayerKey> sourced;
            for (const auto& [key, place] : c.fast.layers) sourced.insert(key);
            const std::set<LayerKey> unmodeled(c.geometry.unmodeled.begin(),
                                               c.geometry.unmodeled.end());
            const bool body = c.stackup.find(40, 0) != nullptr;
            std::size_t coverageLayers = 0, coverageShapes = 0, coverageOf = 0;

            for (const auto& [key, t] : c.tally) {
                const std::string k = id + " " + keyText(key);
                if (auto l = byKey.find(key); l != byKey.end()) {
                    const FastLayer& fl = *l->second;
                    const TiledLayer& tl = fl.tiles;
                    out.push_back("layer " + k + " " + fl.place.name + " gds="
                                  + std::to_string(t.inGds) + " prepared="
                                  + std::to_string(tl.shapes) + " boxes="
                                  + std::to_string(tl.boxes.size()) + " others="
                                  + std::to_string(tl.shapes - tl.boxes.size()) + " trapezoids="
                                  + std::to_string(tl.traps.size()) + " tiles="
                                  + std::to_string(tl.tiles.size()));
                    if (tl.inGds != t.inGds) {
                        fail(k + ": the fast reader counts " + std::to_string(tl.inGds)
                             + " shapes, the standard one " + std::to_string(t.inGds));
                    }
                    if (tl.shapes < t.inGds
                        && !contains(notes, preparedNote(id, key.layer, key.datatype,
                                                         fl.place.name, tl.shapes, t.inGds))) {
                        fail(k + ": " + std::to_string(t.inGds - tl.shapes)
                             + " shapes not prepared and no note names them");
                    }
                    if (withCamera) {
                        const std::size_t i = choiceIndex.at(&fl);
                        const LayerBuckets& b = choice.layers[i];
                        out.push_back("buckets " + k + " shape=" + std::to_string(b.shape)
                                      + " coverage=" + std::to_string(b.coverage)
                                      + " outside=" + std::to_string(b.outside)
                                      + " hidden=" + std::to_string(b.hidden)
                                      + " shape_records=" + std::to_string(b.shapeRecords)
                                      + " coverage_tiles=" + std::to_string(b.coverageTiles));
                        out.push_back("row " + k + " " + fl.place.name + ": "
                                      + fastRowLabel(t.inGds, b));
                        if (b.total() != tl.shapes) {
                            fail(k + ": the buckets add up to " + std::to_string(b.total())
                                 + ", " + std::to_string(tl.shapes) + " were prepared");
                        }
                        if (b.coverage > 0) {
                            ++coverageLayers;
                            coverageShapes += b.coverage;
                            coverageOf += tl.inGds;
                        }
                    }
                    continue;
                }
                if (body && key == LayerKey(40, 0)) {
                    out.push_back("body " + k + " gds=" + std::to_string(t.inGds)
                                  + " (the stackup's substrate, one body over the whole die)");
                    continue;
                }
                if (sourced.count(key)) {
                    out.push_back("missing " + k + " gds=" + std::to_string(t.inGds));
                    fail(k + ": placed, but the fast reader returned nothing for it");
                    continue;
                }
                out.push_back("unplaced " + k + " gds=" + std::to_string(t.inGds));
                if (!unmodeled.count(key) || c.note.empty() || !contains(notes, c.note)) {
                    fail(k + ": " + std::to_string(t.inGds)
                         + " shapes placed nowhere and no note names them");
                }
            }
            if (coverageLayers > 0) {
                const std::string line =
                    fastCoverageNote(id, coverageLayers, coverageShapes, coverageOf);
                out.push_back("coverage " + id + " layers=" + std::to_string(coverageLayers)
                              + " shapes=" + std::to_string(coverageShapes)
                              + " of=" + std::to_string(coverageOf));
                if (!contains(notes, line)) {
                    fail(id + ": " + std::to_string(coverageShapes)
                         + " shapes drawn as coverage and the notes do not say so: " + line);
                }
            }
        }
        if (withCamera) {
            // A frame with every layer hidden passes every check above, so
            // say how many layers it could draw.
            const auto shown = std::count_if(inputs.begin(), inputs.end(),
                                             [](const TileLayerInput& in) { return in.visible; });
            out.push_back("frame layers_shown=" + std::to_string(shown) + " of "
                          + std::to_string(inputs.size())
                          + " shape_records=" + std::to_string(choice.shapeRecords)
                          + " coverage_tiles=" + std::to_string(choice.coverageTiles));
        }
        for (const std::string& n : notes) out.push_back("note " + n);
    }

    return finish();
}

} // namespace chiplet
