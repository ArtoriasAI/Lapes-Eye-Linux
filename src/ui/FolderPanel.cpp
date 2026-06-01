#include "LapesEye/ui/FolderPanel.h"
#include "LapesEye/core/Collection.h"
#include "LapesEye/ui/ColorLabelEditor.h"
#include "LapesEye/core/MetaStore.h"

#include <QVBoxLayout>
#include <QDir>
#include <QStandardPaths>
#include <QMimeData>
#include <QUrl>
#include <QSettings>
#include <QStorageInfo>
#include <QFileInfo>
#include <QFont>
#include <QListWidgetItem>
#include <QTimer>
#include <QMenu>
#include <QCursor>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace LapesEye {

static const int RolePath    = Qt::UserRole;
static const int RoleType    = Qt::UserRole + 1;
static const int RoleArrow   = Qt::UserRole + 2; // rect obszaru strzałki
// type: "header", "header_toggle", "place", "expandable"

FolderPanel::FolderPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Subklasa QListWidget z bezpośrednią obsługą drag eventów
    class DropListWidget : public QListWidget {
    public:
        explicit DropListWidget(FolderPanel* p) : QListWidget(p), panel(p) {
            setAcceptDrops(true);
            viewport()->setAcceptDrops(true);
        }
    protected:
        void dragEnterEvent(QDragEnterEvent* e) override { panel->dragEnterEvent(e); }
        void dragMoveEvent(QDragMoveEvent* e) override {
            // Pozycja jest już w układzie viewport/listy
            panel->handle_drag_move(e, itemAt(e->position().toPoint()));
        }
        void dropEvent(QDropEvent* e) override {
            panel->handle_drop(e, itemAt(e->position().toPoint()));
        }
    private:
        FolderPanel* panel;
    };
    m_list = new DropListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setMouseTracking(true);
    setAcceptDrops(true);  // FolderPanel też akceptuje gdy kursor jest poza listą
    m_list->setStyleSheet(R"(
        QListWidget { background:#1e1e1e; border:none; outline:none; }
        QListWidget::item { padding:4px 6px 4px 8px; color:#ccc; }
        QListWidget::item:selected { background:#2D7DD2; color:white; }
        QListWidget::item:hover:!selected { background:#2a2a2a; }
    )");
    layout->addWidget(m_list);

    QSettings s("Lape", "LapesEye");
    m_favorites   = s.value("favorites").toStringList();
    m_recent_dirs = s.value("recent_dirs").toStringList();
    m_recent_open = s.value("recent_open", false).toBool();

    build_list();

    // Klik lewym = navigate do folderu (lub toggle sekcji)
    QObject::connect(m_list, &QListWidget::itemClicked,
                     this, &FolderPanel::on_item_clicked);

    // Problem 1: menu kontekstowe PPM na folderze
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(m_list, &QListWidget::customContextMenuRequested,
                     this, &FolderPanel::on_context_menu_requested);

    m_dev_timer = new QTimer(this);
    m_dev_timer->setInterval(3000);
    QObject::connect(m_dev_timer, &QTimer::timeout,
                     this, &FolderPanel::refresh_devices);
    m_dev_timer->start();
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

void FolderPanel::add_header(const QString& text) {
    auto* item = new QListWidgetItem(text, m_list);
    item->setFlags(Qt::NoItemFlags);
    QFont f = item->font();
    f.setPointSize(8); f.setBold(true);
    item->setFont(f);
    item->setForeground(QColor(0x77,0x77,0x77));
    item->setBackground(QColor(0x22,0x22,0x22));
    item->setData(RoleType, "header");
}

void FolderPanel::add_toggle_header(const QString& text, const QString& section_id,
                                     bool is_open)
{
    QString arrow = is_open ? "▾ " : "▸ ";
    auto* item = new QListWidgetItem(arrow + text, m_list);
    QFont f = item->font();
    f.setPointSize(8); f.setBold(true);
    item->setFont(f);
    item->setForeground(QColor(0x99,0x99,0x99));
    item->setBackground(QColor(0x22,0x22,0x22));
    item->setData(RoleType, "header_toggle");
    item->setData(RolePath, section_id);
}

// Zwraca przyciemniony kolor etykiety folderu (lub invalid jeśli brak)
static QColor folder_label_color(const QString& path) {
    if (path.isEmpty() || !QFileInfo::exists(path)) return {};
    auto meta = MetaStore::load(path);
    if (meta.color_label == ColorLabel::None) return {};
    int idx = static_cast<int>(meta.color_label) - 1;
    auto labels = LabelConfig::load();
    if (idx < 0 || idx >= labels.size()) return {};
    QColor lc = labels[idx].color;
    // Przyciemniona wersja — pasek boczny jest ciemny
    return QColor(lc.red()/5, lc.green()/5, lc.blue()/5);
}

void FolderPanel::add_place(const QString& icon, const QString& label,
                             const QString& path, int indent, bool current)
{
    if (!path.isEmpty() && !QFileInfo::exists(path)) return;
    auto* item = new QListWidgetItem(
        QString(indent * 4, ' ') + icon + "  " + label, m_list);
    item->setData(RolePath, path);
    item->setData(RoleType, "place");
    item->setToolTip(path);
    if (current) {
        item->setBackground(QColor(0x1a, 0x3a, 0x5c));
        item->setForeground(Qt::white);
    } else {
        QColor lc = folder_label_color(path);
        if (lc.isValid()) item->setBackground(lc);
    }
}

void FolderPanel::add_expandable(const QString& icon, const QString& label,
                                  const QString& path, int indent, bool current)
{
    bool exp = m_expanded.contains(path);
    // Strzałka ▸/▾ po lewej, potem ikona i nazwa
    QString arrow = exp ? "▾" : "▸";
    auto* item = new QListWidgetItem(
        QString(indent * 4, ' ') + arrow + " " + icon + " " + label, m_list);
    item->setData(RolePath, path);
    item->setData(RoleType, "expandable");
    item->setData(Qt::UserRole + 10, indent);  // zapisz głębokość dla wykrywania kliknięcia strzałki
    item->setToolTip(path);
    QFont f = item->font();
    if (indent == 0) f.setBold(true);
    item->setFont(f);
    if (current) {
        item->setBackground(QColor(0x1a, 0x3a, 0x5c));
        item->setForeground(Qt::white);
    } else {
        QColor lc = folder_label_color(path);
        if (lc.isValid()) item->setBackground(lc);
    }
}

void FolderPanel::add_children(const QString& path, int indent,
                                const QStringList& skip_names)
{
    if (!m_expanded.contains(path)) return;
    const auto subdirs = QDir(path).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const auto& sub : subdirs) {
        QString name = sub.fileName();
        if (skip_names.contains(name) || name.startsWith('.')) continue;
        QString subpath = sub.absoluteFilePath();
        bool is_cur = (subpath == m_current_path);
        bool has_children = !QDir(subpath).entryList(
            QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
        if (has_children)
            add_expandable("📁", name, subpath, indent, is_cur);
        else
            add_place("📁", name, subpath, indent, is_cur);
        add_children(subpath, indent + 1, {});
    }
}

// ─── set_current_path — podświetl aktualny folder ────────────────────────────

void FolderPanel::set_current_path(const QString& path) {
    if (path == m_current_path) return;
    m_current_path = path;
    m_setting_path = true;

    // Auto-rozwiń ścieżkę do aktualnego folderu w urządzeniach
    // Znajdź punkt montowania dla tej ścieżki
    QString mount_point;
    for (const QStorageInfo& vol : QStorageInfo::mountedVolumes()) {
        if (path.startsWith(vol.rootPath()) &&
            vol.rootPath().length() > mount_point.length())
            mount_point = vol.rootPath();
    }

    if (!mount_point.isEmpty()) {
        // Rozwiń każdy segment ścieżki od mount point do aktualnego folderu
        QString seg = mount_point;
        m_expanded.insert(seg);
        QString rel = path.mid(mount_point.length());
        for (const QString& part : rel.split('/', Qt::SkipEmptyParts)) {
            seg = seg.endsWith('/') ? seg + part : seg + '/' + part;
            if (QDir(seg).exists()) m_expanded.insert(seg);
            if (seg == path) break;
        }
    }

    build_list();
    highlight_current();
    m_setting_path = false;
}

void FolderPanel::highlight_current() {
    // Przewiń do zaznaczonego elementu
    for (int i = 0; i < m_list->count(); ++i) {
        auto* item = m_list->item(i);
        if (item->data(RolePath).toString() == m_current_path) {
            m_list->scrollToItem(item, QAbstractItemView::EnsureVisible);
            break;
        }
    }
}

// ─── Budowanie listy ──────────────────────────────────────────────────────────

void FolderPanel::build_list() {
    m_list->clear();

    // ── Kolekcje ──────────────────────────────────────────────────────────────
    auto cols = CollectionStore::load_all();
    if (!cols.isEmpty()) {
        add_header("  Kolekcje");
        for (const auto& col : cols) {
            QString icon = col.type == CollectionType::Smart ? "🔍" : "📚";
            auto* item = new QListWidgetItem(
                "  " + icon + "  " + col.name, m_list);
            item->setData(RolePath,  "collection:" + col.id);
            item->setData(RoleType,  "collection");
            item->setToolTip(QString("Kolekcja: %1 (%2 plików)")
                .arg(col.name).arg(col.static_paths.size()));
        }
    }

    // ── Miejsca ───────────────────────────────────────────────────────────────
    add_header("  Miejsca");
    auto pl = [&](const QString& ic, const QString& lb, const QString& p) {
        add_place(ic, lb, p, 0, p == m_current_path);
    };
    pl("🏠","Katalog domowy", QDir::homePath());
    pl("🖥","Pulpit",    QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    pl("📄","Dokumenty", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    pl("⬇","Pobrane",   QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    pl("🎵","Muzyka",    QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    pl("🖼","Obrazy",    QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    pl("🎬","Filmy",     QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    QString trash = QDir::homePath() + "/.local/share/Trash/files";
    if (QDir(trash).exists()) pl("🗑","Kosz", trash);

    // ── Ulubione ──────────────────────────────────────────────────────────────
    if (!m_favorites.isEmpty()) {
        add_header("  Ulubione");
        for (const auto& p : m_favorites)
            if (QDir(p).exists())
                add_place("⭐", QFileInfo(p).fileName(), p, 0, p == m_current_path);
    }

    // ── Zdalne (GVFS — tylko Linux) ───────────────────────────────────────────
#ifdef Q_OS_LINUX
    QString gvfs = QString("/run/user/%1/gvfs").arg(::getuid());
    if (QDir(gvfs).exists()) {
        auto entries = QDir(gvfs).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (!entries.isEmpty()) {
            add_header("  Zdalne");
            for (const auto& e : entries)
                add_place("🌐", e.fileName(), e.absoluteFilePath(), 0,
                          e.absoluteFilePath() == m_current_path);
        }
    }
#endif

    // ── Ostatnie ─────────────────────────────────────────────────────────────
    if (!m_recent_dirs.isEmpty()) {
        add_toggle_header("Ostatnie", "recent", m_recent_open);
        if (m_recent_open) {
            int count = 0;
            for (const auto& p : m_recent_dirs) {
                if (!QDir(p).exists()) continue;
                add_place("🕒", QFileInfo(p).fileName(), p, 1, p == m_current_path);
                if (++count >= 3) break;
            }
        }
    }

    // ── Urządzenia ────────────────────────────────────────────────────────────
    add_header("  Urządzenia");

#ifdef Q_OS_WIN
    // Windows: QStorageInfo zwraca C:\, D:\ itp. — bez filtrowania
    for (const QStorageInfo& vol : QStorageInfo::mountedVolumes()) {
        if (!vol.isValid() || !vol.isReady()) continue;
        QString mount = vol.rootPath();
        QString label = vol.displayName();
        if (label.isEmpty()) label = vol.name();
        if (label.isEmpty()) label = mount;

        qint64 total = vol.bytesTotal();
        if (total > 0) {
            double gb = total / (1024.0*1024.0*1024.0);
            label += gb >= 1.0
                ? QString(" (%1 GB)").arg(gb,0,'f',0)
                : QString(" (%1 MB)").arg(total/(1024*1024));
        }
        // Ikona: dysk wymienialny (USB) vs stały
        QString icon = vol.isReadOnly() ? "📀" :
                       (mount.startsWith("A:") || mount.startsWith("B:") ? "💿" :
                        mount == "C:\\" ? "💻" : "💾");
        bool is_cur = (mount == m_current_path);
        bool in_path = !m_current_path.isEmpty() && m_current_path.startsWith(mount);
        add_expandable(icon, label, mount, 0, is_cur || in_path);
        add_children(mount, 1, QStringList{"$RECYCLE.BIN","System Volume Information"});
    }
#else
    // Linux: filtruj pseudo-filesystemy
    static const QStringList SKIP_MOUNTS = {
        "/proc","/sys","/dev","/run/user","/run/lock","/run/systemd",
        "/run/udev","/run/dbus","/run/credentials",
        "/snap","/boot/efi","/boot","/tmp","/var/tmp"
    };
    static const QStringList SKIP_FS = {
        "tmpfs","devtmpfs","sysfs","proc","cgroup","cgroup2","pstore",
        "efivarfs","securityfs","configfs","debugfs","hugetlbfs",
        "mqueue","fusectl","bpf","overlay","squashfs","autofs"
    };
    static const QStringList SKIP_ROOT = {
        "proc","sys","dev","run","tmp","boot","lost+found","snap","initrd","media","mnt"
    };

    for (const QStorageInfo& vol : QStorageInfo::mountedVolumes()) {
        if (!vol.isValid() || !vol.isReady()) continue;
        QString mount = vol.rootPath();
        bool skip = false;
        for (const auto& p : SKIP_MOUNTS)
            if (mount == p || mount.startsWith(p + "/")) { skip=true; break; }
        if (skip) continue;
        if (SKIP_FS.contains(QString::fromLatin1(vol.fileSystemType()))) continue;

        QString label = vol.displayName();
        if (label.isEmpty()) label = vol.name();
        if (label.isEmpty()) label = QFileInfo(mount).fileName();
        if (label.isEmpty()) label = mount;

        qint64 total = vol.bytesTotal();
        if (total > 0) {
            double gb = total / (1024.0*1024.0*1024.0);
            label += gb >= 1.0
                ? QString(" (%1 GB)").arg(gb,0,'f',0)
                : QString(" (%1 MB)").arg(total/(1024*1024));
        }

        QString icon = (mount.startsWith("/media") || mount.startsWith("/run/media"))
                       ? "🔌" : (mount == "/" ? "💻" : "💾");

        bool is_cur = (mount == m_current_path);
        bool in_path = !m_current_path.isEmpty() && m_current_path.startsWith(mount);
        add_expandable(icon, label, mount, 0, is_cur || in_path);
        add_children(mount, 1, mount == "/" ? SKIP_ROOT
                                            : QStringList{"$RECYCLE.BIN","System Volume Information"});
    }
#endif
}

void FolderPanel::refresh_devices() {
    QStringList mounts;
    for (const QStorageInfo& v : QStorageInfo::mountedVolumes())
        mounts << v.rootPath();
    if (mounts != m_last_mounts) {
        m_last_mounts = mounts;
        build_list();
    }
}

// ─── Kliknięcie ───────────────────────────────────────────────────────────────
// WAŻNE: klik w strzałkę (▸/▾) = tylko toggle expand, BEZ nawigacji
//        klik w resztę nazwy = navigate

void FolderPanel::on_item_clicked(QListWidgetItem* item) {
    if (m_setting_path) {
        qDebug() << "[FolderPanel] on_item_clicked: ignoruję — m_setting_path=true, path=" << item->data(RolePath).toString();
        return;
    }
    QString type = item->data(RoleType).toString();
    QString path = item->data(RolePath).toString();

    // Kolekcja
    if (type == "collection") {
        QString col_id = path.mid(QString("collection:").length());
        emit collection_selected(col_id);
        return;
    }

    if (type == "header_toggle") {
        if (path == "recent") {
            m_recent_open = !m_recent_open;
            QSettings s("Lape", "LapesEye");
            s.setValue("recent_open", m_recent_open);
        }
        build_list();
        return;
    }

    if (type == "expandable") {
        QPoint local_pos = m_list->mapFromGlobal(QCursor::pos());
        QRect  item_rect = m_list->visualItemRect(item);
        int    level     = item->data(Qt::UserRole + 10).toInt();

        // Zmierz dokładnie ile pikseli zajmuje prefix przed nazwą folderu
        // Tekst: "    ▸ 📁 NazwaFolderu" (level*4 spacje + strzałka + spacja + ikona + spacja)
        QFontMetrics fm(m_list->font());
        // Prefix to wszystko przed nazwą: spacje + strzałka + " " + ikona + " "
        QString prefix = QString(level * 4, ' ') + "▸  📁 ";  // ~prefix bez nazwy
        int prefix_w   = fm.horizontalAdvance(prefix);
        int arrow_end  = item_rect.left() + prefix_w;

        bool clicked_arrow = (local_pos.x() < arrow_end);

        // Toggle expand
        if (m_expanded.contains(path))
            m_expanded.remove(path);
        else
            m_expanded.insert(path);
        build_list();

        // Nawiguj tylko jeśli NIE kliknięto w strzałkę
        if (!clicked_arrow && !path.isEmpty() && QFileInfo::exists(path))
            emit folder_selected(path);
        return;
    }

    if (!path.isEmpty() && QFileInfo::exists(path))
        emit folder_selected(path);
}

// ─── Ostatnie ────────────────────────────────────────────────────────────────

void FolderPanel::add_recent(const QString& path) {
    if (path.isEmpty()) return;
    m_recent_dirs.removeAll(path);
    m_recent_dirs.prepend(path);
    while (m_recent_dirs.size() > MAX_RECENT) m_recent_dirs.removeLast();
    QSettings s("Lape", "LapesEye");
    s.setValue("recent_dirs", m_recent_dirs);
    if (m_recent_open) build_list();
}

void FolderPanel::on_item_double_clicked(QListWidgetItem* item) {
    // Podwójny klik zawsze nawiguje (nawet dla expandable)
    QString path = item->data(RolePath).toString();
    if (!path.isEmpty() && QFileInfo::exists(path))
        emit folder_selected(path);
}

// ─── Drag & Drop — przenoszenie plików na foldery ────────────────────────────

bool FolderPanel::eventFilter(QObject* /*obj*/, QEvent* /*event*/) {
    return false;  // eventFilter zostawiony dla zgodności z .h
}

void FolderPanel::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
    else e->ignore();
}

void FolderPanel::dragMoveEvent(QDragMoveEvent* e) {
    if (!e->mimeData()->hasUrls()) { e->ignore(); return; }
    e->acceptProposedAction();
}

void FolderPanel::handle_drag_move(QDragMoveEvent* e, QListWidgetItem* item) {
    if (!e->mimeData()->hasUrls()) { e->ignore(); return; }
    if (item) {
        QString path = item->data(RolePath).toString();
        if (QFileInfo(path).isDir()) { e->acceptProposedAction(); return; }
    }
    e->ignore();
}

void FolderPanel::handle_drop(QDropEvent* e, QListWidgetItem* list_item) {
    if (!e->mimeData()->hasUrls()) { e->ignore(); return; }

    QString dest_dir;
    if (list_item) {
        dest_dir = list_item->data(RolePath).toString();
        if (!QFileInfo(dest_dir).isDir()) dest_dir.clear();
    }
    if (dest_dir.isEmpty()) { e->ignore(); return; }

    QStringList moved;
    for (const QUrl& url : e->mimeData()->urls()) {
        QString src = url.toLocalFile();
        if (src.isEmpty() || !QFileInfo::exists(src)) continue;
        QString fname = QFileInfo(src).fileName();
        QString dst   = dest_dir + "/" + fname;
        if (src == dst) continue;
        if (QFileInfo::exists(dst)) {
            QString base = QFileInfo(src).completeBaseName();
            QString ext  = QFileInfo(src).suffix();
            dst = dest_dir + "/" + base + "_kopia" + (ext.isEmpty() ? "" : "." + ext);
        }
        if (QFile::rename(src, dst)) moved << src;
    }
    if (!moved.isEmpty()) {
        e->setDropAction(Qt::MoveAction);
        e->accept();
        emit files_moved_to(dest_dir, moved);
    } else {
        e->ignore();
    }
}

void FolderPanel::dropEvent(QDropEvent* e) {
    // Wywoływany gdy drop na FolderPanel ale poza m_list
    e->ignore();
}

// ─── Menu kontekstowe PPM ─────────────────────────────────────────────────────

void FolderPanel::on_context_menu_requested(const QPoint& pos) {
    auto* item = m_list->itemAt(pos);
    if (!item) return;
    QString path = item->data(RolePath).toString();
    QString type = item->data(RoleType).toString();
    if (type == "header" || type == "header_toggle") return;

    QMenu menu(this);
    menu.setStyleSheet(R"(
        QMenu { background:#252525; border:1px solid #444; color:#ccc; }
        QMenu::item:selected { background:#2D7DD2; color:white; }
        QMenu::separator { height:1px; background:#444; margin:3px 0; }
    )");

    // ── Kolekcja ──────────────────────────────────────────────────────────────
    if (type == "collection") {
        QString col_id = path.mid(QString("collection:").length());
        menu.addAction("▶  Otwórz kolekcję", [this, col_id]() {
            emit collection_selected(col_id);
        });
        menu.addSeparator();
        menu.addAction("✎  Zmień nazwę / edytuj", [this, col_id]() {
            emit collection_edit_requested(col_id);
        });
        menu.addAction("🗑  Usuń kolekcję", [this, col_id]() {
            emit collection_delete_requested(col_id);
        });
        menu.exec(m_list->mapToGlobal(pos));
        return;
    }

    // ── Folder ────────────────────────────────────────────────────────────────
    if (!QFileInfo::exists(path)) return;

    menu.addAction("📁  Otwórz", [this, path]() {
        emit folder_selected(path);
    });
    menu.addAction("➕  Otwórz w nowej karcie", [this, path]() {
        emit folder_open_in_tab(path);
    });
    menu.addAction("🗔   Otwórz w nowym oknie", [this, path]() {
        emit folder_open_in_window(path);
    });
    menu.addSeparator();
    menu.addAction("⭐  Dodaj do ulubionych", [this, path]() {
        if (!m_favorites.contains(path)) {
            m_favorites << path;
            QSettings s("Lape", "LapesEye");
            s.setValue("favorites", m_favorites);
            build_list();
        }
    });

    menu.exec(m_list->mapToGlobal(pos));
}

} // namespace LapesEye