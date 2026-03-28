#include "protocol/ProtocolManager.h"
#include "protocol/ProtocolHandler.h"
#include <glib.h>   // g_warning

namespace myairshow {

ProtocolManager::ProtocolManager(MediaPipeline* pipeline)
    : m_pipeline(pipeline) {}

ProtocolManager::~ProtocolManager() {
    stopAll();
}

void ProtocolManager::addHandler(std::unique_ptr<ProtocolHandler> handler) {
    handler->setMediaPipeline(m_pipeline);
    m_handlers.push_back(std::move(handler));
}

bool ProtocolManager::startAll() {
    bool ok = true;
    for (auto& h : m_handlers) {
        if (!h->start()) {
            g_warning("ProtocolManager: handler '%s' failed to start", h->name().c_str());
            ok = false;
        }
    }
    return ok;
}

void ProtocolManager::stopAll() {
    for (auto& h : m_handlers) {
        if (h->isRunning()) {
            h->stop();
        }
    }
}

std::size_t ProtocolManager::handlerCount() const {
    return m_handlers.size();
}

} // namespace myairshow
