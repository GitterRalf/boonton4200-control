#include "mainwindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMovie>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QStyle>
#include <QTextStream>
#include <QVBoxLayout>

#include <qwt_plot.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_scale_widget.h>
#include <qwt_symbol.h>

#include <algorithm>
#include <array>
#include <utility>

namespace {

constexpr std::size_t historyCapacity = 1000;

QLabel *label(const QString &text, const char *name)
{
    auto *widget = new QLabel(text);
    widget->setObjectName(QString::fromLatin1(name));
    widget->setTextFormat(Qt::PlainText);
    return widget;
}

QFrame *card()
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("card"));
    return frame;
}

QIcon globeIcon(bool dark)
{
    QPixmap icon(24, 24);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(dark ? "#d4e3f5" : "#27425a"), 1.35));
    painter.drawEllipse(QRectF(3, 3, 18, 18));
    painter.drawEllipse(QRectF(8, 3, 8, 18));
    painter.drawLine(QPointF(3, 12), QPointF(21, 12));
    painter.drawLine(QPointF(5, 7.5), QPointF(19, 7.5));
    painter.drawLine(QPointF(5, 16.5), QPointF(19, 16.5));
    return QIcon(icon);
}

QString channelName(int channel)
{
    return channel == 3 ? MainWindow::tr("Differenz 1–2") : MainWindow::tr("Kanal %1").arg(channel);
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), instrument(new Boonton4200)
{
    setWindowTitle(QStringLiteral("Boonton 4200 Control"));
    resize(1040, 690);
    setMinimumSize(820, 560);
    loadSettings();
    installLanguage(preferences.value(QStringLiteral("language"), QStringLiteral("de")).toString());
    createMenus();

    auto *workspace = new QWidget;
    workspace->setObjectName(QStringLiteral("workspace"));
    setCentralWidget(workspace);
    auto *layout = new QVBoxLayout(workspace);
    layout->setContentsMargins(22, 16, 22, 16);
    layout->setSpacing(12);

    auto *header = new QHBoxLayout;
    auto *heading = new QVBoxLayout;
    heading->setSpacing(2);
    heading->addWidget(label(QStringLiteral("Boonton 4200"), "title"));
    heading->addWidget(localized(label({}, "quiet"), QT_TR_NOOP("HF-Leistungsmesser")));
    header->addLayout(heading);
    header->addStretch();
    connectionState = label({}, "linkState");
    header->addWidget(connectionState, 0, Qt::AlignVCenter);
    connectButton = new QPushButton;
    connectButton->setObjectName(QStringLiteral("connectDevice"));
    connectButton->setMinimumWidth(118);
    header->addWidget(connectButton);
    layout->addLayout(header);
    connect(connectButton, &QPushButton::clicked, this, [this] {
        start(connected ? Operation::Disconnect : Operation::Connect);
    });

    auto *panels = new QHBoxLayout;
    panels->setSpacing(12);
    auto *measurement = new QVBoxLayout;
    measurement->setSpacing(12);
    measurement->addWidget(createMeter());
    measurement->addWidget(createPlot(), 1);
    panels->addLayout(measurement, 1);
    panels->addWidget(createControls());
    layout->addLayout(panels, 1);

    statusLabel = label(QString(), "status");
    statusLabel->setWordWrap(true);
    layout->addWidget(statusLabel);
    setStatus([this] { return tr("Bereit · %1").arg(connectionText()); });
    updateMeter();
    setDarkTheme(darkTheme);

    refreshTimer.setSingleShot(true);
    connect(&refreshTimer, &QTimer::timeout, this, [this] { start(Operation::Read); });
    qRegisterMetaType<InstrumentResult>();
    instrument->moveToThread(&instrumentThread);
    connect(&instrumentThread, &QThread::finished, instrument, &QObject::deleteLater);
    connect(instrument, &Boonton4200::finished, this, &MainWindow::finish);
    instrumentThread.start();
    updateControls();
}

MainWindow::~MainWindow()
{
    refreshTimer.stop();
    if (instrumentThread.isRunning()) {
        QMetaObject::invokeMethod(instrument, [worker = instrument] {
            worker->execute(Operation::Disconnect);
        }, Qt::BlockingQueuedConnection);
        instrumentThread.quit();
        instrumentThread.wait();
    }
    qApp->removeTranslator(&appTranslator);
    qApp->removeTranslator(&qtTranslator);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (busy || connected) {
        event->ignore();
        closing = true;
        refreshTimer.stop();
        if (!busy) {
            start(Operation::Disconnect);
        }
        return;
    }
    event->accept();
}

void MainWindow::createMenus()
{
    auto *bar = menuBar();
    bar->setNativeMenuBar(false);
    auto *file = localized(bar->addMenu(QString()), QT_TR_NOOP("&Datei"), &QMenu::setTitle);
    saveCsvAction = localized(file->addAction(QString(), this, &MainWindow::saveCsv),
                              QT_TR_NOOP("Messwerte als CSV speichern …"));
    saveCsvAction->setObjectName(QStringLiteral("saveCsv"));
    clearHistoryAction = localized(file->addAction(QString(), this, &MainWindow::clearHistory),
                                   QT_TR_NOOP("Verlauf leeren"));
    file->addSeparator();
    localized(file->addAction(QString(), this, &QWidget::close, QKeySequence::Quit), QT_TR_NOOP("&Beenden"));

    auto *settings = localized(bar->addMenu(QString()), QT_TR_NOOP("&Einstellungen"), &QMenu::setTitle);
    settings->setObjectName(QStringLiteral("settingsMenu"));
    connectionSettings = localized(settings->addAction(QString(), this, &MainWindow::editConnection),
                                   QT_TR_NOOP("Verbindung …"));
    connectionSettings->setObjectName(QStringLiteral("connectionSettings"));
    auto *appearance = localized(settings->addMenu(QString()), QT_TR_NOOP("Darstellung"), &QMenu::setTitle);
    auto *themes = new QActionGroup(appearance);
    for (bool dark : {false, true}) {
        auto *action = localized(appearance->addAction(QString()), dark ? QT_TR_NOOP("Dunkel") : QT_TR_NOOP("Hell"));
        action->setObjectName(dark ? QStringLiteral("darkTheme") : QStringLiteral("lightTheme"));
        action->setCheckable(true);
        action->setChecked(dark == darkTheme);
        themes->addAction(action);
        connect(action, &QAction::triggered, this, [this, dark] { setDarkTheme(dark); });
    }
    settings->addSeparator();
    auto *save = localized(settings->addAction(QString(), this, &MainWindow::saveSettings, QKeySequence::Save),
                           QT_TR_NOOP("Einstellungen speichern"));
    save->setObjectName(QStringLiteral("saveSettings"));

    auto *right = new QMenuBar(bar);
    right->setNativeMenuBar(false);
    languageMenu = new QMenu(right);
    languageMenu->setObjectName(QStringLiteral("languageMenu"));
    languageMenu->setIcon(globeIcon(darkTheme));
    right->addMenu(languageMenu);
    localized(languageMenu->menuAction(), QT_TR_NOOP("Sprache wählen"), &QAction::setToolTip);
    const std::array choices{
        std::pair{QStringLiteral("de"), QStringLiteral("Deutsch")},
        std::pair{QStringLiteral("en"), QStringLiteral("English")},
        std::pair{QStringLiteral("fr"), QStringLiteral("Français")}
    };
    auto *languages = new QActionGroup(languageMenu);
    languages->setExclusive(true);
    for (const auto &[code, caption] : choices) {
        auto *action = languageMenu->addAction(caption);
        action->setObjectName(QStringLiteral("language-") + code);
        action->setCheckable(true);
        action->setChecked(code == language);
        languages->addAction(action);
        connect(action, &QAction::triggered, this, [this, code] { changeLanguage(code); });
    }

    auto *about = localized(right->addMenu(QString()), QT_TR_NOOP("Über"), &QMenu::setTitle);
    about->setObjectName(QStringLiteral("aboutMenu"));
#ifdef BOONTON_QT_COMMUNITY
    localized(about->addAction(QString(), this, [this] { QMessageBox::aboutQt(this, tr("Über Qt")); }),
              QT_TR_NOOP("Über Qt"));
#endif
    localized(about->addAction(QString(), this, &MainWindow::showAbout), QT_TR_NOOP("Über"));
    about->addSeparator();
    updateAction = localized(about->addAction(QString(), this, &MainWindow::checkForUpdates),
                             QT_TR_NOOP("Nach Updates suchen"), &QAction::setText);
    updateAction->setObjectName("checkForUpdates");
    bar->setCornerWidget(right, Qt::TopRightCorner);
}

void MainWindow::installLanguage(const QString &code)
{
    language = code == QStringLiteral("en") || code == QStringLiteral("fr") ? code : QStringLiteral("de");
    const QLocale selected(language);
    QLocale::setDefault(selected);
    setLocale(selected);

    qApp->removeTranslator(&appTranslator);
    qApp->removeTranslator(&qtTranslator);
    if (qtTranslator.load(QStringLiteral(":/qt-i18n/qtbase_") + language)) {
        qApp->installTranslator(&qtTranslator);
    }
    if (appTranslator.load(QStringLiteral(":/i18n/boonton4200_") + language)) {
        qApp->installTranslator(&appTranslator);
    }
}

void MainWindow::changeLanguage(const QString &code)
{
    if (code == language) {
        return;
    }
    installLanguage(code);
    preferences.setValue(QStringLiteral("language"), language);

    for (const auto &update : textUpdates) {
        update();
    }
    for (int index = 0; index < channelBox->count(); ++index) {
        channelBox->setItemText(index, channelName(channelBox->itemData(index).toInt()));
    }
    plot->setAxisTitle(QwtAxis::XBottom, tr("Zeit seit Beginn [s]"));
    updateMeter();
    updatePlot();
    statusLabel->setText(statusText());
    updateControls();
}

QWidget *MainWindow::createMeter()
{
    auto *meter = card();
    meter->setObjectName(QStringLiteral("meter"));
    auto *layout = new QVBoxLayout(meter);
    layout->setContentsMargins(18, 12, 18, 12);
    layout->setSpacing(4);
    layout->addWidget(localized(label({}, "sectionTitle"), QT_TR_NOOP("Aktueller Messwert")));

    auto *valueRow = new QHBoxLayout;
    valueRow->setSpacing(10);
    valueLabel = label(QStringLiteral("—"), "measurementValue");
    valueLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    unitLabel = label(QString(), "measurementUnit");
    valueRow->addWidget(valueLabel, 0, Qt::AlignBaseline);
    valueRow->addWidget(unitLabel, 0, Qt::AlignBaseline);
    valueRow->addStretch();
    layout->addLayout(valueRow);

    measurementInfo = label({}, "quiet");
    measurementInfo->setObjectName(QStringLiteral("measurementInfo"));
    measurementInfo->setWordWrap(true);
    layout->addWidget(measurementInfo);
    return meter;
}

QWidget *MainWindow::createPlot()
{
    auto *frame = card();
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 12, 14, 10);
    layout->setSpacing(8);
    auto *heading = new QHBoxLayout;
    heading->addWidget(localized(label({}, "sectionTitle"), QT_TR_NOOP("Messverlauf")));
    heading->addStretch();
    pointCount = label(QString(), "quiet");
    heading->addWidget(pointCount);
    layout->addLayout(heading);

    plot = new QwtPlot;
    plot->setObjectName(QStringLiteral("historyPlot"));
    plot->setAutoFillBackground(true);
    if (auto *canvas = qobject_cast<QwtPlotCanvas *>(plot->canvas())) {
        canvas->setFrameStyle(QFrame::NoFrame);
    }
    plot->setAxisTitle(QwtAxis::XBottom, tr("Zeit seit Beginn [s]"));
    plot->setAxisMaxMajor(QwtAxis::XBottom, 7);
    plot->setAxisMaxMajor(QwtAxis::YLeft, 6);
    grid = new QwtPlotGrid;
    grid->attach(plot);
    curve = new QwtPlotCurve;
    curve->setRenderHint(QwtPlotItem::RenderAntialiased);
    curve->attach(plot);
    layout->addWidget(plot, 1);
    updatePlot();
    return frame;
}

QWidget *MainWindow::createControls()
{
    auto *side = new QWidget;
    side->setFixedWidth(286);
    auto *layout = new QVBoxLayout(side);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    operationControls = new QWidget;
    auto *operations = new QVBoxLayout(operationControls);
    operations->setContentsMargins(0, 0, 0, 0);
    operations->setSpacing(12);
    layout->addWidget(operationControls);

    auto *acquisition = card();
    auto *measure = new QVBoxLayout(acquisition);
    measure->setContentsMargins(13, 10, 13, 12);
    measure->setSpacing(7);
    measure->addWidget(localized(label({}, "sectionTitle"), QT_TR_NOOP("Messung")));

    auto *read = localized(new QPushButton, QT_TR_NOOP("Messung abrufen"));
    read->setObjectName(QStringLiteral("primary"));
    read->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    connect(read, &QPushButton::clicked, this, [this] { start(Operation::Read); });
    measure->addWidget(read);

    const auto addButton = [this](QBoxLayout *row, const char *text, Operation operation) {
        auto *button = localized(new QPushButton, text);
        row->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, operation] {
            if (operation == Operation::Zero || operation == Operation::Calibrate) {
                confirmAdjustment(operation);
            } else {
                start(operation);
            }
        });
        return button;
    };
    localized(addButton(measure, QT_TR_NOOP("Messung auslösen"), Operation::Trigger),
              QT_TR_NOOP("Sendet einen Trigger und liest anschließend die Anzeige."), &QWidget::setToolTip);
    auto *automatic = new QHBoxLayout;
    autoRefresh = localized(new QCheckBox, QT_TR_NOOP("Automatisch abrufen"));
    autoRefresh->setObjectName(QStringLiteral("autoRefresh"));
    automatic->addWidget(autoRefresh, 1);
    interval = new QSpinBox;
    interval->setObjectName(QStringLiteral("refreshInterval"));
    localized(interval, QT_TR_NOOP("Abrufintervall"), &QWidget::setAccessibleName);
    interval->setRange(1, 60);
    interval->setFixedWidth(98);
    interval->setSuffix(QStringLiteral(" s"));
    interval->setValue(preferences.value(QStringLiteral("refresh/interval"), 2).toInt());
    automatic->addWidget(interval);
    measure->addLayout(automatic);
    connect(autoRefresh, &QCheckBox::toggled, this, &MainWindow::updateControls);
    connect(interval, qOverload<int>(&QSpinBox::valueChanged), this, [this] {
        refreshTimer.stop();
        updateControls();
    });
    operations->addWidget(acquisition);

    auto *device = card();
    auto *control = new QVBoxLayout(device);
    control->setContentsMargins(13, 10, 13, 12);
    control->setSpacing(7);
    control->addWidget(localized(label({}, "sectionTitle"), QT_TR_NOOP("Gerätebedienung")));
    auto *modes = new QHBoxLayout;
    addButton(modes, QT_TR_NOOP("Leistung (mW)"), Operation::Power);
    localized(addButton(modes, QT_TR_NOOP("Logarithmisch"), Operation::Decibel),
              QT_TR_NOOP("dBm oder dBr, entsprechend der Referenzeinstellung am Gerät."), &QWidget::setToolTip);
    control->addLayout(modes);
    auto *ranges = new QHBoxLayout;
    addButton(ranges, QT_TR_NOOP("Auto-Bereich"), Operation::AutoRange);
    addButton(ranges, QT_TR_NOOP("Bereich halten"), Operation::HoldRange);
    control->addLayout(ranges);

    channelRow = new QWidget;
    auto *channel = new QHBoxLayout(channelRow);
    channel->setContentsMargins(0, 0, 0, 0);
    auto *channelLabel = localized(new QLabel, QT_TR_NOOP("Kanal"));
    channel->addWidget(channelLabel);
    channelBox = new QComboBox;
    channelBox->setObjectName(QStringLiteral("channelSelector"));
    channelLabel->setBuddy(channelBox);
    for (int index = 1; index <= 3; ++index) {
        channelBox->addItem(channelName(index), index);
    }
    channel->addWidget(channelBox, 1);
    control->addWidget(channelRow);
    connect(channelBox, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        start(Operation::Channel, channelBox->itemData(index).toInt());
    });

    auto *adjustment = new QHBoxLayout;
    addButton(adjustment, QT_TR_NOOP("Nullabgleich …"), Operation::Zero);
    addButton(adjustment, QT_TR_NOOP("Kalibrieren …"), Operation::Calibrate);
    control->addLayout(adjustment);
    addButton(control, QT_TR_NOOP("Lokale Bedienung"), Operation::Local);
    operations->addWidget(device);

    addressLabel = label(QString(), "quiet");
    addressLabel->setAlignment(Qt::AlignRight);
    layout->addWidget(addressLabel);
    layout->addStretch();
    return side;
}

void MainWindow::loadSettings()
{
    target.board = std::clamp(preferences.value(QStringLiteral("gpib/board"), 0).toInt(), 0, 255);
    target.address = std::clamp(preferences.value(QStringLiteral("gpib/address"), 8).toInt(), 0, 30);
    target.terminator = static_cast<Terminator>(std::clamp(
        preferences.value(QStringLiteral("gpib/terminator"), int(Terminator::CrLf)).toInt(), 0, 3));
    secondChannel = preferences.value(QStringLiteral("device/secondChannel"), false).toBool();
    darkTheme = preferences.value(QStringLiteral("appearance/dark"), true).toBool();
}

void MainWindow::editConnection()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("GPIB-Verbindung"));
    dialog.setMinimumWidth(390);
    auto *form = new QFormLayout(&dialog);
    form->setContentsMargins(18, 16, 18, 16);
    form->setSpacing(10);
    form->addRow(tr("Schnittstelle"), new QLabel(QStringLiteral("NI-488.2")));

    auto *board = new QSpinBox;
    board->setObjectName(QStringLiteral("gpibBoard"));
    board->setRange(0, 255);
    board->setValue(target.board);
    form->addRow(tr("GPIB-Bus"), board);
    auto *address = new QSpinBox;
    address->setObjectName(QStringLiteral("gpibAddress"));
    address->setRange(0, 30);
    address->setValue(target.address);
    form->addRow(tr("Primäradresse"), address);

    auto *terminator = new QComboBox;
    terminator->setObjectName(QStringLiteral("gpibTerminator"));
    terminator->addItems({QStringLiteral("EOI"), QStringLiteral("CR"),
                          QStringLiteral("LF"), QStringLiteral("CR + LF")});
    terminator->setCurrentIndex(int(target.terminator));
    terminator->setToolTip(tr("Muss den Schaltern 6 und 7 am Boonton entsprechen."));
    form->addRow(tr("Nachrichtenende"), terminator);
    auto *dual = new QCheckBox(tr("Zweiter Kanal vorhanden (4200-03)"));
    dual->setObjectName(QStringLiteral("secondChannel"));
    dual->setChecked(secondChannel);
    form->addRow(dual);

    auto *buttons = new QDialogButtonBox;
    buttons->addButton(tr("Abbrechen"), QDialogButtonBox::RejectRole);
    buttons->addButton(tr("Übernehmen"), QDialogButtonBox::AcceptRole);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        if (board->value() != target.board || address->value() != target.address) {
            clearHistory();
            displayedReading.reset();
            measurementUnavailable = false;
            updateMeter();
        }
        target = {board->value(), address->value(), static_cast<Terminator>(terminator->currentIndex())};
        secondChannel = dual->isChecked();
        setStatus([this] { return tr("Bereit · %1").arg(connectionText()); });
        updateControls();
    }
}

void MainWindow::saveSettings()
{
    preferences.setValue(QStringLiteral("gpib/board"), target.board);
    preferences.setValue(QStringLiteral("gpib/address"), target.address);
    preferences.setValue(QStringLiteral("gpib/terminator"), int(target.terminator));
    preferences.setValue(QStringLiteral("device/secondChannel"), secondChannel);
    preferences.setValue(QStringLiteral("appearance/dark"), darkTheme);
    preferences.setValue(QStringLiteral("refresh/interval"), interval->value());
    preferences.sync();
    if (preferences.status() == QSettings::NoError) {
        setStatus([] { return tr("Einstellungen gespeichert."); });
    } else {
        setStatus([] { return tr("Die Einstellungen konnten nicht gespeichert werden."); }, true);
    }
}

void MainWindow::setDarkTheme(bool dark)
{
    darkTheme = dark;
    const auto color = [dark](const char *night, const char *day) {
        return QColor(dark ? night : day);
    };
    QPalette palette;
    palette.setColor(QPalette::Window, color("#10151e", "#edf3f8"));
    palette.setColor(QPalette::WindowText, color("#e8f0fb", "#1c3044"));
    palette.setColor(QPalette::Base, color("#171f2b", "#ffffff"));
    palette.setColor(QPalette::AlternateBase, color("#192331", "#e7eef5"));
    palette.setColor(QPalette::Text, color("#e8f0fb", "#1c3044"));
    palette.setColor(QPalette::Button, color("#222e3e", "#e7eef5"));
    palette.setColor(QPalette::ButtonText, color("#e8f0fb", "#1c3044"));
    palette.setColor(QPalette::Highlight, color("#33536d", "#cbe3f7"));
    palette.setColor(QPalette::HighlightedText, color("#e8f0fb", "#17334e"));
    for (const auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText}) {
        palette.setColor(QPalette::Disabled, role, color("#8896a9", "#586d80"));
    }
    qApp->setPalette(palette);
    QString style;
    for (const auto &resource : {QStringLiteral(":/theme.qss"), QStringLiteral(":/theme-light.qss")}) {
        if (!dark || resource == QStringLiteral(":/theme.qss")) {
            QFile file(resource);
            if (file.open(QIODevice::ReadOnly)) {
                style += QString::fromUtf8(file.readAll()) + QLatin1Char('\n');
            }
        }
    }
    qApp->setStyleSheet(style);
    languageMenu->setIcon(globeIcon(dark));

    palette.setColor(QPalette::Window, color("#171f2b", "#f9fbfd"));
    palette.setColor(QPalette::WindowText, color("#d4e3f5", "#253f56"));
    palette.setColor(QPalette::Text, color("#d4e3f5", "#253f56"));
    plot->setPalette(palette);
    for (auto axis : {QwtAxis::XBottom, QwtAxis::YLeft}) {
        plot->axisWidget(axis)->setPalette(palette);
    }
    plot->setCanvasBackground(color("#101925", "#ffffff"));
    grid->setMajorPen(color("#41576e", "#b0c2d0"), 1, Qt::DotLine);
    const auto accent = color("#77ccff", "#006ea8");
    curve->setPen(accent, 1.5);
    curve->setSymbol(new QwtSymbol(QwtSymbol::Ellipse, QBrush(accent), QPen(Qt::NoPen), QSize(5, 5)));
    plot->replot();
}

void MainWindow::showAbout()
{
    QMessageBox about(this);
    about.setWindowTitle(tr("Über"));
    about.setTextFormat(Qt::RichText);
    about.setText(QStringLiteral("<b>Boonton 4200 Control</b><br>%1<br><br><b>Ralf Tralow</b>")
                      .arg(tr("Version %1").arg(QCoreApplication::applicationVersion())));
    QMovie logo(QStringLiteral(":/branding/logo_g.gif"));
    if (logo.isValid()) {
        connect(&logo, &QMovie::frameChanged, &about, [&] {
            about.setIconPixmap(logo.currentPixmap());
        });
        logo.start();
    } else {
        about.setIconPixmap(windowIcon().pixmap(120));
    }
    about.exec();
}

void MainWindow::confirmAdjustment(Operation operation)
{
    refreshTimer.stop();
    const bool zero = operation == Operation::Zero;
    const auto title = zero ? tr("Nullabgleich") : tr("1-mW-Kalibrierung");
    const auto prompt = zero
        ? tr("HF-Signal vom Sensor trennen. Nullabgleich jetzt am Gerät starten?")
        : tr("Sensor an den eingeschalteten 1-mW-Referenzausgang anschließen. "
                         "Kanal, Leistungsmodus, Autorange und Kalibrierfaktor gemäß Handbuch "
                         "(Abschnitt 3-23) einstellen. Kalibrierung jetzt starten?");
    QMessageBox dialog(QMessageBox::Question, title, prompt, QMessageBox::Yes | QMessageBox::No, this);
    dialog.button(QMessageBox::Yes)->setText(tr("Ja"));
    dialog.button(QMessageBox::No)->setText(tr("Nein"));
    dialog.setDefaultButton(QMessageBox::No);
    if (dialog.exec() == QMessageBox::Yes) {
        start(operation);
    } else {
        updateControls();
    }
}

void MainWindow::start(Operation operation, int channel)
{
    if (busy || (operation == Operation::Connect ? connected : !connected)) {
        return;
    }
    busy = true;
    active = operation;
    setStatus([this, operation] {
        switch (operation) {
        case Operation::Connect: return tr("%1 wird geöffnet …").arg(connectionText());
        case Operation::Disconnect: return tr("GPIB-Verbindung wird getrennt …");
        case Operation::Zero: return tr("Nullabgleich läuft …");
        case Operation::Calibrate: return tr("Kalibrierung läuft …");
        case Operation::Local: return tr("Gerätebedienung wird freigegeben …");
        default: return tr("Messwert wird gelesen …");
        }
    });
    updateControls();
    QMetaObject::invokeMethod(instrument, [worker = instrument, operation, target = target, channel] {
        worker->execute(operation, target, channel);
    }, Qt::QueuedConnection);
}

void MainWindow::finish(const InstrumentResult &result)
{
    connected = result.connected;
    busy = false;
    if (closing) {
        if (connected) {
            start(Operation::Disconnect);
        } else {
            close();
        }
        return;
    }
    if (!connected || !result.error.isEmpty()) {
        autoRefresh->setChecked(false);
    }
    if (!result.error.isEmpty()) {
        setStatus([error = result.error] { return error; }, true);
        if (active != Operation::Disconnect && active != Operation::Local) {
            displayedReading.reset();
            measurementUnavailable = true;
            updateMeter();
        }
    } else if (result.reading) {
        showReading(*result.reading);
        setStatus([reading = *result.reading] { return reading.statusText(); }, result.reading->status != 0);
    } else if (connected) {
        setStatus([] { return tr("Lokale Bedienung freigegeben."); });
    } else {
        setStatus([] { return tr("GPIB-Verbindung getrennt."); });
    }
    updateControls();
}

void MainWindow::showReading(const Reading &reading)
{
    displayedReading = reading;
    measurementUnavailable = false;
    updateMeter();
    channelBox->setCurrentIndex(channelBox->findData(reading.channel));

    if (!history.empty() && (history.back().reading.mode != reading.mode
                             || history.back().reading.channel != reading.channel)) {
        clearHistory();
    }
    if (reading.status == 0) {
        if (history.empty()) {
            historyClock.start();
        }
        history.push_back({reading, historyClock.elapsed() / 1000.0});
        if (history.size() > historyCapacity) {
            history.pop_front();
        }
        updatePlot();
    }
}

void MainWindow::updateMeter()
{
    if (!displayedReading) {
        valueLabel->setText(QStringLiteral("—"));
        unitLabel->clear();
        measurementInfo->setText(measurementUnavailable ? tr("Kein aktueller Messwert") : tr("Noch kein Messwert"));
        return;
    }

    const auto &reading = *displayedReading;
    valueLabel->setText(reading.status == 0 ? locale().toString(reading.value, 'f', reading.decimals) : QStringLiteral("—"));
    unitLabel->setText(reading.unit());
    measurementInfo->setText(tr("%1 · Bereichscode %2 · %3")
                             .arg(channelName(reading.channel)).arg(reading.range)
                             .arg(reading.time.toString(QStringLiteral("HH:mm:ss"))));
}

void MainWindow::updatePlot()
{
    QVector<QPointF> points;
    points.reserve(static_cast<qsizetype>(history.size()));
    for (const auto &point : history) {
        points.append(QPointF(point.seconds, point.reading.value));
    }
    curve->setSamples(points);
    if (history.empty()) {
        pointCount->setText(tr("Keine Messwerte"));
        plot->setAxisTitle(QwtAxis::YLeft, tr("Messwert"));
        plot->setAxisScale(QwtAxis::XBottom, 0, 10);
        plot->setAxisScale(QwtAxis::YLeft, 0, 1);
    } else {
        pointCount->setText(history.size() == 1 ? tr("1 Messwert")
                                               : tr("%1 Messwerte").arg(history.size()));
        plot->setAxisTitle(QwtAxis::YLeft, history.back().reading.unit());
        plot->setAxisScale(QwtAxis::XBottom, history.front().seconds,
                           std::max(history.front().seconds + 2, history.back().seconds + 0.2));
        plot->setAxisAutoScale(QwtAxis::YLeft);
    }
    plot->replot();
}

void MainWindow::clearHistory()
{
    history.clear();
    updatePlot();
    updateControls();
}

void MainWindow::saveCsv()
{
    const auto series = history;
    if (series.empty()) {
        return;
    }
    const auto filename = QStringLiteral("Boonton4200_%1.csv")
                              .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")));
    QFileDialog dialog(this, tr("Messwerte als CSV speichern"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(tr("CSV-Datei (*.csv)"));
    dialog.setDefaultSuffix(QStringLiteral("csv"));
    dialog.selectFile(filename);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QSaveFile file(dialog.selectedFiles().constFirst());
    bool saved = false;
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        QLocale csv(QLocale::German, QLocale::Germany);
        csv.setNumberOptions(QLocale::OmitGroupSeparator);
        out << "Zeitstempel;Kanal;Messwert;Einheit;Bereichscode\n";
        for (const auto &point : series) {
            const auto &reading = point.reading;
            out << reading.time.toString(Qt::ISODateWithMs) << ';' << reading.channel << ';'
                << csv.toString(reading.value, 'g', 10) << ';' << reading.unit() << ';' << reading.range << '\n';
        }
        out.flush();
        saved = out.status() == QTextStream::Ok && file.commit();
    }
    if (saved) {
        setStatus([] { return tr("Messwerte gespeichert."); });
    } else {
        setStatus([error = file.errorString()] { return tr("Datei konnte nicht gespeichert werden. %1").arg(error); }, true);
    }
}

void MainWindow::updateControls()
{
    connectionSettings->setEnabled(!connected && !busy);
    connectButton->setEnabled(!busy);
    connectButton->setText(connected ? tr("Trennen") : tr("Verbinden"));
    connectButton->setProperty("primary", !connected);
    connectButton->style()->unpolish(connectButton);
    connectButton->style()->polish(connectButton);
    if (busy && active == Operation::Connect) {
        connectionState->setText(tr("Verbindet …"));
    } else if (busy && active == Operation::Disconnect) {
        connectionState->setText(tr("Trennt …"));
    } else {
        connectionState->setText(connected ? tr("Verbunden") : tr("Getrennt"));
    }
    connectionState->setProperty("connected", connected);
    connectionState->style()->unpolish(connectionState);
    connectionState->style()->polish(connectionState);
    addressLabel->setText(connectionText());
    operationControls->setEnabled(connected && !busy);
    channelRow->setVisible(secondChannel);
    saveCsvAction->setEnabled(!history.empty());
    clearHistoryAction->setEnabled(!history.empty());
    if (connected && !busy && !closing && autoRefresh->isChecked()) {
        if (!refreshTimer.isActive()) {
            refreshTimer.start(interval->value() * 1000);
        }
    } else {
        refreshTimer.stop();
    }
}

void MainWindow::setStatus(std::function<QString()> text, bool error)
{
    statusText = std::move(text);
    statusLabel->setText(statusText());
    statusLabel->setObjectName(error ? QStringLiteral("statusError") : QStringLiteral("status"));
    statusLabel->style()->unpolish(statusLabel);
    statusLabel->style()->polish(statusLabel);
}

QString MainWindow::connectionText() const
{
    return tr("NI-488.2 · GPIB %1 · Adresse %2").arg(target.board).arg(target.address);
}
