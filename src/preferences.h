#pragma once
#include "batchengine.h"
namespace imagebatch {
struct Preferences {
    Options options;
    BatchSettings batch;
    bool watermarkEnabled = false;
    QString lastFolder;
    Preferences() { batch.workers = 2; }
};
Preferences readPreferences(const QString &file);
bool writePreferences(const QString &file, const Preferences &preferences, QString *error = nullptr);
}
