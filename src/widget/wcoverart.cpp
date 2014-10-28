#include <QAction>
#include <QApplication>
#include <QBitmap>
#include <QLabel>
#include <QIcon>
#include <QPainter>

#include "controlobject.h"
#include "widget/wcoverart.h"
#include "widget/wskincolor.h"
#include "library/coverartcache.h"
#include "library/coverartutils.h"
#include "util/math.h"
#include "util/dnd.h"

WCoverArt::WCoverArt(QWidget* parent,
                     ConfigObject<ConfigValue>* pConfig,
                     const QString& group)
        : QWidget(parent),
          WBaseWidget(this),
          m_group(group),
          m_pConfig(pConfig),
          m_bEnable(true),
          m_pMenu(new WCoverArtMenu(this)),
          m_pDlgFullSize(new DlgCoverArtFullSize()) {
    // Accept drops if we have a group to load tracks into.
    setAcceptDrops(!m_group.isEmpty());

    CoverArtCache* pCache = CoverArtCache::instance();
    if (pCache != NULL) {
        connect(pCache, SIGNAL(coverFound(const QObject*, const CoverInfo&, QPixmap)),
                this, SLOT(slotCoverFound(const QObject*, const CoverInfo&, QPixmap)));
    }
    connect(m_pMenu, SIGNAL(coverArtSelected(const CoverArt&)),
            this, SLOT(slotCoverArtSelected(const CoverArt&)));
}

WCoverArt::~WCoverArt() {
    delete m_pMenu;
    delete m_pDlgFullSize;
}

void WCoverArt::setup(QDomNode node, const SkinContext& context) {
    Q_UNUSED(node);
    setMouseTracking(true);

    // Background color
    QColor bgc(255,255,255);
    if (context.hasNode(node, "BgColor")) {
        bgc.setNamedColor(context.selectString(node, "BgColor"));
        setAutoFillBackground(true);
    }
    QPalette pal = palette();
    pal.setBrush(backgroundRole(), WSkinColor::getCorrectColor(bgc));

    // Foreground color
    QColor m_fgc(0,0,0);
    if (context.hasNode(node, "FgColor")) {
        m_fgc.setNamedColor(context.selectString(node, "FgColor"));
    }
    bgc = WSkinColor::getCorrectColor(bgc);
    m_fgc = QColor(255 - bgc.red(), 255 - bgc.green(), 255 - bgc.blue());
    pal.setBrush(foregroundRole(), m_fgc);
    setPalette(pal);

    if (context.hasNode(node, "DefaultCover")) {
        m_defaultCover = QPixmap(context.selectString(node, "DefaultCover"));
    }

    // If no default cover is specified or we failed to load it, fall back on
    // the resource bundle default cover.
    if (m_defaultCover.isNull()) {
        m_defaultCover = QPixmap(CoverArtUtils::defaultCoverLocation());
    }
    m_defaultCoverScaled = scaledCoverArt(m_defaultCover);
}

void WCoverArt::slotCoverArtSelected(const CoverArt& art) {
    if (m_loadedTrack) {
        // Will trigger slotTrackCoverArtUpdated().
        m_loadedTrack->setCoverArt(art);
    }
}

void WCoverArt::slotEnable(bool enable) {
    bool wasDisabled = !m_bEnable && enable;
    m_bEnable = enable;

    if (wasDisabled) {
        slotLoadTrack(m_loadedTrack);
    }

    update();
}

void WCoverArt::slotReset() {
    if (m_loadedTrack) {
        disconnect(m_loadedTrack.data(), SIGNAL(coverArtUpdated()),
                   this, SLOT(slotTrackCoverArtUpdated()));
    }
    m_loadedTrack = TrackPointer();
    m_lastRequestedCover = CoverInfo();
    m_loadedCover = QPixmap();
    m_loadedCoverScaled = QPixmap();
    update();
}

void WCoverArt::slotCoverFound(const QObject* pRequestor, const CoverInfo& info,
                               QPixmap pixmap) {
    if (!m_bEnable) {
        return;
    }

    if (pRequestor == this && m_loadedTrack &&
            m_loadedTrack->getId() == info.trackId) {
        m_loadedCover = pixmap;
        m_loadedCoverScaled = scaledCoverArt(pixmap);
        update();
    }
}

void WCoverArt::slotTrackCoverArtUpdated() {
    if (m_loadedTrack) {
        m_lastRequestedCover = m_loadedTrack->getCoverInfo();
        m_lastRequestedCover.trackId = m_loadedTrack->getId();
        m_lastRequestedCover.trackLocation = m_loadedTrack->getLocation();
        CoverArtCache* pCache = CoverArtCache::instance();
        if (pCache != NULL) {
            pCache->requestCover(m_lastRequestedCover, this);
        }
    }
}

void WCoverArt::slotLoadTrack(TrackPointer pTrack) {
    if (m_loadedTrack) {
        disconnect(m_loadedTrack.data(), SIGNAL(coverArtUpdated()),
                   this, SLOT(slotTrackCoverArtUpdated()));
    }
    m_lastRequestedCover = CoverInfo();
    m_loadedCover = QPixmap();
    m_loadedCoverScaled = QPixmap();
    m_loadedTrack = pTrack;
    if (m_loadedTrack) {
        connect(m_loadedTrack.data(), SIGNAL(coverArtUpdated()),
                this, SLOT(slotTrackCoverArtUpdated()));
    }

    if (!m_bEnable) {
        return;
    }

    slotTrackCoverArtUpdated();
}

QPixmap WCoverArt::scaledCoverArt(const QPixmap& normal) {
    if (normal.isNull()) {
        return QPixmap();
    }
    return normal.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void WCoverArt::paintEvent(QPaintEvent* pEvent) {
    if (!m_bEnable) {
        QWidget::paintEvent(pEvent);
        return;
    }

    QPainter painter(this);

    QPixmap toDraw = m_loadedCoverScaled;
    if (toDraw.isNull()) {
        toDraw = m_defaultCoverScaled;
    }

    if (!toDraw.isNull()) {
        QSize widgetSize = size();
        QSize pixmapSize = toDraw.size();

        int x = math_max(0, (widgetSize.width() - pixmapSize.width()) / 2);
        int y = math_max(0, (widgetSize.height() - pixmapSize.height()) / 2);
        painter.drawPixmap(x, y, toDraw);
    } else {
        QWidget::paintEvent(pEvent);
    }
}

void WCoverArt::resizeEvent(QResizeEvent*) {
    m_loadedCoverScaled = scaledCoverArt(m_loadedCover);
    m_defaultCoverScaled = scaledCoverArt(m_defaultCover);
}

void WCoverArt::mousePressEvent(QMouseEvent* event) {
    if (!m_bEnable) {
        return;
    }

    if (event->button() == Qt::RightButton && m_loadedTrack) { // show context-menu
        m_pMenu->show(event->globalPos(), m_lastRequestedCover, m_loadedTrack);
    } else if (event->button() == Qt::LeftButton) { // init/close fullsize cover
        if (m_pDlgFullSize->isVisible()) {
            m_pDlgFullSize->close();
        } else {
            m_pDlgFullSize->init(m_lastRequestedCover);
        }
    }
}

void WCoverArt::leaveEvent(QEvent*) {
    m_pDlgFullSize->close();
}

void WCoverArt::mouseMoveEvent(QMouseEvent* event) {
    if ((event->buttons() & Qt::LeftButton) && m_loadedTrack) {
        DragAndDropHelper::dragTrack(m_loadedTrack, this);
    }
}

void WCoverArt::dragEnterEvent(QDragEnterEvent* event) {
    // We don't have a group to load the track into.
    if (m_group.isEmpty()) {
        event->ignore();
        return;
    }

    if (event->mimeData()->hasUrls() &&
            event->mimeData()->urls().size() > 0) {
        // Accept if the Deck isn't playing or the settings allow to interrupt a playing deck
        if ((!ControlObject::get(ConfigKey(m_group, "play")) ||
             m_pConfig->getValueString(ConfigKey("[Controls]", "AllowTrackLoadToPlayingDeck")).toInt())) {
            QList<QFileInfo> files = DragAndDropHelper::supportedTracksFromUrls(
                event->mimeData()->urls(), true, false);
            if (!files.isEmpty()) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void WCoverArt::dropEvent(QDropEvent *event) {
    // We don't have a group to load the track into.
    if (m_group.isEmpty()) {
        event->ignore();
        return;
    }

    if (event->mimeData()->hasUrls()) {
        QList<QFileInfo> files = DragAndDropHelper::supportedTracksFromUrls(
                event->mimeData()->urls(), true, false);
        if (!files.isEmpty()) {
            event->accept();
            emit(trackDropped(files.at(0).canonicalFilePath(), m_group));
            return;
        }
    }
    event->ignore();
}
