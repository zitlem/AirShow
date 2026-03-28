#pragma once
#include <string>

namespace myairshow {

class MediaPipeline;  // forward declare — do not include MediaPipeline.h here

class ProtocolHandler {
public:
    virtual ~ProtocolHandler() = default;

    // Begin listening for sender connections. Returns false on fatal error.
    virtual bool start() = 0;

    // Tear down all active sessions and stop listening.
    virtual void stop() = 0;

    // Short lowercase protocol identifier: "airplay", "cast", "dlna"
    virtual std::string name() const = 0;

    // True after start() returns true and before stop() is called.
    virtual bool isRunning() const = 0;

    // Called by ProtocolManager when the shared pipeline is available.
    // Handler stores the pointer; does NOT own it (no delete).
    virtual void setMediaPipeline(MediaPipeline* pipeline) = 0;
};

} // namespace myairshow
