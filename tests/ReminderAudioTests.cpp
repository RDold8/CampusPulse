#include "desktop/ReminderAudio.h"
#include "desktop/ReminderSound.h"
#include "desktop/ReminderSoundDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QtTest>
#include <stdexcept>

using namespace campus;

// These tests observe injected playback requests and timers only. They do not
// open an audio device or establish that a person heard a notification sound.
class ReminderAudioTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settingsFolder_;

    QByteArray settingsBytes() const {
        const QSettings settings;
        QFile file(settings.fileName());
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("Isolated test settings cannot be read");
        return file.readAll();
    }

  private slots:
    void initTestCase() {
        QVERIFY(settingsFolder_.isValid());
        QCoreApplication::setOrganizationName("CampusPulseReminderAudioTests");
        QCoreApplication::setApplicationName("IsolatedReminderAudio");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsFolder_.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsFolder_.path());
        const QSettings settings;
        QVERIFY(settings.fileName().startsWith(settingsFolder_.path()));
    }
    void init() {
        QSettings settings;
        settings.setFallbacksEnabled(false);
        settings.clear();
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    void defaultsAreLoadedWithoutCreatingAPlaybackRequest() {
        const auto options = ReminderAudioOptions::load();
        QCOMPARE(options.tone, QString("gentle"));
        QCOMPARE(options.volume, 60);
        QVERIFY(options.repeat);
        QSettings settings;
        QVERIFY(!settings.contains("reminders/soundTone"));
        QVERIFY(!settings.contains("reminders/soundVolume"));
        QVERIFY(!settings.contains("reminders/repeatSound"));
    }
    void preferencesRoundTrip_data() {
        QTest::addColumn<QString>("tone");
        QTest::addColumn<int>("volume");
        QTest::addColumn<bool>("repeat");
        for (const auto &tone : {QString("gentle"), QString("double"), QString("alarm"), QString("mute")})
            for (const auto volume : {0, 100})
                for (const auto repeat : {false, true}) {
                    const auto name = QString("%1-%2-%3").arg(tone).arg(volume).arg(repeat).toUtf8();
                    QTest::newRow(name.constData()) << tone << volume << repeat;
                }
    }
    void preferencesRoundTrip() {
        QFETCH(QString, tone);
        QFETCH(int, volume);
        QFETCH(bool, repeat);
        ReminderAudioOptions{tone, volume, repeat}.save();
        const auto reopened = ReminderAudioOptions::load();
        QCOMPARE(reopened.tone, tone);
        QCOMPARE(reopened.volume, volume);
        QCOMPARE(reopened.repeat, repeat);
        const QSettings settings;
        QCOMPARE(settings.value("reminders/soundTone").toString(), tone);
        QCOMPARE(settings.value("reminders/soundVolume").toInt(), volume);
        QCOMPARE(settings.value("reminders/repeatSound").toBool(), repeat);
    }
    void malformedSavedPreferencesFallBackOrClamp_data() {
        QTest::addColumn<QString>("storedTone");
        QTest::addColumn<QVariant>("storedVolume");
        QTest::addColumn<QString>("expectedTone");
        QTest::addColumn<int>("expectedVolume");
        QTest::newRow("unknown-tone-negative-volume") << QString("not-a-tone") << QVariant(-1)
                                                       << QString("gentle") << 0;
        QTest::newRow("empty-tone-over-range") << QString() << QVariant(101)
                                                << QString("gentle") << 100;
        QTest::newRow("valid-tone-nonnumeric-volume") << QString("alarm") << QVariant("loud")
                                                       << QString("alarm") << 60;
        QTest::newRow("valid-tone-numeric-string") << QString("double") << QVariant("37")
                                                    << QString("double") << 37;
    }
    void malformedSavedPreferencesFallBackOrClamp() {
        QFETCH(QString, storedTone);
        QFETCH(QVariant, storedVolume);
        QFETCH(QString, expectedTone);
        QFETCH(int, expectedVolume);
        {
            QSettings settings;
            settings.setValue("reminders/soundTone", storedTone);
            settings.setValue("reminders/soundVolume", storedVolume);
            settings.setValue("reminders/repeatSound", false);
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
        }
        const auto before = settingsBytes();
        const auto result = ReminderAudioOptions::load();
        QCOMPARE(result.tone, expectedTone);
        QCOMPARE(result.volume, expectedVolume);
        QVERIFY(!result.repeat);
        QCOMPARE(settingsBytes(), before); // Reading must not overwrite a user's saved choices.
    }
    void rejectedPreferencesDoNotPartiallyOverwriteSavedChoices_data() {
        QTest::addColumn<QString>("tone");
        QTest::addColumn<int>("volume");
        QTest::newRow("invalid-tone") << QString("unknown") << 50;
        QTest::newRow("negative-volume") << QString("gentle") << -1;
        QTest::newRow("over-range-volume") << QString("mute") << 101;
    }
    void rejectedPreferencesDoNotPartiallyOverwriteSavedChoices() {
        QFETCH(QString, tone);
        QFETCH(int, volume);
        ReminderAudioOptions{"double", 42, false}.save();
        const auto before = settingsBytes();
        const ReminderAudioOptions invalid{tone, volume, true};
        QVERIFY_EXCEPTION_THROWN(invalid.save(), std::invalid_argument);
        const auto saved = ReminderAudioOptions::load();
        QCOMPARE(saved.tone, QString("double"));
        QCOMPARE(saved.volume, 42);
        QVERIFY(!saved.repeat);
        QCOMPARE(settingsBytes(), before);
    }
    void mutedRequestsNeverInvokePlayback_data() {
        QTest::addColumn<QString>("tone");
        QTest::addColumn<int>("volume");
        QTest::newRow("mute-tone") << QString("mute") << 100;
        QTest::newRow("zero-volume") << QString("alarm") << 0;
    }
    void mutedRequestsNeverInvokePlayback() {
        QFETCH(QString, tone);
        QFETCH(int, volume);
        int playbackCalls = 0;
        ReminderAudio audio([&](const QString &, int) { ++playbackCalls; return true; },
                            [] {}, nullptr, 15, 65);
        audio.begin({tone, volume, true});
        QVERIFY(!audio.repeating());
        QTest::qWait(90);
        QCOMPARE(playbackCalls, 0);
    }
    void oneShotSubmitsTheSelectedToneAndVolumeOnce() {
        QList<QPair<QString, int>> requests;
        ReminderAudio audio([&](const QString &tone, int volume) {
            requests.append(qMakePair(tone, volume));
            return true;
        }, [] {}, nullptr, 15, 65);
        audio.begin({"double", 27, false});
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.front().first, QString("double"));
        QCOMPARE(requests.front().second, 27);
        QVERIFY(!audio.repeating());
        QTest::qWait(90);
        QCOMPARE(requests.size(), 1);
    }
    void repetitionStopsAtItsDeadline() {
        int playbackCalls = 0;
        ReminderAudio audio([&](const QString &tone, int volume) {
            ++playbackCalls;
            return tone == "gentle" && volume == 60;
        }, [] {}, nullptr, 20, 100);
        audio.begin({"gentle", 60, true});
        QCOMPARE(playbackCalls, 1);
        QVERIFY(audio.repeating());
        QTRY_VERIFY_WITH_TIMEOUT(playbackCalls >= 2, 250);
        QTRY_VERIFY_WITH_TIMEOUT(!audio.repeating(), 300);
        const auto endedAt = playbackCalls;
        QTest::qWait(70);
        QCOMPARE(playbackCalls, endedAt);
    }
    void explicitStopCancelsAllFutureRequestsAndIsIdempotent() {
        int playbackCalls = 0;
        ReminderAudio audio([&](const QString &, int) { ++playbackCalls; return true; },
                            [] {}, nullptr, 20, 200);
        audio.begin({"alarm", 83, true});
        QTRY_VERIFY_WITH_TIMEOUT(playbackCalls >= 2, 200);
        audio.stop();
        QVERIFY(!audio.repeating());
        const auto stoppedAt = playbackCalls;
        audio.stop();
        audio.stop();
        QTest::qWait(90);
        QCOMPARE(playbackCalls, stoppedAt);
        QVERIFY(!audio.repeating());
    }
    void failedInitialPlaybackStopsWithoutRetrying() {
        int playbackCalls = 0;
        ReminderAudio audio([&](const QString &, int) { ++playbackCalls; return false; },
                            [] {}, nullptr, 15, 65);
        audio.begin({"alarm", 60, true});
        QCOMPARE(playbackCalls, 1);
        QVERIFY(!audio.repeating());
        QTest::qWait(90);
        QCOMPARE(playbackCalls, 1);
    }
    void failedLaterPlaybackAlsoStopsWithoutRetrying() {
        int playbackCalls = 0;
        ReminderAudio audio([&](const QString &, int) { return ++playbackCalls == 1; },
                            [] {}, nullptr, 20, 150);
        audio.begin({"gentle", 60, true});
        QVERIFY(audio.repeating());
        QTRY_COMPARE_WITH_TIMEOUT(playbackCalls, 2, 200);
        QVERIFY(!audio.repeating());
        QTest::qWait(90);
        QCOMPARE(playbackCalls, 2);
    }
    void severalBeginsKeepOnlyOneRepeatingSequence() {
        int playbackCalls = 0;
        QList<QString> requestedTones;
        ReminderAudio audio([&](const QString &tone, int) {
            ++playbackCalls;
            requestedTones.append(tone);
            return true;
        }, [] {}, nullptr, 20, 130);
        for (int i = 0; i < 5; ++i)
            audio.begin({i == 4 ? "alarm" : "gentle", 60, true});
        QCOMPARE(playbackCalls, 5);
        QTest::qWait(75);
        // A stack of five repeating timers would issue far more than five
        // further calls during this window. Allow normal event-loop jitter.
        QVERIFY(playbackCalls >= 6);
        QVERIFY(playbackCalls <= 10);
        for (int i = 5; i < requestedTones.size(); ++i)
            QCOMPARE(requestedTones.at(i), QString("alarm"));
        QTRY_VERIFY_WITH_TIMEOUT(!audio.repeating(), 300);
        const auto endedAt = playbackCalls;
        QTest::qWait(60);
        QCOMPARE(playbackCalls, endedAt);
    }
    void restartingAlsoResetsTheDeadlineAndOneShotCancelsRepeating() {
        int playbackCalls = 0;
        ReminderAudio audio([&](const QString &, int) { ++playbackCalls; return true; },
                            [] {}, nullptr, 20, 160);
        audio.begin({"gentle", 60, true});
        QTest::qWait(100);
        audio.begin({"double", 35, true});
        QTest::qWait(90); // The first sequence's deadline has passed; the second has not.
        QVERIFY(audio.repeating());
        audio.begin({"alarm", 90, false});
        QVERIFY(!audio.repeating());
        const auto stoppedAt = playbackCalls;
        QTest::qWait(190);
        QCOMPARE(playbackCalls, stoppedAt);
    }
    void invalidNewOptionsCancelTheOldSequenceWithoutPlaying() {
        int playbackCalls = 0;
        ReminderAudio audio([&](const QString &, int) { ++playbackCalls; return true; },
                            [] {}, nullptr, 20, 100);
        audio.begin({"gentle", 60, true});
        const ReminderAudioOptions invalid{"unknown", 60, true};
        QVERIFY_EXCEPTION_THROWN(audio.begin(invalid), std::invalid_argument);
        QVERIFY(!audio.repeating());
        QTest::qWait(120);
        QCOMPARE(playbackCalls, 1);
    }
    void dialogSavesTheSamePreferencesUsedByTheAudioController() {
        ReminderAudioOptions{"alarm", 77, true}.save();
        ReminderSoundDialog dialog;
        auto *tone = dialog.findChild<QComboBox *>("reminderSoundTone");
        auto *volume = dialog.findChild<QSlider *>("reminderSoundVolume");
        auto *repeat = dialog.findChild<QCheckBox *>("reminderSoundRepeat");
        auto *save = dialog.findChild<QPushButton *>("saveReminderSoundButton");
        QVERIFY(tone);
        QVERIFY(volume);
        QVERIFY(repeat);
        QVERIFY(save);
        QCOMPARE(tone->currentData().toString(), QString("alarm"));
        QCOMPARE(volume->value(), 77);
        QVERIFY(repeat->isChecked());
        tone->setCurrentIndex(tone->findData("double"));
        volume->setValue(85);
        repeat->setChecked(false);
        QSignalSpy saved(&dialog, &ReminderSoundDialog::settingsSaved);
        save->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(saved.count(), 1);
        const auto reopened = ReminderAudioOptions::load();
        QCOMPARE(reopened.tone, QString("double"));
        QCOMPARE(reopened.volume, 85);
        QVERIFY(!reopened.repeat);
        QCOMPARE(dialog.options().tone, reopened.tone);
        QCOMPARE(dialog.options().volume, reopened.volume);
        QCOMPARE(dialog.options().repeat, reopened.repeat);
    }
    void cancellingTheDialogLeavesSavedPreferencesUntouched() {
        ReminderAudioOptions{"gentle", 23, false}.save();
        const auto before = settingsBytes();
        ReminderSoundDialog dialog;
        auto *tone = dialog.findChild<QComboBox *>("reminderSoundTone");
        auto *volume = dialog.findChild<QSlider *>("reminderSoundVolume");
        auto *repeat = dialog.findChild<QCheckBox *>("reminderSoundRepeat");
        auto *cancel = dialog.findChild<QPushButton *>("cancelReminderSoundButton");
        QVERIFY(tone);
        QVERIFY(volume);
        QVERIFY(repeat);
        QVERIFY(cancel);
        tone->setCurrentIndex(tone->findData("mute"));
        volume->setValue(0);
        repeat->setChecked(true);
        QSignalSpy saved(&dialog, &ReminderSoundDialog::settingsSaved);
        cancel->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(saved.count(), 0);
        const auto reopened = ReminderAudioOptions::load();
        QCOMPARE(reopened.tone, QString("gentle"));
        QCOMPARE(reopened.volume, 23);
        QVERIFY(!reopened.repeat);
        QCOMPARE(settingsBytes(), before);
    }
    void openingAndChangingOptionsDoesNotAutoplay() {
        ReminderSoundDialog dialog;
        auto *player = dialog.findChild<ReminderSound *>();
        auto *tone = dialog.findChild<QComboBox *>("reminderSoundTone");
        auto *volume = dialog.findChild<QSlider *>("reminderSoundVolume");
        auto *repeat = dialog.findChild<QCheckBox *>("reminderSoundRepeat");
        QVERIFY(player);
        QVERIFY(tone);
        QVERIFY(volume);
        QVERIFY(repeat);
        QVERIFY(!player->isPlaying());
        QSignalSpy started(player, &ReminderSound::started);
        QSignalSpy failed(player, &ReminderSound::failed);
        tone->setCurrentIndex(tone->findData("alarm"));
        volume->setValue(100);
        repeat->setChecked(false);
        QTest::qWait(30);
        QCOMPARE(started.count(), 0);
        QCOMPARE(failed.count(), 0);
        QVERIFY(!player->isPlaying());
        const auto saved = ReminderAudioOptions::load();
        QCOMPARE(saved.tone, QString("gentle"));
        QCOMPARE(saved.volume, 60);
        QVERIFY(saved.repeat);
    }
    void mutePreviewGivesFeedbackWithoutStartingPlaybackOrSaving() {
        ReminderAudioOptions{"gentle", 42, false}.save();
        const auto before = settingsBytes();
        ReminderSoundDialog dialog;
        auto *tone = dialog.findChild<QComboBox *>("reminderSoundTone");
        auto *volume = dialog.findChild<QSlider *>("reminderSoundVolume");
        auto *preview = dialog.findChild<QPushButton *>("previewReminderSoundButton");
        auto *stop = dialog.findChild<QPushButton *>("stopReminderSoundButton");
        auto *status = dialog.findChild<QLabel *>("reminderSoundStatus");
        auto *player = dialog.findChild<ReminderSound *>();
        QVERIFY(tone);
        QVERIFY(volume);
        QVERIFY(preview);
        QVERIFY(stop);
        QVERIFY(status);
        QVERIFY(player);
        tone->setCurrentIndex(tone->findData("mute"));
        volume->setValue(100);
        QSignalSpy started(player, &ReminderSound::started);
        QSignalSpy failed(player, &ReminderSound::failed);
        preview->click(); // Only the mute branch is exercised, never a real audible preview.
        QVERIFY(status->text().contains("当前为静音"));
        QVERIFY(!stop->isEnabled());
        QVERIFY(!player->isPlaying());
        QCOMPARE(started.count(), 0);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(settingsBytes(), before);
    }
};
QTEST_MAIN(ReminderAudioTests)
#include "ReminderAudioTests.moc"
