#include "mainwindow.h"
#include "packagecheck.h"
#include <QApplication>
#include <QImageReader>
#include <QThreadPool>
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("ImageBatch");
    QCoreApplication::setApplicationVersion(IMAGEBATCH_VERSION);
    QImageReader::setAllocationLimit(256);
    QThreadPool::globalInstance()->setMaxThreadCount(1);
    imagebatch::MainWindow window;
    window.show();
    const int diagnostic = app.arguments().indexOf("--self-test");
    if (diagnostic >= 0) {
        if (diagnostic + 1 >= app.arguments().size()) return 2;
        return imagebatch::runPackageCheck(app, window, app.arguments()[diagnostic + 1]);
    }
    return app.exec();
}
