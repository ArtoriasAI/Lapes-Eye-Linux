#include "LapesEye/ui/MainWindow.h"
#include "LapesEye/core/PerfTimer.h"
#include "LapesEye/ui/FolderPanel.h"
#include "LapesEye/ui/ThumbnailGrid.h"
#include "LapesEye/ui/ThumbnailCanvas.h"
#include "LapesEye/ui/PreviewPanel.h"
#include "LapesEye/ui/MetaPanel.h"
#include "LapesEye/ui/FilterBar.h"
#include <QtConcurrent/QtConcurrent>
#include "LapesEye/ui/BreadcrumbBar.h"
#include "LapesEye/ui/CompareView.h"
#include "LapesEye/ui/TearOffTabBar.h"
#include "LapesEye/ui/BatchRenameDialog.h"
#include "LapesEye/ui/SettingsDialog.h"
#include "LapesEye/ui/ColorLabelEditor.h"
#include <QProcess>
#include <QMessageBox>
#include "LapesEye/ui/CollectionDialog.h"
#include "LapesEye/core/Collection.h"
#include "LapesEye/ui/AdvancedSearchDialog.h"
#include "LapesEye/ui/ExportDialog.h"
#include "LapesEye/ui/PrintDialog.h"
#include "LapesEye/core/ThumbCache.h"
#include "LapesEye/core/MetaStore.h"
#include "LapesEye/core/LapeIPC.h"
#include "LapesEye/workers/ThumbWorker.h"

#include <QMenuBar>
#include <QMenu>
#include <QStackedLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QDebug>
#include <QInputDialog>
#include <QCursor>
#include <QSlider>
#include <QLabel>
#include <QVBoxLayout>
#include <QSettings>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QCloseEvent>
#include <QFileInfo>
#include <QDir>
#include <QToolButton>
#include <QStyle>
#include <QPixmapCache>
#include <QTimer>

namespace LapesEye {

// ─── Konstruktor ──────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Lape's Eye");
    setMinimumSize(1100, 700);
    resize(1400, 900);

    qApp->setStyle("Fusion");
    QPalette pal;
    pal.setColor(QPalette::Window,          QColor(0x1E, 0x1E, 0x1E));
    pal.setColor(QPalette::WindowText,      QColor(0xCC, 0xCC, 0xCC));
    pal.setColor(QPalette::Base,            QColor(0x28, 0x28, 0x28));
    pal.setColor(QPalette::AlternateBase,   QColor(0x24, 0x24, 0x24));
    pal.setColor(QPalette::Text,            QColor(0xCC, 0xCC, 0xCC));
    pal.setColor(QPalette::Button,          QColor(0x38, 0x38, 0x38));
    pal.setColor(QPalette::ButtonText,      QColor(0xCC, 0xCC, 0xCC));
    pal.setColor(QPalette::Highlight,       QColor(0x2D, 0x7D, 0xD2));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    qApp->setPalette(pal);

    m_thumb_cache  = std::make_unique<ThumbCache>();
    m_thumb_cache->open();
    m_thumb_worker = std::make_unique<ThumbWorker>(m_thumb_cache.get());
    m_fullscreen_viewer = new FullscreenViewer(this);
    m_ipc          = std::make_unique<LapeIPC>();

    // KOLEJNOŚĆ KRYTYCZNA — toolbar/statusbar przed panelami, panele przed zakładkami
    setup_toolbar();
    setup_statusbar();
    setup_panels();
    setup_tabs();
    setup_menu();
    load_settings();

    // Zastosuj ustawienia cache z poprzedniej sesji
    QPixmapCache::setCacheLimit(SettingsDialog::ram_cache_mb() * 1024);
    // Wczytaj ustawienia UI po setup_panels (m_preview_panel już istnieje)
    m_preview_panel->set_histogram_visible(SettingsDialog::histogram_visible());

    // Globalne skróty klawiszowe dla schowka — działają niezależnie od focusu
    new QShortcut(QKeySequence::Copy,  this, [this]() {
        if (auto* g = current_grid()) g->copy_selected(false); });
    new QShortcut(QKeySequence::Cut,   this, [this]() {
        if (auto* g = current_grid()) g->copy_selected(true); });
    new QShortcut(QKeySequence::Paste, this, [this]() {
        if (auto* g = current_grid()) g->paste_here(); });

    // Enter/Return — obsługiwane wyłącznie przez ThumbnailGrid::keyPressEvent
    // (WindowShortcut kolidował z keyPressEvent gdy oba aktywne jednocześnie)
    // Instaluj globalny event filter — przechwytuje Enter PRZED dotarciem do QToolButton.
    // Qt dostarcza klawisze do widgetu z focusem; jeśli to QToolButton w toolbarze,
    // Enter wywoła jego action (np. action_new_tab). Przechwycimy to wcześniej.
    qApp->installEventFilter(this);

    // Ctrl+F — zaawansowane wyszukiwanie
    new QShortcut(QKeySequence("Ctrl+F"), this, [this]() { action_advanced_search(); });
    // Ctrl+L — focus na pasek adresu (jak w przeglądarkach)
    new QShortcut(QKeySequence("Ctrl+L"), this, [this]() {
        if (m_breadcrumb) m_breadcrumb->enter_edit_mode();
    });

    m_ipc->connect_to_lape();
    QObject::connect(m_ipc.get(), &LapeIPC::status_changed,
                     this, &MainWindow::on_ipc_status_changed);
}

MainWindow::~MainWindow() {
    save_settings();
    m_thumb_cache->close();
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void MainWindow::setup_toolbar() {
    auto* tb = addToolBar("Główny");
    tb->setObjectName("toolbar_main");
    tb->setMovable(false);
    tb->setIconSize(QSize(16, 16));

    // Wstecz / Przód
    m_act_back = new QAction("◀", tb);
    m_act_back->setToolTip("Wstecz (Alt+←)");
    m_act_back->setEnabled(false);
    m_act_back->setShortcut(QKeySequence("Alt+Left"));
    QObject::connect(m_act_back, &QAction::triggered, this, &MainWindow::action_go_back);
    tb->addAction(m_act_back);

    m_act_forward = new QAction("▶", tb);
    m_act_forward->setToolTip("Przód (Alt+→)");
    m_act_forward->setEnabled(false);
    m_act_forward->setShortcut(QKeySequence("Alt+Right"));
    QObject::connect(m_act_forward, &QAction::triggered, this, &MainWindow::action_go_forward);
    tb->addAction(m_act_forward);

    tb->addSeparator();

    auto* act_folder = new QAction("📁 Folder", tb);
    QObject::connect(act_folder, &QAction::triggered, this, &MainWindow::action_open_folder);
    tb->addAction(act_folder);

    // ── Edytowalny pasek adresu ────────────────────────────────────────────────
    // Pasek nawigacji w stylu Dolphin (breadcrumbs)
    m_breadcrumb = new BreadcrumbBar(tb);
    m_breadcrumb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QObject::connect(m_breadcrumb, &BreadcrumbBar::navigate_requested,
                     this, [this](const QString& path) { navigate_to(path); });
    tb->addWidget(m_breadcrumb);

    auto* act_new_tab = new QAction("＋ Zakładka", tb);
    act_new_tab->setToolTip("Nowa zakładka (Ctrl+T)");
    QObject::connect(act_new_tab, &QAction::triggered, this, &MainWindow::action_new_tab);
    tb->addAction(act_new_tab);

    tb->addSeparator();

    auto* act_selall = new QAction("☑ Zaznacz wszystko", tb);
    QObject::connect(act_selall, &QAction::triggered, this, &MainWindow::action_select_all);
    tb->addAction(act_selall);

    auto* act_rename = new QAction("✎ Zmień nazwy", tb);
    QObject::connect(act_rename, &QAction::triggered, this, &MainWindow::action_batch_rename);
    tb->addAction(act_rename);

    tb->addSeparator();
    tb->addWidget(new QLabel("  Rozmiar:  ", tb));

    m_zoom_slider = new QSlider(Qt::Horizontal, tb);
    m_zoom_slider->setRange(100, 1200);
    m_zoom_slider->setValue(220);
    m_zoom_slider->setFixedWidth(160);
    m_zoom_slider->setToolTip("Rozmiar miniatur");
    tb->addWidget(m_zoom_slider);

    // Po interakcji ze suwakiem — fokus wraca do siatki (nie do suwaka)
    QObject::connect(m_zoom_slider, &QSlider::sliderReleased, this, [this]() {
        if (auto* g = current_grid()) g->setFocus();
    });
    QObject::connect(m_zoom_slider, &QSlider::valueChanged, this, [this](int val) {
        if (auto* g = current_grid()) g->set_thumb_size(val);
    });

    // KLUCZOWE: toolbar i wszystkie jego przyciski NIE mogą mieć focusu.
    // QToolButton stworzony przez addAction() domyślnie ma Qt::TabFocus —
    // Enter na zaznaczonym przycisku toolbara wywołuje action (→ nowa zakładka).
    tb->setFocusPolicy(Qt::NoFocus);
    // Ustaw NoFocus na wszystkich dzieciach toolbara (QToolButton, QSlider, itd.)
    // przez QTimer — przyciski tworzone przez addAction są dostępne dopiero po setup
    QTimer::singleShot(0, this, [tb]() {
        const auto children = tb->findChildren<QWidget*>();
        for (QWidget* w : children)
            w->setFocusPolicy(Qt::NoFocus);
    });
}

void MainWindow::setup_statusbar() {
    m_status_count  = new QLabel("", this);
    m_status_path   = new QLabel("", this);
    m_ipc_indicator = new QLabel("● Lape: rozłączono", this);
    m_ipc_indicator->setStyleSheet("color: #888;");
    statusBar()->addWidget(m_status_count);
    statusBar()->addWidget(m_status_path, 1);
    statusBar()->addPermanentWidget(m_ipc_indicator);
}

void MainWindow::setup_panels() {
    // Lewy dock: panel miejsc
    m_folder_panel = new FolderPanel(this);
    auto* left_dock = new QDockWidget("Foldery", this);
    left_dock->setObjectName("dock_folders");
    left_dock->setWidget(m_folder_panel);
    left_dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    left_dock->setFeatures(QDockWidget::DockWidgetMovable |
                           QDockWidget::DockWidgetFloatable |
                           QDockWidget::DockWidgetClosable);
    left_dock->setMinimumWidth(180);
    left_dock->setMaximumWidth(280);
    // Problem 5: QDockWidget może pochłaniać drag eventy — wymuś acceptDrops
    left_dock->setAcceptDrops(true);
    addDockWidget(Qt::LeftDockWidgetArea, left_dock);

    // Prawy dock góra: podgląd / porównanie
    // Stack widget — PreviewPanel (single) lub CompareView (2/4)
    m_preview_panel = new PreviewPanel(this);
    m_compare_view  = new CompareView(this);
    m_compare_view->setVisible(false);

    // Kontener dla obu widoków w jednym docku
    auto* preview_container = new QWidget(this);
    auto* preview_stack     = new QStackedLayout(preview_container);
    preview_stack->setContentsMargins(0, 0, 0, 0);
    preview_stack->addWidget(m_preview_panel);   // index 0 — Single
    preview_stack->addWidget(m_compare_view);    // index 1 — Split2/Split4
    preview_stack->setCurrentIndex(0);

    m_preview_dock = new QDockWidget("Podgląd", this);
    m_preview_dock->setObjectName("dock_preview");
    m_preview_dock->setWidget(preview_container);
    m_preview_dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_preview_dock->setFeatures(QDockWidget::DockWidgetMovable |
                                QDockWidget::DockWidgetFloatable |
                                QDockWidget::DockWidgetClosable);
    m_preview_dock->setMinimumWidth(230);
    addDockWidget(Qt::RightDockWidgetArea, m_preview_dock);

    // Prawy dock dół: metadane
    m_meta_panel = new MetaPanel(this);
    auto* meta_dock = new QDockWidget("Metadane", this);
    meta_dock->setObjectName("dock_meta");
    meta_dock->setWidget(m_meta_panel);
    meta_dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    meta_dock->setFeatures(QDockWidget::DockWidgetMovable |
                           QDockWidget::DockWidgetFloatable |
                           QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::RightDockWidgetArea, meta_dock);
    splitDockWidget(m_preview_dock, meta_dock, Qt::Vertical);

    // Dół: filtr
    m_filter_bar = new FilterBar(this);
    auto* filter_dock = new QDockWidget("Filtry", this);
    filter_dock->setObjectName("dock_filter");
    filter_dock->setWidget(m_filter_bar);
    filter_dock->setAllowedAreas(Qt::BottomDockWidgetArea);
    filter_dock->setTitleBarWidget(new QWidget());
    filter_dock->setFixedHeight(44);
    addDockWidget(Qt::BottomDockWidgetArea, filter_dock);

    QObject::connect(m_folder_panel, &FolderPanel::folder_selected,
                     this, &MainWindow::on_folder_selected);
    QObject::connect(m_folder_panel, &FolderPanel::collection_selected,
                     this, [this](const QString& col_id) {
                         auto cols = CollectionStore::load_all();
                         for (const auto& c : cols) {
                             if (c.id == col_id) {
                                 auto* g = current_grid();
                                 if (!g) return;
                                 g->load_collection(c.static_paths, c.name);
                                 m_current_collection_id = c.id;
                                 m_current_dir.clear();
                                 setWindowTitle("Lape's Eye — 📚 " + c.name);
                                 if (m_breadcrumb)
                                     m_breadcrumb->set_path("📚 " + c.name);
                                 return;
                             }
                         }
                     });
    QObject::connect(m_folder_panel, &FolderPanel::folder_open_in_tab,
                     this, &MainWindow::on_folder_in_new_tab);
    QObject::connect(m_folder_panel, &FolderPanel::folder_open_in_window,
                     this, &MainWindow::on_folder_in_new_window);
    QObject::connect(m_folder_panel, &FolderPanel::collection_edit_requested,
                     this, [this](const QString& col_id) {
                         auto cols = CollectionStore::load_all();
                         for (int i = 0; i < cols.size(); ++i) {
                             if (cols[i].id == col_id) {
                                 CollectionDialog dlg(this, &cols[i]);
                                 if (dlg.exec() == QDialog::Accepted) {
                                     cols[i] = dlg.result();
                                     CollectionStore::save_all(cols);
                                     m_folder_panel->refresh();
                                 }
                                 return;
                             }
                         }
                     });
    QObject::connect(m_folder_panel, &FolderPanel::collection_delete_requested,
                     this, [this](const QString& col_id) {
                         auto cols = CollectionStore::load_all();
                         for (int i = 0; i < cols.size(); ++i) {
                             if (cols[i].id == col_id) {
                                 if (QMessageBox::question(this, "Usuń kolekcję",
                                     QString("Usunąć kolekcję \"%1\"?").arg(cols[i].name))
                                     == QMessageBox::Yes) {
                                     cols.removeAt(i);
                                     CollectionStore::save_all(cols);
                                     m_folder_panel->refresh();
                                 }
                                 return;
                             }
                         }
                     });
    // Problem 3: po przeniesieniu plików przez FolderPanel odśwież aktualny grid
    QObject::connect(m_folder_panel, &FolderPanel::files_moved_to,
                     this, [this](const QString& /*dest*/, const QStringList& srcs) {
                         if (auto* g = current_grid()) {
                             // Ignoruj podczas drag — ThumbnailGrid sam obsłuży usunięcie
                             // po zakończeniu drag->exec() przez MoveAction
                             if (g->is_drag_active()) return;
                             QStringList still_in_grid;
                             for (const QString& src : srcs)
                                 if (g->has_item(src)) still_in_grid << src;
                             if (!still_in_grid.isEmpty())
                                 g->remove_items_in_place(still_in_grid);
                         }
                     });
    QObject::connect(m_meta_panel, &MetaPanel::metadata_saved,
                     this, [this](const QString& path) {
                         if (auto* g = current_grid()) g->refresh_metadata(path);
                     });
    // Problem 3: zmiana flagi pick/reject natychmiast odświeża siatkę (bez reload folderu)
    QObject::connect(m_meta_panel, &MetaPanel::flag_changed,
                     this, [this](const QString& path) {
                         if (auto* g = current_grid()) {
                             g->refresh_metadata(path);
                             g->reapply_filter();
                         }
                     });
    QObject::connect(m_meta_panel, &MetaPanel::return_focus,
                     this, [this]() {
                         if (auto* g = current_grid()) g->setFocus();
                     });
    // Problem 2: Rename z panelu metadanych — po przeładowaniu zaznacza nową nazwę
    QObject::connect(m_meta_panel, &MetaPanel::rename_requested,
                     this, [this](const QString& old_path, const QString& new_name) {
                         // Problem 1: użyj tej samej logiki co rename inline
                         // — aktualizuje siatkę w miejscu bez przeładowania
                         if (auto* g = current_grid())
                             g->rename_item(old_path, new_name);
                     });
}

void MainWindow::setup_tabs() {
    // Problem 4: TearOffTabWidget — subklasa QTabWidget z własnym TabBar
    auto* tear_widget = new TearOffTabWidget(this);
    m_tabs = tear_widget;
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);

    QObject::connect(tear_widget, &TearOffTabWidget::tab_torn_off,
                     this, [this](int index, const QString& path) {
        QString folder = path.isEmpty() ? m_tabs->tabToolTip(index) : path;
        if (!folder.isEmpty())
            on_folder_in_new_window(folder);
    });
    m_tabs->setStyleSheet(R"(
        QTabWidget::pane   { border: none; }
        QTabBar::tab       { background: #2a2a2a; color: #aaa;
                             padding: 5px 14px; margin-right: 2px; min-width: 80px; }
        QTabBar::tab:selected { background: #1e1e1e; color: #fff;
                                border-bottom: 2px solid #2D7DD2; }
        QTabBar::tab:hover    { background: #333; color: #ddd; }
    )");

    auto* new_tab_btn = new QToolButton(m_tabs);
    new_tab_btn->setText("+");
    new_tab_btn->setToolTip("Nowa zakładka (Ctrl+T)");
    new_tab_btn->setAutoRaise(true);
    new_tab_btn->setFixedSize(24, 24);
    new_tab_btn->setFocusPolicy(Qt::NoFocus);  // nie kradnie focusu od siatki
    new_tab_btn->setStyleSheet(
        "QToolButton { background: transparent; color: #aaa; font-size: 16px; "
        "border: none; border-radius: 3px; }"
        "QToolButton:hover { background: #333; color: #fff; }");
    m_tabs->setCornerWidget(new_tab_btn, Qt::TopLeftCorner);
    QObject::connect(new_tab_btn, &QToolButton::clicked, this, &MainWindow::action_new_tab);

    QObject::connect(m_tabs, &QTabWidget::currentChanged,
                     this, &MainWindow::on_tab_changed);
    QObject::connect(m_tabs, &QTabWidget::tabCloseRequested,
                     this, &MainWindow::on_tab_close_requested);

    setCentralWidget(m_tabs);
    add_tab();  // pierwsza zakładka — wszystkie pola już istnieją
}

void MainWindow::setup_menu() {
    auto* file_menu = menuBar()->addMenu("&Plik");
    file_menu->addAction("📤  Eksportuj zaznaczone...", QKeySequence("Ctrl+E"),
                          this, &MainWindow::action_export);
    file_menu->addAction("🖨  Drukuj zaznaczone...", QKeySequence::Print,
                          this, &MainWindow::action_print);
    file_menu->addSeparator();
    file_menu->addAction("Otwórz folder...",
        QKeySequence("Ctrl+O"), this, &MainWindow::action_open_folder);
    file_menu->addAction("Otwórz w nowej zakładce...",
        QKeySequence("Ctrl+Shift+O"), this, [this]() {
            QString d = QFileDialog::getExistingDirectory(this, "Otwórz w nowej zakładce",
                m_current_dir.isEmpty() ? QDir::homePath() : m_current_dir);
            if (!d.isEmpty()) on_folder_in_new_tab(d);
        });
    file_menu->addAction("Otwórz w nowym oknie...",
        QKeySequence("Ctrl+N"), this, [this]() {
            QString d = QFileDialog::getExistingDirectory(this, "Otwórz w nowym oknie",
                m_current_dir.isEmpty() ? QDir::homePath() : m_current_dir);
            if (!d.isEmpty()) open_folder_in_new_window(d);
        });
    file_menu->addSeparator();
    // Return i Shift+Return obsługiwane przez ThumbnailGrid::keyPressEvent
    // (QAction shortcut przechwytywałby Enter podczas rename — usunięto)
    auto* open_lape_act = file_menu->addAction("Otwórz w Lape", this, [this]() {
        if (auto* g = current_grid())
            for (const auto& p : g->selected_paths()) on_open_in_lape(p);
    });
    open_lape_act->setShortcutContext(Qt::WidgetShortcut);  // nie globalny

    auto* open_layer_act = file_menu->addAction("Otwórz jako warstwę", this, [this]() {
        if (auto* g = current_grid())
            for (const auto& p : g->selected_paths()) on_open_as_layer(p);
    });
    open_layer_act->setShortcutContext(Qt::WidgetShortcut);
    file_menu->addSeparator();
    file_menu->addAction("Zmiana nazw wsadowych...",
        QKeySequence("Ctrl+Shift+R"), this, &MainWindow::action_batch_rename);
    file_menu->addSeparator();
    // Raport wydajności — tylko w buildach z LAPE_PERF=1
    auto* perf_act = file_menu->addAction("Raport wydajności [konsola]",
        QKeySequence("Ctrl+Shift+P"), this, []() {
            qDebug() << "\n══════════ LAPE PERF REPORT ══════════";
            PERF_PRINT_ALL();
            qDebug() << "══════════════════════════════════════";
        });
    perf_act->setVisible(LAPE_PERF != 0);
    file_menu->addSeparator();
    file_menu->addAction("Nowa zakładka",    QKeySequence("Ctrl+T"),  this, &MainWindow::action_new_tab);
    file_menu->addAction("Zamknij zakładkę", QKeySequence("Ctrl+W"),  this, &MainWindow::action_close_tab);
    file_menu->addSeparator();
    file_menu->addAction("Wyjdź", QKeySequence("Ctrl+Q"), qApp, &QApplication::quit);

    auto* nav_menu = menuBar()->addMenu("&Nawigacja");
    nav_menu->addAction("Wstecz",  QKeySequence("Alt+Left"),  this, &MainWindow::action_go_back);
    nav_menu->addAction("Naprzód", QKeySequence("Alt+Right"), this, &MainWindow::action_go_forward);

    auto* edit_menu = menuBar()->addMenu("&Edycja");
    edit_menu->addAction("Zmień nazwę [F2]", QKeySequence(Qt::Key_F2), this, [this]() {
        if (auto* g = current_grid()) g->start_rename_selected();
    });
    edit_menu->addSeparator();
    edit_menu->addAction("Zaznacz wszystko", QKeySequence("Ctrl+A"), this, &MainWindow::action_select_all);
    edit_menu->addAction("Odznacz wszystko", QKeySequence("Escape"), this, [this]() {
        if (auto* g = current_grid()) g->deselect_all();
    });
    edit_menu->addSeparator();
    edit_menu->addAction("Usuń [Del]", QKeySequence(Qt::Key_Delete), this, [this]() {
        if (auto* g = current_grid()) g->delete_selected();
    });

    // Zakładka Ocena usunięta na życzenie użytkownika
    auto* rate_menu = menuBar()->addMenu("&Ocena");
    rate_menu->menuAction()->setVisible(false);
    for (int i = 1; i <= 5; ++i)
        rate_menu->addAction(QString(i, QChar(0x2605)),
            QKeySequence(QString::number(i)), this, [this, i]() { action_rate(i); });
    rate_menu->addAction("Usuń ocenę", QKeySequence("0"), this, [this]() { action_rate(0); });
    rate_menu->addSeparator();
    struct { const char* name; ColorLabel lbl; const char* key; } labels[] = {
        {"Czerwona",  ColorLabel::Red,    "6"},
        {"Żółta",     ColorLabel::Yellow, "7"},
        {"Zielona",   ColorLabel::Green,  "8"},
        {"Niebieska", ColorLabel::Blue,   "9"},
        {"Fioletowa", ColorLabel::Purple, ""},
        {"Brak",      ColorLabel::None,   ""},
    };
    for (const auto& l : labels) {
        auto* act = rate_menu->addAction(QString("Etykieta: ") + l.name,
            this, [this, lbl = l.lbl]() { action_label(lbl); });
        if (*l.key) act->setShortcut(QKeySequence(l.key));
    }

    auto* tools_menu = menuBar()->addMenu("&Narzędzia");
    tools_menu->addAction("⚙  Ustawienia...", QKeySequence("Ctrl+,"), this, [this]() {
        SettingsDialog dlg(this);
        QObject::connect(&dlg, &SettingsDialog::cache_clear_requested, this, [this]() {
            m_thumb_cache->purge_missing();
            statusBar()->showMessage("Cache wyczyszczony.", 3000);
        });
        dlg.exec();
        // Zastosuj widoczność histogramu
        m_preview_panel->set_histogram_visible(SettingsDialog::histogram_visible());
    });
    tools_menu->addSeparator();
    tools_menu->addAction("Wyczyść cache miniatur", this, &MainWindow::action_purge_cache);
    auto* new_folder_act = new QAction("Nowy folder tutaj", this);
    new_folder_act->setShortcut(QKeySequence("Ctrl+Shift+N"));
    QObject::connect(new_folder_act, &QAction::triggered, this, &MainWindow::action_new_folder);
    tools_menu->addAction(new_folder_act);
    tools_menu->addAction("📚  Kolekcje...", this, [this]() {
        // Dialog zarządzania kolekcjami — lista z edycją i usuwaniem
        auto cols = CollectionStore::load_all();
        QStringList names;
        for (const auto& c : cols) names << c.name;
        names << "[ + Nowa kolekcja ]";
        bool ok;
        QString chosen = QInputDialog::getItem(this, "Kolekcje",
            "Wybierz kolekcję do edycji lub utwórz nową:", names, 0, false, &ok);
        if (!ok) return;
        if (chosen == names.last()) {
            CollectionDialog dlg(this);
            if (dlg.exec() == QDialog::Accepted) {
                cols << dlg.result();
                CollectionStore::save_all(cols);
                m_folder_panel->refresh();
            }
        } else {
            int idx = names.indexOf(chosen);
            if (idx < 0 || idx >= cols.size()) return;
            QMenu sub;
            sub.addAction("✎  Edytuj", [&]() {
                CollectionDialog dlg(this, &cols[idx]);
                if (dlg.exec() == QDialog::Accepted) {
                    cols[idx] = dlg.result();
                    CollectionStore::save_all(cols);
                    m_folder_panel->refresh();
                }
            });
            sub.addAction("🗑  Usuń", [&]() {
                if (QMessageBox::question(this, "Usuń kolekcję",
                    QString("Usunac kolekcje \"%1\"?").arg(cols[idx].name))
                    == QMessageBox::Yes) {
                    cols.removeAt(idx);
                    CollectionStore::save_all(cols);
                    m_folder_panel->refresh();
                }
            });
            sub.exec(QCursor::pos());
        }
    });
    tools_menu->addAction("Połącz z Lape",          this, [this]() { m_ipc->connect_to_lape(); });

    menuBar()->addMenu("&Pomoc")->addAction("O Lape's Eye...", this, &MainWindow::action_about);
}

// ─── Zakładki ────────────────────────────────────────────────────────────────

ThumbnailGrid* MainWindow::current_grid() const {
    if (!m_tabs || m_tabs->count() == 0) return nullptr;
    return qobject_cast<ThumbnailGrid*>(m_tabs->currentWidget());
}

TabHistory& MainWindow::current_history() {
    int id = m_tabs->currentIndex();
    return m_history[id];
}

ThumbnailGrid* MainWindow::add_tab(const QString& path) {
    auto* grid = new ThumbnailGrid(m_thumb_worker.get(), m_tabs);
    int initial_size = m_zoom_slider->value();
    grid->set_thumb_size(initial_size);
    // Synchronizuj worker natychmiast — nie czekaj na timer 200ms
    m_thumb_worker->set_thumb_size(initial_size);

    QObject::connect(grid, &ThumbnailGrid::primary_changed,
                     this, [this, grid](const QString& path) {
                         if (grid != current_grid()) return;
                         on_primary_changed(path);
                     });
    QObject::connect(grid, &ThumbnailGrid::preview_rename,
                     this, [this, grid](const QString& old_path, const QString& new_path) {
                         if (grid != current_grid()) return;
                         // Tylko aktualizuj ścieżkę — nie przeładowuj obrazu
                         m_preview_panel->rename_path(old_path, new_path);
                     });
    QObject::connect(grid, &ThumbnailGrid::status_message,
                     this, [this](const QString& msg) { statusBar()->showMessage(msg, 3000); });
    QObject::connect(grid, &ThumbnailGrid::fullscreen_requested, this,
                     [this, grid](const QStringList& paths, int idx) {
                         m_fullscreen_viewer->show_image(paths, idx);
                         // Punkt 5: gdy zamykamy fullscreen, zaznacz ostatnie przeglądane zdjęcie
                         // Używamy jednorazowego połączenia przez QMetaObject
                         QObject::connect(m_fullscreen_viewer, &FullscreenViewer::closed,
                                          this, [this, grid]() {
                             int cur = m_fullscreen_viewer->current_index();
                             auto all = m_fullscreen_viewer->paths();
                             if (cur >= 0 && cur < all.size())
                                 grid->select_path(all[cur]);
                             // Przywróć fokus do grida po wyjściu z fullscreen
                             grid->setFocus();
                         }, Qt::SingleShotConnection);
                     });
    QObject::connect(grid, &ThumbnailGrid::selection_changed, this, &MainWindow::on_selection_changed);
    QObject::connect(grid, &ThumbnailGrid::open_in_lape,      this, &MainWindow::on_open_in_lape);
    QObject::connect(grid, &ThumbnailGrid::open_as_layer,     this, &MainWindow::on_open_as_layer);
    QObject::connect(grid, &ThumbnailGrid::context_menu,      this, &MainWindow::on_context_menu);
    QObject::connect(grid, &ThumbnailGrid::context_menu_background,
                     this, &MainWindow::on_context_menu_background);
    QObject::connect(grid, &ThumbnailGrid::folder_navigate,
                     this, &MainWindow::on_folder_selected);
    QObject::connect(grid, &ThumbnailGrid::folder_renamed,
                     this, [this](const QString&, const QString&) {
                         m_folder_panel->refresh();
                     });
    // Blokuj open_primary gdy canvas emituje rename_requested — PRZED on_rename_requested
    // Ustawiamy flagę natychmiast żeby EventFilter dla QWidgetWindow (ten sam Enter)
    // widział m_rename_just_finished=true i nie wywoływał open_primary()
    if (grid->canvas()) {
        QObject::connect(grid->canvas(), &ThumbnailCanvas::rename_requested,
                         this, [this](const QString&, const QString&) {
                             m_rename_just_finished = true;
                             QTimer::singleShot(600, this, [this]() {
                                 m_rename_just_finished = false;
                             });
                         });
    }
    // rename_done — odśwież FolderPanel jeśli folder był przemianowany
    QObject::connect(grid, &ThumbnailGrid::rename_done, this, [this]() {
        // flaga już ustawiona powyżej — nic dodatkowego
    });
    // Problem 3/4: licznik miniatur w statusbar (trwały, nie nadpisywany przez navigate_to)
    QObject::connect(grid, &ThumbnailGrid::thumb_progress,
                     this, [this, grid](int loaded, int total) {
                         if (grid != current_grid()) return;
                         m_meta_panel->set_file_count(loaded, total);
                     });
    QObject::connect(m_filter_bar, &FilterBar::filter_changed, grid, &ThumbnailGrid::set_filter);
    QObject::connect(m_filter_bar, &FilterBar::view_mode_changed,
                     this, &MainWindow::on_view_mode_changed);
    QObject::connect(m_filter_bar, &FilterBar::advanced_search_requested,
                     this, [this]() { action_advanced_search(); });
    QObject::connect(m_filter_bar, &FilterBar::rotate_requested,
                     this, [this, grid](int degrees) {
        grid->rotate_selected(degrees);
        // Wymuś odświeżenie podglądu — rotation zmieniła się dla zaznaczonych
        const QStringList sel = grid->selected_paths();
        if (!sel.isEmpty()) {
            m_preview_panel->invalidate(sel.first());
            m_preview_panel->load(sel.first());
        }
    });

    QString cleanLabelPath = path;
    while (cleanLabelPath.endsWith('/') && cleanLabelPath.size() > 1) cleanLabelPath.chop(1);
    QString label = path.isEmpty() ? "Nowa zakładka" : QFileInfo(cleanLabelPath).fileName();
    if (label.isEmpty()) label = QDir(cleanLabelPath).dirName();
    if (label.isEmpty()) label = "Nowa zakładka";
    int idx = m_tabs->addTab(grid, "📁 " + label);
    m_tabs->setTabToolTip(idx, path);

    // Inicjalizuj historię dla tej zakładki
    m_history[idx] = TabHistory{};

    if (!path.isEmpty()) {
        grid->load_folder(path);
        m_history[idx].current = path;
    }

    return grid;
}

void MainWindow::update_tab_title(int index, const QString& path) {
    if (index < 0 || index >= m_tabs->count()) {
        return;
    }
    // Debug: pokaż hex ostatnich 3 znaków żeby złapać trailing slash/null
    QString hexSuffix;
    for (int i = qMax(0, path.size()-4); i < path.size(); ++i)
        hexSuffix += QString("\\x%1").arg((uint)path[i].unicode(), 4, 16, QChar('0'));

    // Normalizuj ścieżkę — QFileInfo.fileName() zwraca "" gdy path kończy się na "/"
    QString cleanPath = path;
    while (cleanPath.endsWith('/') && cleanPath.size() > 1)
        cleanPath.chop(1);
    QString name = cleanPath.isEmpty() ? "" : QFileInfo(cleanPath).fileName();
    if (name.isEmpty()) name = QDir(cleanPath).dirName();
    if (name.isEmpty()) name = "Nowa zakładka";
    QString newText = "📁 " + name;
    m_tabs->setTabText(index, newText);
    m_tabs->setTabToolTip(index, path);
}

void MainWindow::on_tab_changed(int index) {
    if (index < 0) return;
    auto* grid = qobject_cast<ThumbnailGrid*>(m_tabs->widget(index));
    if (!grid) return;

    QString path = m_tabs->tabToolTip(index);
    m_current_dir = path;
    if (m_status_path) m_status_path->setText(path);
    setWindowTitle(path.isEmpty() ? "Lape's Eye" : "Lape's Eye — " + path);

    update_nav_buttons();
    // Użyj timera — QTabBar jeszcze przetwarza kliknięcie, bezpośredni setFocus()
    // jest natychmiast nadpisywany przez QTabBar który kradnie fokus po mouseRelease.
    QTimer::singleShot(0, grid, [grid]() { grid->setFocus(); });
}

void MainWindow::on_tab_close_requested(int index) {
    m_history.remove(index);
    if (m_tabs->count() <= 1) {
        if (auto* g = current_grid()) g->load_folder("");
        update_tab_title(0, "");
        m_current_dir.clear();
        setWindowTitle("Lape's Eye");
        m_history[0] = TabHistory{};
        update_nav_buttons();
        return;
    }
    auto* grid = qobject_cast<ThumbnailGrid*>(m_tabs->widget(index));
    m_tabs->removeTab(index);
    if (grid) grid->deleteLater();
}

void MainWindow::action_new_tab() {
    qDebug() << "[Lape] action_new_tab WYWOŁANA — STACK TRACE poniżej";
    // Wymuś backtrace przez Qt
    qDebug() << "  sender:" << sender();
    add_tab();
    m_tabs->setCurrentIndex(m_tabs->count() - 1);
}

void MainWindow::action_close_tab() {
    on_tab_close_requested(m_tabs->currentIndex());
}

// ─── Nawigacja z historią ─────────────────────────────────────────────────────

void MainWindow::navigate_to(const QString& path_ref, bool add_to_history) {
    // KLUCZOWE: skopiuj path natychmiast — referencja może być unieważniona
    // przez load_folder (sygnały, realokacje historii, etc.)
    const QString path = path_ref;
    if (path.isEmpty()) return;

    auto* grid = current_grid();
    if (!grid) return;

    // Nie nawiguj gdy canvas ma aktywny rename — folder zostanie załadowany
    // przez on_rename_requested po zakończeniu
    if (grid->canvas() && grid->canvas()->is_renaming()) {
        return;
    }

    // KLUCZOWE: użyj indexOf(grid) zamiast currentIndex().
    // currentIndex() może zwrócić -1 lub inny indeks jeśli zakładka zmienia się
    // asynchronicznie (np. przez sygnały z load_folder lub FolderPanel).
    int idx = m_tabs->indexOf(grid);
    if (idx < 0) return;
    auto& hist = m_history[idx];

    if (add_to_history && !hist.current.isEmpty() && hist.current != path) {
        hist.back.append(hist.current);
        hist.forward.clear();
    }

    hist.current = path;
    m_current_dir = path;
    m_current_collection_id.clear();

    m_navigating = true;
    grid->load_folder(path);
    update_tab_title(idx, path);
    m_status_path->setText(path);
    QString winName = QFileInfo(path).fileName();
    if (winName.isEmpty()) winName = QDir(path).dirName();
    setWindowTitle("Lape's Eye — " + winName);
    m_folder_panel->add_recent(path);
    m_folder_panel->set_current_path(path);  // ustawia m_setting_path=true podczas build_list
    update_nav_buttons();
    if (m_breadcrumb) m_breadcrumb->set_path(path);
    // Resetuj m_navigating po 200ms — pochłonie szybkie reaktywne kliknięcia
    QTimer::singleShot(200, this, [this]() {
        m_navigating = false;
    });
    QTimer::singleShot(250, this, [this]() {
        if (auto* g = current_grid()) g->setFocus();
    });
}

void MainWindow::update_nav_buttons() {
    if (!m_tabs || m_tabs->count() == 0) return;
    int idx = m_tabs->currentIndex();
    auto it = m_history.find(idx);
    if (it == m_history.end()) {
        m_act_back->setEnabled(false);
        m_act_forward->setEnabled(false);
        return;
    }
    m_act_back->setEnabled(!it->back.isEmpty());
    m_act_forward->setEnabled(!it->forward.isEmpty());
}

void MainWindow::action_go_back() {
    auto& hist = current_history();
    if (hist.back.isEmpty()) return;
    hist.forward.prepend(hist.current);
    QString prev = hist.back.takeLast();
    navigate_to(prev, false);
}

void MainWindow::action_go_forward() {
    auto& hist = current_history();
    if (hist.forward.isEmpty()) return;
    hist.back.append(hist.current);
    QString next = hist.forward.takeFirst();
    navigate_to(next, false);
}

// ─── Główne sloty ─────────────────────────────────────────────────────────────

void MainWindow::open_folder(const QString& path) {
    navigate_to(path);
}

void MainWindow::open_folder_in_new_tab(const QString& path, bool switch_to) {
    add_tab(path);
    if (switch_to) m_tabs->setCurrentIndex(m_tabs->count() - 1);
}

void MainWindow::open_folder_in_new_window(const QString& path) {
    auto* w = new MainWindow();
    w->show();
    if (!path.isEmpty()) w->open_folder(path);
}

void MainWindow::on_folder_selected(const QString& path_ref) {
    const QString path = path_ref;
    if (path == m_current_dir) return;
    if (m_navigating) {
        return;
    }
    if (m_rename_just_finished) {
        return;
    }
    // Blokuj nawigację gdy canvas ma aktywny rename
    if (auto* g = current_grid())
        if (g->canvas() && g->canvas()->is_renaming()) return;
    navigate_to(path);
}

void MainWindow::on_folder_in_new_tab(const QString& path) {
    add_tab(path);
    m_tabs->setCurrentIndex(m_tabs->count() - 1);
}

void MainWindow::on_folder_in_new_window(const QString& path) {
    open_folder_in_new_window(path);
}

void MainWindow::load_meta_async(const QString& path) {
    if (path.isEmpty()) return;
    if (path == m_meta_panel->current_path()) return;
    int gen = ++m_meta_gen;
    m_meta_future = QtConcurrent::run([this, path, gen]() -> FileMetadata {
        return MetaStore::load(path);
    });
    auto* watcher = new QFutureWatcher<FileMetadata>(this);
    connect(watcher, &QFutureWatcher<FileMetadata>::finished,
            this, [this, watcher, gen, path]() {
                watcher->deleteLater();
                if (gen != m_meta_gen) return;
                m_meta_panel->load(watcher->result());
            });
    watcher->setFuture(m_meta_future);
}

void MainWindow::on_primary_changed(const QString& path) {
    if (m_view_mode == CompareMode::Single)
        m_preview_panel->load(path);

    // Odśwież MetaPanel asynchronicznie — nie blokuj UI wątku
    if (!path.isEmpty() && !m_meta_panel->is_editing_filename())
        load_meta_async(path);

    if (m_view_mode != CompareMode::Single) {
        auto* g = current_grid();
        if (g && g->selected_paths().isEmpty())
            update_compare_view();
    }
}

void MainWindow::on_selection_changed(const QStringList& paths) {
    if (sender() != current_grid()) return;
    int n = paths.size();
    m_status_count->setText(n > 0
        ? QString("%1 zaznaczon%2")
              .arg(n).arg(n == 1 ? "y" : "ych")
        : "");

    auto* g = current_grid();
    if (!g) return;

    // Problem 7: metadane pokazują JEDNO zaznaczone
    if (n == 1) {
        // Nie ładuj ponownie jeśli to ten sam plik co primary
        // (primary_changed już uruchomił load_meta_async)
        auto* g2 = current_grid();
        if (!g2 || paths.first() != g2->primary_path())
            load_meta_async(paths.first());
    } else if (n == 0) {
        if (!g->primary_path().isEmpty())
            load_meta_async(g->primary_path());
    }
    // n > 1 → nie zmieniaj metadanych

    // Problem 8: split — zaznaczone do porównania, brak zaznaczonych → sąsiednie
    if (m_view_mode != CompareMode::Single)
        update_compare_view();
}

// ─── Tryb porównania ──────────────────────────────────────────────────────────

void MainWindow::on_view_mode_changed(CompareMode mode) {
    m_view_mode = mode;

    // Przełącz widoczny widget w stacku
    auto* container = m_preview_dock->widget();
    auto* stack = qobject_cast<QStackedLayout*>(container->layout());
    if (stack) {
        stack->setCurrentIndex(mode == CompareMode::Single ? 0 : 1);
    }

    m_compare_view->set_mode(mode);

    if (mode == CompareMode::Single) {
        // Wróć do normalnego podglądu
        auto* g = current_grid();
        if (g && !g->primary_path().isEmpty())
            m_preview_panel->load(g->primary_path());
    } else {
        update_compare_view();
    }
}

void MainWindow::update_compare_view() {
    auto* g = current_grid();
    if (!g) return;

    QStringList sel = g->selected_paths();
    int needed = (m_view_mode == CompareMode::Split2) ? 2 : 4;

    // Odfiltruj foldery
    QStringList files;
    for (const auto& p : sel) {
        if (QFileInfo(p).isFile()) files << p;
        if (files.size() >= needed) break;
    }

    // Problem 8: brak zaznaczonych w split → pokaż sąsiednie wokół primary
    if (files.isEmpty()) {
        QStringList visible = g->visible_file_paths();  // tylko pliki, bez folderów
        QString prim = g->primary_path();
        int prim_idx = visible.indexOf(prim);
        if (prim_idx < 0 && !visible.isEmpty()) prim_idx = 0;

        if (prim_idx >= 0) {
            // Wycentruj needed zdjęć wokół primary
            int start = qMax(0, prim_idx - needed / 2);
            int end   = qMin(visible.size() - 1, start + needed - 1);
            start = qMax(0, end - needed + 1);
            for (int i = start; i <= end; ++i)
                files << visible[i];
        }
    }

    m_compare_view->set_paths(files);
}

void MainWindow::open_in_external_editor(const QStringList& paths, bool as_layer) {
    if (paths.isEmpty()) return;

    QString editor = SettingsDialog::external_editor_path().trimmed();
    if (editor.isEmpty()) {
        QMessageBox::information(this, "Zewnętrzny edytor",
            "Nie ustawiono zewnętrznego edytora.\n"
            "Przejdź do Narzędzia → Ustawienia → Zewnętrzny edytor.");
        return;
    }

    // Tryb: otwórz jako warstwę w Photoshopie przez Lape plugin (IPC)
    if (as_layer || SettingsDialog::external_editor_as_layer()) {
        // Wysyłamy pliki przez IPC do otwartego Photoshopa
        // Lape plugin obsługuje "place_as_layer" command
        if (m_ipc && m_ipc->is_connected()) {
            for (const QString& p : paths)
                m_ipc->open_as_layer(p);
            return;
        }
        // Fallback — PS nie jest uruchomiony, otwórz normalnie
        QMessageBox::information(this, "Photoshop nie połączony",
            "Photoshop nie jest uruchomiony lub Lape plugin nie jest aktywny.\n"
            "Otwieranie plików normalnie.");
    }

    // Standardowe uruchomienie: program [args] plik1 plik2 ...
    QString args_str = SettingsDialog::external_editor_args().trimmed();
    QStringList args;
    if (!args_str.isEmpty())
        args = args_str.split(' ', Qt::SkipEmptyParts);
    args += paths;

    bool ok = QProcess::startDetached(editor, args);
    if (!ok) {
        QMessageBox::warning(this, "Zewnętrzny edytor",
            QString("Nie udało się uruchomić:\n%1\n\n"
                    "Sprawdź ścieżkę w Narzędzia → Ustawienia.").arg(editor));
    }
}

void MainWindow::on_open_in_lape(const QString& path) {
    m_ipc->open_file(path);
    if (!m_ipc->is_connected())
        statusBar()->showMessage("Lape nie jest uruchomiony.", 3000);
}

void MainWindow::on_open_as_layer(const QString& path) {
    m_ipc->open_as_layer(path);
}

void MainWindow::on_ipc_status_changed() { update_ipc_indicator(); }

void MainWindow::update_ipc_indicator() {
    if (m_ipc->is_connected()) {
        m_ipc_indicator->setText("● Lape: połączono");
        m_ipc_indicator->setStyleSheet("color: #2ECC71;");
    } else {
        m_ipc_indicator->setText("● Lape: rozłączono");
        m_ipc_indicator->setStyleSheet("color: #888;");
    }
}

// ─── Context Menu ─────────────────────────────────────────────────────────────

void MainWindow::on_context_menu_background(const QPoint& pos) {
    QMenu menu(this);
    auto* g = current_grid();

    // Wklej — jeśli coś jest w wewnętrznym lub systemowym schowku
    bool has_clipboard = false;
    if (g && !g->clipboard_empty()) {
        has_clipboard = true;
    } else {
        const QMimeData* mime = QApplication::clipboard()->mimeData();
        has_clipboard = mime && mime->hasUrls();
    }

    if (has_clipboard) {
        menu.addAction("⎘  Wklej  [Ctrl+V]", [this]() {
            if (auto* gr = current_grid()) gr->paste_here();
        });
        menu.addSeparator();
    }

    menu.addAction("➕  Nowa zakładka  [Ctrl+T]", this, &MainWindow::action_new_tab);
    menu.addAction("📁  Nowy folder tutaj",        this, &MainWindow::action_new_folder);
    menu.addSeparator();
    menu.addAction("Zaznacz wszystko  [Ctrl+A]", this, &MainWindow::action_select_all);

    menu.exec(pos);
}

void MainWindow::on_context_menu(const QStringList& paths, const QPoint& pos) {
    QMenu menu(this);
    auto* g = current_grid();
    bool has_items = !paths.isEmpty();
    bool single    = paths.size() == 1;
    bool multi     = paths.size() > 1;

    // ── Otwieranie ────────────────────────────────────────────────────────────
    if (single) {
        QFileInfo fi(paths.first());
        if (fi.isDir()) {
            menu.addAction("📁  Otwórz", [this, paths]() { on_folder_selected(paths.first()); });
            menu.addAction("📁  Otwórz w nowej zakładce", [this, paths]() { on_folder_in_new_tab(paths.first()); });
            menu.addAction("🗔   Otwórz w nowym oknie",   [this, paths]() { on_folder_in_new_window(paths.first()); });
        } else {
            menu.addAction("Otwórz w Lape  [↵]",   [this, paths]() { on_open_in_lape(paths.first()); });
            menu.addAction("Otwórz jako warstwę",   [this, paths]() { on_open_as_layer(paths.first()); });
            // Zewnętrzny edytor
            QString ed = SettingsDialog::external_editor_path();
            if (!ed.isEmpty()) {
                QString ed_name = QFileInfo(ed).baseName();
                menu.addAction(QString("Otwórz w %1  [E]").arg(ed_name),
                               [this, paths]() { open_in_external_editor(paths, false); });
                menu.addAction(QString("Otwórz jako warstwę  [Shift+E]"),
                               [this, paths]() { open_in_external_editor(paths, true); });
            }
            menu.addAction("🔍  Podgląd  [Spacja]", [this, g, paths]() {
                if (!g) return;
                QStringList files;
                for (const auto& f : g->visible_file_paths()) files << f;
                int idx = files.indexOf(paths.first());
                if (idx >= 0) emit g->fullscreen_requested(files, idx);
            });
        }
        menu.addSeparator();
    } else if (multi) {
        menu.addAction(QString("Otwórz %1 pliki w Lape").arg(paths.size()),
                       [this, paths]() { m_ipc->batch_open(paths); });
        menu.addSeparator();
    }

    // ── Edycja pliku ──────────────────────────────────────────────────────────
    if (has_items) {
        if (single)
            menu.addAction("✎  Zmień nazwę  [F2]", [this]() {
                if (auto* gr = current_grid()) gr->start_rename_selected(); });
        else
            menu.addAction("✎  Zmień nazwy wsadowo...", this, &MainWindow::action_batch_rename);

        menu.addSeparator();
        menu.addAction("⎘  Kopiuj  [Ctrl+C]", [this]() {
            if (auto* gr = current_grid()) gr->copy_selected(false); });
        menu.addAction("✂  Wytnij  [Ctrl+X]", [this]() {
            if (auto* gr = current_grid()) gr->copy_selected(true); });
        menu.addAction("⎘  Wklej  [Ctrl+V]", [this]() {
            if (auto* gr = current_grid()) gr->paste_here(); });
        menu.addSeparator();
        menu.addAction("⧉  Duplikuj", [this, paths]() {
            if (auto* gr = current_grid()) gr->duplicate_selected(); });
        menu.addAction("📤  Eksportuj...", [this]() { action_export(); });
        menu.addAction("📚  Dodaj do kolekcji...", [this, paths]() {
            CollectionDialog::add_files_to_collection(paths, this);
            m_folder_panel->refresh();
        });
        // Usuń z kolekcji gdy jesteśmy w trybie kolekcji
        if (!m_current_collection_id.isEmpty()) {
            menu.addAction("📚  Usuń z kolekcji", [this, paths]() {
                auto cols = CollectionStore::load_all();
                for (auto& col : cols) {
                    if (col.id == m_current_collection_id) {
                        for (const QString& p : paths)
                            col.static_paths.removeAll(p);
                        CollectionStore::save_all(cols);
                        // Odśwież widok kolekcji
                        if (auto* g = current_grid())
                            g->remove_items_in_place(paths);
                        m_folder_panel->refresh();
                        break;
                    }
                }
            });
        }
        menu.addSeparator();
    }

    // ── Nowy folder / zakładka ────────────────────────────────────────────────
    menu.addAction("➕  Nowa zakładka  [Ctrl+T]", this, &MainWindow::action_new_tab);
    menu.addAction("📁  Nowy folder tutaj",        this, &MainWindow::action_new_folder);

    // ── Ocena i etykieta (dla plików I folderów) ──────────────────────────────
    if (has_items) {
        menu.addSeparator();
        auto* rate_sub = menu.addMenu("⭐  Ustaw ocenę");
        for (int i = 1; i <= 5; ++i) {
            QString stars(i, QChar(0x2605));
            rate_sub->addAction(stars, [this, i]() { action_rate(i); });
        }
        rate_sub->addSeparator();
        rate_sub->addAction("Usuń ocenę", [this]() { action_rate(0); });

        // Etykiety kolorowe — dynamiczne z LabelConfig
        auto* lbl_sub = menu.addMenu("🏷  Etykieta koloru");
        auto custom_labels = LabelConfig::load();
        // ColorLabel enum: None=0, Red=1, Yellow=2, Green=3, Blue=4, Purple=5
        const ColorLabel lbl_enums[] = {
            ColorLabel::Red, ColorLabel::Yellow, ColorLabel::Green,
            ColorLabel::Blue, ColorLabel::Purple
        };
        for (int i = 0; i < custom_labels.size(); ++i) {
            const auto& cl = custom_labels[i];
            // Ikona koloru w akcji
            QPixmap px(14, 14); px.fill(cl.color);
            QAction* act = lbl_sub->addAction(QIcon(px), cl.name,
                [this, lbl = lbl_enums[i]]() { action_label(lbl); });
            (void)act;
        }
        lbl_sub->addSeparator();
        lbl_sub->addAction("✖  Brak etykiety", [this]() { action_label(ColorLabel::None); });
        lbl_sub->addSeparator();
        lbl_sub->addAction("✏  Edytuj etykiety...", this, [this]() {
            ColorLabelEditor dlg(this);
            if (dlg.exec() == QDialog::Accepted) {
                LabelConfig::save(dlg.labels());
                // Odśwież siatkę żeby pokazać nowe kolory
                if (auto* gr = current_grid()) gr->reapply_filter();
            }
        });
    }

    // ── Właściwości / informacje ──────────────────────────────────────────────
    if (single) {
        menu.addSeparator();
        menu.addAction("Właściwości", [this, paths]() {
            action_file_info(paths.first()); });
    }

    // ── Usuwanie ──────────────────────────────────────────────────────────────
    if (has_items) {
        menu.addSeparator();
        menu.addAction(
            QString("🗑  Usuń%1  [Del]")
                .arg(multi ? QString(" (%1)").arg(paths.size()) : ""),
            this, [this]() { if (auto* gr = current_grid()) gr->delete_selected(); });
    }

    menu.exec(pos);
}

void MainWindow::action_file_info(const QString& path) {
    QFileInfo fi(path);
    QString info;
    info += QString("<b>%1</b><br>").arg(fi.fileName());
    info += QString("<br><b>Ścieżka:</b> %1").arg(fi.absolutePath());
    info += QString("<br><b>Typ:</b> %1").arg(fi.isDir() ? "Folder" : fi.suffix().toUpper() + " plik");
    info += QString("<br><b>Rozmiar:</b> %1").arg(
        fi.isDir() ? "—" :
        fi.size() < 1024*1024
            ? QString("%1 KB").arg(fi.size()/1024)
            : QString("%1 MB").arg(fi.size()/1024/1024));
    info += QString("<br><b>Zmodyfikowano:</b> %1").arg(
        fi.lastModified().toString("yyyy-MM-dd HH:mm:ss"));
    info += QString("<br><b>Utworzono:</b> %1").arg(
        fi.birthTime().toString("yyyy-MM-dd HH:mm:ss"));

    // Metadane EXIF jeśli plik
    if (!fi.isDir()) {
        auto meta = MetaStore::load(path);
        const auto& ex = meta.exif;
        if (!ex.camera_make.isEmpty() || !ex.camera_model.isEmpty())
            info += QString("<br><br><b>Aparat:</b> %1 %2")
                .arg(ex.camera_make).arg(ex.camera_model).trimmed();
        if (!ex.lens.isEmpty())
            info += QString("<br><b>Obiektyw:</b> %1").arg(ex.lens);
        if (!ex.date_taken.isEmpty())
            info += QString("<br><b>Data zdjęcia:</b> %1").arg(ex.date_taken);
        if (ex.exposure_time > 0)
            info += QString("<br><b>Ekspozycja:</b> 1/%1s  f/%2  ISO %3")
                .arg(qRound(1.0 / ex.exposure_time))
                .arg(ex.fnumber, 0, 'f', 1)
                .arg(ex.iso);
        if (ex.width > 0)
            info += QString("<br><b>Wymiary:</b> %1 × %2 px")
                .arg(ex.width).arg(ex.height);
    }

    QMessageBox::information(this, "Właściwości pliku",
        QString("<html>%1</html>").arg(info));
}

// ─── Akcje ────────────────────────────────────────────────────────────────────

void MainWindow::action_open_folder() {
    QString dir = QFileDialog::getExistingDirectory(this, "Otwórz folder",
        m_current_dir.isEmpty() ? QDir::homePath() : m_current_dir);
    if (!dir.isEmpty()) navigate_to(dir);
}

void MainWindow::action_select_all() {
    if (auto* g = current_grid()) g->select_all();
}

void MainWindow::action_rate(int stars) {
    auto* g = current_grid(); if (!g) return;
    for (const auto& path : g->selected_paths()) {
        FileMetadata meta = MetaStore::load(path);
        meta.rating = stars;
        MetaStore::save(meta);
        g->refresh_metadata(path);
    }
    if (!g->primary_path().isEmpty())
        m_meta_panel->load(MetaStore::load(g->primary_path()));
}

void MainWindow::action_label(ColorLabel label) {
    auto* g = current_grid(); if (!g) return;
    bool has_dirs = false;
    for (const auto& path : g->selected_paths()) {
        FileMetadata meta = MetaStore::load(path);
        meta.color_label = label;
        MetaStore::save(meta);
        g->refresh_metadata(path);
        if (QFileInfo(path).isDir()) has_dirs = true;
    }
    // Odśwież panel folderów jeśli zmieniono etykietę folderu
    if (has_dirs) m_folder_panel->refresh();
}

void MainWindow::action_batch_rename() {
    auto* g = current_grid(); if (!g) return;
    // Problem 5: użyj kolejności z siatki (m_visible) zamiast QSet::selected_paths()
    QStringList paths = g->selected_paths_ordered();
    if (paths.isEmpty()) { statusBar()->showMessage("Zaznacz pliki.", 3000); return; }
    BatchRenameDialog dlg(paths, this);
    if (dlg.exec() != QDialog::Accepted) return;
    // Problem 1/2: aktualizuj w miejscu — bez przeładowania folderu
    g->apply_batch_rename(dlg.renamed_pairs());
}

void MainWindow::action_new_folder() {
    if (m_current_dir.isEmpty()) return;
    QString base = m_current_dir + "/Nowy folder", name = base;
    int n = 2;
    while (QDir(name).exists()) name = base + " " + QString::number(n++);
    if (!QDir().mkdir(name)) {
        statusBar()->showMessage("Nie można utworzyć folderu.", 3000);
        return;
    }
    auto* g = current_grid();
    if (!g) return;
    // Problem 2: dodaj folder do siatki w miejscu — bez load_folder
    g->add_item_in_place(name);
    // Zaznacz i uruchom rename natychmiast
    QTimer::singleShot(0, this, [this, name]() {
        if (auto* gr = current_grid())
            gr->select_and_rename(name);
    });
}

void MainWindow::action_purge_cache() {
    m_thumb_cache->purge_missing();
    statusBar()->showMessage(
        QString("Cache wyczyszczony. %.1f MB").arg(m_thumb_cache->db_size_mb()), 4000);
}

void MainWindow::action_print() {
    auto* g = current_grid();
    if (!g) return;
    QStringList paths;
    for (const QString& p : g->selected_paths())
        if (!QFileInfo(p).isDir()) paths << p;
    if (paths.isEmpty()) {
        statusBar()->showMessage("Zaznacz pliki do drukowania.", 3000);
        return;
    }
    PrintDialog dlg(paths, this);
    dlg.exec();
}

void MainWindow::action_export() {
    auto* g = current_grid();
    if (!g) return;
    QStringList paths;
    for (const QString& p : g->selected_paths())
        if (!QFileInfo(p).isDir()) paths << p;
    if (paths.isEmpty()) {
        statusBar()->showMessage("Zaznacz pliki do eksportu.", 3000);
        return;
    }
    ExportDialog dlg(paths, this);
    dlg.exec();
}

void MainWindow::action_advanced_search() {
    auto* g = current_grid();
    if (!g) return;
    AdvancedSearchDialog dlg(g->current_filter(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    GridFilter f = dlg.result();
    // Zachowaj sort_mode z istniejącego filtra
    f.sort_mode = g->current_filter().sort_mode;
    g->set_filter(f);
    // Pokaż informację w statusbar gdy aktywne zaawansowane filtry
    if (f.is_advanced()) {
        statusBar()->showMessage("Zaawansowane wyszukiwanie aktywne", 0);
    } else {
        statusBar()->clearMessage();
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    // Globalny przechwyt Enter/Return — zanim QToolButton lub inny widget go obsłuży.
    // Filter jest instalowany na qApp — dostaje wszystkie KeyPress eventy w aplikacji.
    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            QWidget* fw = QApplication::focusWidget();
            // Pomiń gdy fokus na QLineEdit (rename, BreadcrumbBar) — Enter zatwierdza tekst
            if (qobject_cast<QLineEdit*>(fw)) {
                // Jeśli to rename edit w canvas — nie rób nic, QLineEdit sam obsłuży Enter
                // Przepuść do QLineEdit przez QMainWindow::eventFilter
                return QMainWindow::eventFilter(obj, event);
            }
            // Zablokuj gdy fokus na QAbstractButton (QToolButton "+" w toolbarze).
            // Kliknięcie myszą daje button-owi fokus mimo setFocusPolicy(NoFocus).
            // Bez blokady Enter aktywuje przycisk → action_new_tab.
            if (qobject_cast<QAbstractButton*>(fw)) {
                // Zużyj event — nie otwieraj folderu ani nie aktywuj przycisku
                // Zamiast tego — przenieś fokus na siatkę i otwórz
                if (auto* g = current_grid()) { g->setFocus(); g->open_primary(); }
                event->accept();
                return true;
            }
            // Każdy MainWindow ma swój event filter zainstalowany na qApp.
            // Filter jest wywoływany RAZ dla każdego eventu — z ostatnio zainstalowanego.
            // Jeśli fokus jest w innym MainWindow — niech to inne MainWindow obsłuży.
            QWidget* win = fw ? fw->window() : this;
            auto* mw = qobject_cast<MainWindow*>(win);
            if (mw && mw != this) {
                qDebug() << "[Lape] EventFilter: fokus w innym MainWindow — pomijam";
                return QMainWindow::eventFilter(obj, event);
            }
            // ZAWSZE zużyj event — niezależnie czy mamy primary czy nie.
            // Lepiej nic nie robić niż otworzyć przypadkową zakładkę.
            if (auto* g = current_grid()) {
                // Blokuj open_primary gdy: (1) canvas ma aktywny rename,
                // lub (2) właśnie zakończył rename (m_rename_just_finished)
                bool canvas_renaming = g->canvas() && g->canvas()->is_renaming();
                if (canvas_renaming || m_rename_just_finished) {

                } else {
                    g->open_primary();
                }
            } else {
                qDebug() << "[Lape] EventFilter: brak current_grid()";
            }
            event->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::keyPressEvent(QKeyEvent* e) {
    // Ignoruj gdy fokus na polu tekstowym
    bool text_focused = qobject_cast<QLineEdit*>(QApplication::focusWidget()) != nullptr;

    if (!text_focused) {
        // E — otwórz w zewnętrznym edytorze
        if (e->key() == Qt::Key_E && !(e->modifiers() & Qt::ShiftModifier)) {
            auto* g = current_grid();
            if (g) {
                QStringList paths = g->selected_paths();
                if (paths.isEmpty() && !g->primary_path().isEmpty())
                    paths << g->primary_path();
                open_in_external_editor(paths, false);
                e->accept(); return;
            }
        }
        // Shift+E — otwórz jako warstwę w Photoshopie
        if (e->key() == Qt::Key_E && (e->modifiers() & Qt::ShiftModifier)) {
            auto* g = current_grid();
            if (g) {
                QStringList paths = g->selected_paths();
                if (paths.isEmpty() && !g->primary_path().isEmpty())
                    paths << g->primary_path();
                open_in_external_editor(paths, true);
                e->accept(); return;
            }
        }
        // Enter/Return — deleguj do aktywnej siatki
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (auto* g = current_grid()) {
                g->open_primary();
                e->accept(); return;
            }
        }
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::action_about() {
    QMessageBox::about(this, "O Lape's Eye",
        "<h3>Lape's Eye v0.1</h3>"
        "<p>Przeglądarka mediów dla Lape Photo Editor.</p>"
        "<p>Alt+← / Alt+→ nawigacja | Ctrl+T nowa zakładka | "
        "strzałki nawigacja | F2 rename | Del usuń</p>"
        "<p>Qt6 + libraw + libexiv2</p>");
}

// ─── Settings ─────────────────────────────────────────────────────────────────

void MainWindow::save_settings() {
    QSettings s("Lape", "LapesEye");
    s.setValue("geometry",   saveGeometry());
    s.setValue("state",      saveState());
    s.setValue("thumb_size", m_zoom_slider->value());
    QStringList tab_dirs;
    for (int i = 0; i < m_tabs->count(); ++i)
        tab_dirs << m_tabs->tabToolTip(i);
    s.setValue("tab_dirs",   tab_dirs);
    s.setValue("active_tab", m_tabs->currentIndex());
}

void MainWindow::load_settings() {
    QSettings s("Lape", "LapesEye");
    if (s.contains("geometry")) restoreGeometry(s.value("geometry").toByteArray());
    if (s.contains("state"))    restoreState(s.value("state").toByteArray());
    if (s.contains("thumb_size")) {
        int sz = s.value("thumb_size", 220).toInt();
        m_zoom_slider->setValue(sz);
        // Zastosuj natychmiast do wszystkich gridów — bez debounce timera
        m_thumb_worker->set_thumb_size(sz);
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto* g = qobject_cast<ThumbnailGrid*>(m_tabs->widget(i))) {
                g->apply_thumb_size_now(sz);
            }
        }
    }
    QStringList tab_dirs = s.value("tab_dirs").toStringList();
    bool first = true;
    for (const auto& dir : tab_dirs) {
        if (dir.isEmpty() || !QDir(dir).exists()) continue;
        if (first) {
            navigate_to(dir);
            first = false;
        } else {
            add_tab(dir);
        }
    }
    int active = s.value("active_tab", 0).toInt();
    if (active < m_tabs->count()) m_tabs->setCurrentIndex(active);
}

void MainWindow::closeEvent(QCloseEvent* e) { save_settings(); e->accept(); }

} // namespace LapesEye
