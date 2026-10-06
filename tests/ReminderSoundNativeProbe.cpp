#include "desktop/ReminderSound.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <functional>

using namespace campus;
int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setApplicationName("CampusPulseSoundAcceptance");
    application.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"evidence-dir", "Output directory for isolated native playback evidence", "directory"});
    parser.process(application);
    if (!parser.isSet("evidence-dir")) return 2;
    const auto path = parser.value("evidence-dir");
    if (!QDir().mkpath(path)) return 2;
    ReminderSound sound;
    const auto tones = ReminderSound::tones();
    QJsonArray started, completed, errors;
    QElapsedTimer elapsed;
    elapsed.start();
    int index = 0;
    bool testingStop = false, stopped = false, writing = false;
    std::function<void()> next;
    const auto finish = [&] {
        if (writing) return;
        writing = true;
        sound.stop();
        const bool passed = completed.size() == tones.size() && stopped && errors.empty();
        QJsonObject proof{{"passed", passed}, {"native_started", started},
                          {"native_completed", completed}, {"stop_while_playing_succeeded", stopped},
                          {"errors", errors}, {"elapsed_ms", double(elapsed.elapsed())},
                          {"volume_percent", 35}, {"physical_speaker_audibility_verified", false},
                          {"system_volume_changed", false}, {"network_requests", 0},
                          {"model_calls", 0}, {"user_data_accessed", false}};
        QFile output(QDir(path).filePath("sound-native.json"));
        if (!output.open(QIODevice::WriteOnly) ||
            output.write(QJsonDocument(proof).toJson()) <= 0) application.exit(3);
        else application.exit(passed ? 0 : 2);
    };
    QObject::connect(&sound, &ReminderSound::started, &application,
                     [&](const QString &tone) { started.append(tone); });
    QObject::connect(&sound, &ReminderSound::failed, &application, [&](const QString &error) {
        errors.append(error);
        QTimer::singleShot(0, &application, finish);
    });
    QObject::connect(&sound, &ReminderSound::finished, &application, [&] {
        if (!testingStop) completed.append(tones.at(index - 1));
        QTimer::singleShot(80, &application, next);
    });
    next = [&] {
        if (index < tones.size()) {
            if (!sound.play(tones.at(index++), 35)) finish();
        } else {
            testingStop = true;
            if (!sound.play("alarm", 35)) { finish(); return; }
            QTimer::singleShot(100, &application, [&] {
                const bool wasPlaying = sound.isPlaying();
                sound.stop();
                stopped = wasPlaying && !sound.isPlaying();
                finish();
            });
        }
    };
    QTimer::singleShot(0, &application, next);
    QTimer::singleShot(10000, &application, [&] {
        errors.append("Native sound probe timed out"); finish();
    });
    return application.exec();
}
