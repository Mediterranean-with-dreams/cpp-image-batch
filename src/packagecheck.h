#pragma once
#include <QString>
class QApplication;
namespace imagebatch {
class MainWindow;
// Developer-only check of the actual distributed executable.
int runPackageCheck(QApplication &app, MainWindow &window, const QString &reportPath);
}
