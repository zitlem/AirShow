#pragma once
#include <memory>
#include <vector>

namespace airshow {

class ProtocolHandler;
class MediaPipeline;

// Owns all registered ProtocolHandler instances.
// Starts/stops them as a group and routes the shared MediaPipeline to each.
class ProtocolManager {
public:
    explicit ProtocolManager(MediaPipeline* pipeline);
    ~ProtocolManager();

    // Register a handler. ProtocolManager takes ownership.
    void addHandler(std::unique_ptr<ProtocolHandler> handler);

    // Start all registered handlers. Returns false if any handler fails to start.
    bool startAll();

    // Stop all registered handlers.
    void stopAll();

    // Number of registered handlers.
    std::size_t handlerCount() const;

private:
    MediaPipeline*                               m_pipeline;
    std::vector<std::unique_ptr<ProtocolHandler>> m_handlers;
};

} // namespace airshow
