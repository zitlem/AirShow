#include "platform/WindowsFirewall.h"

#ifdef _WIN32
// Windows-only: INetFwPolicy2 COM API (D-12)
// Headers: netfw.h (Windows SDK), ole32.lib, oleaut32.lib, uuid.lib
#include <windows.h>
#include <netfw.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

#include <glib.h>

namespace {

HRESULT addRule(INetFwRules* pRules, const wchar_t* name,
                NET_FW_IP_PROTOCOL proto, const wchar_t* ports) {
    INetFwRule* pRule = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(NetFwRule), nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  __uuidof(INetFwRule),
                                  reinterpret_cast<void**>(&pRule));
    if (FAILED(hr)) return hr;

    pRule->put_Name(SysAllocString(name));
    pRule->put_Description(SysAllocString(L"AirShow discovery port"));
    pRule->put_Protocol(proto);
    pRule->put_LocalPorts(SysAllocString(ports));
    pRule->put_Direction(NET_FW_RULE_DIR_IN);
    pRule->put_Action(NET_FW_ACTION_ALLOW);
    pRule->put_Enabled(VARIANT_TRUE);
    pRule->put_Profiles(NET_FW_PROFILE2_DOMAIN | NET_FW_PROFILE2_PRIVATE);

    hr = pRules->Add(pRule);
    pRule->Release();
    return hr;
}

} // anonymous namespace

#endif // _WIN32

namespace airshow {

bool WindowsFirewall::registerRules() {
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INetFwPolicy2* pPolicy = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(NetFwPolicy2), nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  __uuidof(INetFwPolicy2),
                                  reinterpret_cast<void**>(&pPolicy));
    if (FAILED(hr)) {
        // D-13: access denied or COM unavailable — show actionable prompt
        g_warning("WindowsFirewall: CoCreateInstance failed (hr=0x%lx). "
                  "Firewall rules cannot be registered automatically.", hr);
        // QMessageBox shown from main.cpp based on return value false
        CoUninitialize();
        return false;
    }

    INetFwRules* pRules = nullptr;
    pPolicy->get_Rules(&pRules);

    addRule(pRules, L"AirShow mDNS",     NET_FW_IP_PROTOCOL_UDP, L"5353");
    addRule(pRules, L"AirShow SSDP",     NET_FW_IP_PROTOCOL_UDP, L"1900");
    addRule(pRules, L"AirShow AirPlay",  NET_FW_IP_PROTOCOL_TCP, L"7000");
    addRule(pRules, L"AirShow Cast",     NET_FW_IP_PROTOCOL_TCP, L"8009");
    addRule(pRules, L"AirShow Protocol", NET_FW_IP_PROTOCOL_TCP, L"7400");
    addRule(pRules, L"AirShow Web",      NET_FW_IP_PROTOCOL_TCP, L"7401");

    if (pRules)  pRules->Release();
    if (pPolicy) pPolicy->Release();
    CoUninitialize();
    return true;
#else
    // D-14: Linux and macOS rely on system defaults — no firewall changes needed
    return true;
#endif
}

bool WindowsFirewall::rulesAlreadyRegistered() {
#ifdef _WIN32
    // Check QSettings first-launch flag set by AppSettings::setFirstLaunchComplete()
    // This is a fast path to skip COM/UAC on subsequent launches.
    // We rely on AppSettings::isFirstLaunch() == false meaning rules are done.
    return false;  // Caller checks AppSettings::isFirstLaunch() — see main.cpp
#else
    return true;
#endif
}

} // namespace airshow
