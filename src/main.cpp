/**
 * Chiplet Studio - 3D Chiplet Assembly Design Tool
 *
 * Main entry point.
 */

#include <QApplication>
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Chiplet Studio");
    app.setApplicationVersion("0.1.0");

    MainWindow window;
    window.show();

    return app.exec();
}
