#include "mainwindow.h"
#include "packagecheck.h"
#include <QApplication>
#include <QImageReader>
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("ImageBatch");
    QCoreApplication::setApplicationVersion("1.0.0");
    QImageReader::setAllocationLimit(256);
    imagebatch::MainWindow window;
    window.show();
    const int diagnostic = app.arguments().indexOf("--self-test");
    if (diagnostic >= 0) {
        if (diagnostic + 1 >= app.arguments().size()) return 2;
        return imagebatch::runPackageCheck(app, window, app.arguments()[diagnostic + 1]);
    }
    return app.exec();
}
