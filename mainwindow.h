#pragma once

#include "boonton4200.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QTranslator>

#include <deque>
#include <functional>
#include <vector>

class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QSpinBox;
class QwtPlot;
class QwtPlotCurve;
class QwtPlotGrid;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    struct Point
    {
        Reading reading;
        double seconds;
    };

    template<typename Object, typename Setter>
    Object *localized(Object *object, const char *text, Setter setter)
    {
        textUpdates.emplace_back([object, text, setter] { (object->*setter)(tr(text)); });
        textUpdates.back()();
        return object;
    }

    template<typename Object>
    Object *localized(Object *object, const char *text)
    {
        return localized(object, text, &Object::setText);
    }

    void createMenus();
    void installLanguage(const QString &code);
    void changeLanguage(const QString &code);
    QWidget *createMeter();
    QWidget *createPlot();
    QWidget *createControls();
    void loadSettings();
    void editConnection();
    void saveSettings();
    void setDarkTheme(bool dark);
    void showAbout();
    void checkForUpdates();
    void checkPrivateRelease();
    void finishUpdateCheck(const QByteArray &data, const QString &error = {});
    void start(Operation operation, int channel = 1);
    void finish(const InstrumentResult &result);
    void showReading(const Reading &reading);
    void updateMeter();
    void updatePlot();
    void clearHistory();
    void saveCsv();
    void confirmAdjustment(Operation operation);
    void updateControls();
    void setStatus(std::function<QString()> text, bool error = false);
    QString connectionText() const;

    QSettings preferences;
    QTranslator appTranslator;
    QTranslator qtTranslator;
    QString language;
    std::vector<std::function<void()>> textUpdates;
    std::function<QString()> statusText;
    std::optional<Reading> displayedReading;
    bool measurementUnavailable = false;
    GpibTarget target;
    bool darkTheme = true;
    bool secondChannel = false;
    bool connected = false;
    bool busy = false;
    bool closing = false;
    Operation active = Operation::Connect;
    QThread instrumentThread;
    Boonton4200 *const instrument;
    QTimer refreshTimer;
    QElapsedTimer historyClock;
    std::deque<Point> history;

    QMenu *languageMenu = nullptr;
    QAction *updateAction = nullptr;
    QAction *connectionSettings = nullptr;
    QAction *saveCsvAction = nullptr;
    QAction *clearHistoryAction = nullptr;
    QLabel *connectionState = nullptr;
    QLabel *addressLabel = nullptr;
    QLabel *valueLabel = nullptr;
    QLabel *unitLabel = nullptr;
    QLabel *measurementInfo = nullptr;
    QLabel *pointCount = nullptr;
    QLabel *statusLabel = nullptr;
    QPushButton *connectButton = nullptr;
    QWidget *operationControls = nullptr;
    QWidget *channelRow = nullptr;
    QComboBox *channelBox = nullptr;
    QCheckBox *autoRefresh = nullptr;
    QSpinBox *interval = nullptr;
    QwtPlot *plot = nullptr;
    QwtPlotGrid *grid = nullptr;
    QwtPlotCurve *curve = nullptr;
};
