#pragma once
#include <QQuickItem>
#include <QImage>
#include <QMutex>
#include <QtQml/qqmlregistration.h>

namespace airshow {

// Custom QQuickItem that receives raw video frames from GStreamer's appsink
// and renders them via Qt's scene graph (QSGSimpleTextureNode).
//
// Thread safety: pushFrame() is called from GStreamer's streaming thread.
// updatePaintNode() is called from Qt's render thread. A mutex protects the
// pending frame. This avoids all GStreamer GL context sharing complexity.
class VideoFrameSink : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool forceAspectRatio READ forceAspectRatio WRITE setForceAspectRatio
               NOTIFY forceAspectRatioChanged)

public:
    explicit VideoFrameSink(QQuickItem* parent = nullptr);

    bool forceAspectRatio() const { return m_forceAspectRatio; }
    void setForceAspectRatio(bool v);

    // Push a decoded video frame from any thread (GStreamer streaming thread).
    void pushFrame(const QImage& frame);

signals:
    void forceAspectRatioChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
    QMutex m_mutex;
    QImage m_pendingFrame;
    bool   m_dirty            = false;
    bool   m_forceAspectRatio = true;
};

} // namespace airshow
