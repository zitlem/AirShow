#include "ui/SettingsBridge.h"
#include "settings/AppSettings.h"

namespace myairshow {

SettingsBridge::SettingsBridge(AppSettings& settings, QObject* parent)
    : QObject(parent), m_settings(settings) {}

QString SettingsBridge::receiverName() const {
    return m_settings.receiverName();
}

// Note: receiverNameChanged() signal is declared but not emitted in Phase 3.
// Phase 7 will wire live updates when the settings panel lands (D-10).

} // namespace myairshow
