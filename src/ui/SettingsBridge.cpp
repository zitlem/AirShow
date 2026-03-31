#include "ui/SettingsBridge.h"
#include "settings/AppSettings.h"

namespace airshow {

SettingsBridge::SettingsBridge(AppSettings& settings, QObject* parent)
    : QObject(parent), m_settings(settings) {}

// ---------------------------------------------------------------------------
// Receiver identity
// ---------------------------------------------------------------------------

QString SettingsBridge::receiverName() const {
    return m_settings.receiverName();
}

// Note: receiverNameChanged() signal is declared but not emitted proactively
// in Phase 3. Phase 7 will wire live updates when the settings panel lands.

// ---------------------------------------------------------------------------
// Security settings (Phase 7 / D-12)
// ---------------------------------------------------------------------------

bool SettingsBridge::requireApproval() const {
    return m_settings.requireApproval();
}

void SettingsBridge::setRequireApproval(bool v) {
    if (m_settings.requireApproval() == v) return;
    m_settings.setRequireApproval(v);
    emit requireApprovalChanged(v);
}

bool SettingsBridge::pinEnabled() const {
    return m_settings.pinEnabled();
}

void SettingsBridge::setPinEnabled(bool v) {
    if (m_settings.pinEnabled() == v) return;
    m_settings.setPinEnabled(v);
    emit pinEnabledChanged(v);
}

QString SettingsBridge::pin() const {
    return m_settings.pin();
}

void SettingsBridge::setPin(const QString& v) {
    if (m_settings.pin() == v) return;
    m_settings.setPin(v);
    emit pinChanged(v);
}

QStringList SettingsBridge::trustedDevices() const {
    return m_settings.trustedDevices();
}

void SettingsBridge::clearTrustedDevices() {
    m_settings.clearTrustedDevices();
    emit trustedDevicesChanged(QStringList());
}

} // namespace airshow
