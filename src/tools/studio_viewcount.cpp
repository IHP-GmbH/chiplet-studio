// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * studio-viewcount - What the 3D view draws for a .chiplet, counted
 *
 *   studio-viewcount [--shapes standard|fast|every]
 *                    [--camera fit|mid|close | --eye X,Y,Z --target X,Y,Z]
 *                    [--viewport WxH] [--fill] [--detailed] [--cell ID=NAME]...
 *                    [--mode ID=MODE]... [--png OUT.png] FILE.chiplet
 *
 * Loads FILE the way the studio opens it and prints, per component and
 * layer, the GDS shapes and where each one went. Components open solid or
 * transparent, which draws no layers; --detailed sets every component with
 * a layout to Detailed first, as the hierarchy panel would. --mode sets one
 * component's render mode after that (hidden, wireframe, transparent, solid,
 * detailed or detailed-no-si); it is how a frame shows a mode the hierarchy
 * panel does not offer, such as solid. With a camera
 * it also prints the eye, the boxes of the scene and of each component's
 * layers and body with whether the eye is inside each, the per-frame
 * buckets and the notes strip.
 * --png also draws that camera's view with the studio's own 3D view, which
 * needs a display, and prints the median of five timed frames (render and
 * read back, after one untimed frame).
 * Exit 0 when every shape is accounted for, 1 when a check or the capture
 * fails, 2 on bad usage or a file that does not load.
 */

#include "core/Assembly.h"
#include "core/Component.h"
#include "formats/ChipletFormat.h"
#include "view3d/AssemblyView.h"
#include "view3d/GDSAnalyzer.h"
#include "view3d/ViewCount.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QSurfaceFormat>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace chiplet;

namespace {

const char* const kUsage =
    "usage: studio-viewcount [--shapes standard|fast|every]\n"
    "                        [--camera fit|mid|close | --eye X,Y,Z --target X,Y,Z]\n"
    "                        [--viewport WxH] [--fill] [--detailed] [--cell ID=NAME]...\n"
    "                        [--mode ID=MODE]... [--png OUT.png] FILE.chiplet\n"
    "MODE: hidden, wireframe, transparent, solid, detailed or detailed-no-si\n";

void quietDebug(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    if (type == QtDebugMsg || type == QtInfoMsg) return;
    std::fprintf(stderr, "%s\n", qPrintable(msg));
}

bool parseTriple(const std::string& text, std::array<float, 3>& out)
{
    std::istringstream in(text);
    char c1 = 0, c2 = 0;
    in >> out[0] >> c1 >> out[1] >> c2 >> out[2];
    return in && c1 == ',' && c2 == ',' && in.peek() == std::char_traits<char>::eof();
}

bool parseViewport(const std::string& text, int& w, int& h)
{
    std::istringstream in(text);
    char x = 0;
    in >> w >> x >> h;
    return in && (x == 'x' || x == 'X') && w > 0 && h > 0
        && in.peek() == std::char_traits<char>::eof();
}

// The studio's choice of top cell when the file names none, without its
// dialog: a cancelled dialog's default where it would ask.
void selectCells(Assembly& assembly, const std::map<std::string, std::string>& given)
{
    GDSAnalyzer analyzer;
    for (auto& comp : assembly.components()) {
        if (auto it = given.find(comp->id()); it != given.end()) {
            comp->set_top_cell(it->second);
            continue;
        }
        if (comp->layout_path().empty() || !comp->cells().empty()) continue;
        const auto cells = analyzer.analyzeCells(comp->layout_path());
        if (cells.empty()) continue;
        if (cells.size() == 1) {
            comp->set_top_cell(cells[0].name);
            continue;
        }
        const bool flat = analyzer.isFlatGDS(comp->layout_path());
        std::vector<std::string> top;
        for (const auto& cell : cells) {
            if (cell.isTopCandidate || flat) top.push_back(cell.name);
        }
        if (top.empty()) continue;
        if (top.size() > 1) {
            std::fprintf(stderr, "%s: %zu top cells, using %s (--cell %s=NAME to choose)\n",
                         comp->id().c_str(), top.size(), top[0].c_str(), comp->id().c_str());
        }
        comp->set_cells(top);
    }
}

// Wait in the event loop until `done`, or `seconds` have passed.
template <typename Done>
bool waitFor(Done done, int seconds)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > seconds * 1000LL) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return true;
}

// Draw `camera` with the studio's 3D view at the options' viewport and
// setting, save it to `png`, and print the frame time.
bool capture(Assembly& assembly, const ViewCountOptions& options, const Camera& camera,
             const std::string& png)
{
    // Shown and initialised before the assembly is set, as the studio opens a
    // file: the build then places the base plane under the scene.
    AssemblyView view;
    view.setShapeMode(options.mode);
    view.resize(options.width, options.height);
    view.show();
    if (!waitFor([&view]() { return view.isValid(); }, 60)) {
        std::cerr << "png: no GL context\n";
        return false;
    }
    view.setAssembly(&assembly);
    view.setShowFill(options.fill);  // the load turned fill off
    // Standard reads only fill, and only with fill on.
    auto read = [&view, &options]() {
        return options.mode == ShapeMode::Standard ? !options.fill || view.fillPrepared()
                                                   : view.shapesPrepared();
    };
    if (!waitFor(read, 3600)) {
        std::cerr << "png: the shapes were not read within an hour\n";
        return false;
    }
    view.setCamera(camera);

    const QSize size(options.width, options.height);
    QImage image = view.renderToImage(size, false, 0);  // uploads; not timed
    std::vector<double> ms;
    for (int i = 0; i < 5 && !image.isNull(); ++i) {
        QElapsedTimer t;
        t.start();
        image = view.renderToImage(size, false, 0);
        ms.push_back(t.nsecsElapsed() * 1e-6);
    }
    if (image.isNull() || !image.save(QString::fromStdString(png))) {
        std::cerr << "png: " << png << " was not drawn or not written\n";
        return false;
    }
    std::sort(ms.begin(), ms.end());
    char line[160];
    std::snprintf(line, sizeof line, "png %s; frame %.1f ms (median of 5; min %.1f, max %.1f)",
                  png.c_str(), ms[2], ms.front(), ms.back());
    std::cout << line << "\n";
    if (options.mode != ShapeMode::Standard && !view.shapesPrepared()) {
        std::cout << "png: the standard view was drawn\n";
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    qInstallMessageHandler(quietDebug);
    // The capture draws with the studio's view, which needs widgets and the
    // studio's GL format; the counts alone need neither.
    bool gui = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--png") gui = true;
    }
    std::unique_ptr<QCoreApplication> appHolder;
    if (gui) {
        QSurfaceFormat fmt;
        fmt.setVersion(3, 3);
        fmt.setProfile(QSurfaceFormat::CoreProfile);
        fmt.setDepthBufferSize(24);
        fmt.setStencilBufferSize(8);
        QSurfaceFormat::setDefaultFormat(fmt);
        appHolder = std::make_unique<QApplication>(argc, argv);
    } else {
        appHolder = std::make_unique<QCoreApplication>(argc, argv);
    }
    QCoreApplication& app = *appHolder;

    ViewCountOptions options;
    std::map<std::string, std::string> cells, modes;
    std::string path, png;
    bool eye = false, target = false, detailedAll = false;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const std::string a = args[i].toStdString();
        auto value = [&](std::string& out) {
            if (i + 1 >= args.size()) return false;
            out = args[++i].toStdString();
            return true;
        };
        std::string v;
        if (a == "--shapes" && value(v)) {
            if (v == "standard") options.mode = ShapeMode::Standard;
            else if (v == "fast") options.mode = ShapeMode::Fast;
            else if (v == "every") options.mode = ShapeMode::Every;
            else { std::cerr << "unknown --shapes " << v << "\n" << kUsage; return 2; }
        } else if (a == "--camera" && value(v)) {
            if (v == "fit") options.camera = ViewCountCamera::Fit;
            else if (v == "mid") options.camera = ViewCountCamera::Mid;
            else if (v == "close") options.camera = ViewCountCamera::Close;
            else { std::cerr << "unknown --camera " << v << "\n" << kUsage; return 2; }
        } else if (a == "--eye" && value(v) && parseTriple(v, options.eye)) {
            eye = true;
        } else if (a == "--target" && value(v) && parseTriple(v, options.target)) {
            target = true;
        } else if (a == "--viewport" && value(v)
                   && parseViewport(v, options.width, options.height)) {
        } else if (a == "--fill") {
            options.fill = true;
        } else if (a == "--detailed") {
            detailedAll = true;
        } else if (a == "--png" && value(v) && !v.empty()) {
            png = v;
        } else if (a == "--cell" && value(v) && v.find('=') != std::string::npos) {
            cells[v.substr(0, v.find('='))] = v.substr(v.find('=') + 1);
        } else if (a == "--mode" && value(v) && v.find('=') != std::string::npos) {
            modes[v.substr(0, v.find('='))] = v.substr(v.find('=') + 1);
        } else if (!a.empty() && a[0] != '-' && path.empty()) {
            path = a;
        } else {
            std::cerr << "bad argument " << a << "\n" << kUsage;
            return 2;
        }
    }
    if (path.empty() || eye != target
        || (eye && options.camera != ViewCountCamera::None)
        || (!png.empty() && !eye && options.camera == ViewCountCamera::None)) {
        std::cerr << kUsage;
        return 2;
    }
    if (eye) options.camera = ViewCountCamera::Explicit;

    std::unique_ptr<Assembly> assembly;
    try {
        assembly = ChipletFormat().load(path);
    } catch (const std::exception& e) {
        std::cerr << path << ": " << e.what() << "\n";
        return 2;
    }
    if (!assembly) {
        std::cerr << path << ": did not load\n";
        return 2;
    }
    selectCells(*assembly, cells);
    if (detailedAll) {
        for (auto& comp : assembly->components()) {
            if (!comp->layout_path().empty()) comp->set_render_mode(RenderMode::Detailed);
        }
    }
    std::string modeError;
    if (!setRenderModes(*assembly, modes, modeError)) {
        std::cerr << "--mode: " << modeError << "\n" << kUsage;
        return 2;
    }

    // The scene is loaded once: the capture's camera is placed in it before
    // the count takes it.
    const LayerSheets sheets = loadLayerSheets(*assembly);
    StandardScene scene = loadStandardScene(*assembly, sheets.props, {});
    Camera camera;
    std::string cameraError;
    const bool placed = png.empty()
                     || viewCountCamera(*assembly, scene, options, camera, cameraError);
    const ViewCountReport report = viewCountScene(*assembly, std::move(scene), sheets.notes,
                                                  options);
    if (detailedAll) std::cout << "render every component with a layout: Detailed\n";
    for (const auto& [id, name] : modes) std::cout << "render " << id << ": " << name << "\n";
    for (const std::string& line : report.lines) std::cout << line << "\n";
    bool captured = true;
    if (!png.empty() && !placed) {
        std::cerr << "png: " << cameraError << "\n";
        captured = false;
    } else if (!png.empty()) {
        captured = capture(*assembly, options, camera, png);
    }
    return report.failures.empty() && captured ? 0 : 1;
}
