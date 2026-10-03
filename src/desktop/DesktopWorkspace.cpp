#include "desktop/DesktopWorkspace.h"
#include <QWidget>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shobjidl.h>
#endif
namespace campus {
void placeWindowOnDesktop(QWidget &window, const QString &desktopId) {
    if (desktopId.isEmpty())
        return;
    window.setAttribute(Qt::WA_ShowWithoutActivating);
#ifdef Q_OS_WIN
    GUID target{};
    const auto braces = desktopId.startsWith('{') ? desktopId : "{" + desktopId + "}";
    if (FAILED(CLSIDFromString(reinterpret_cast<LPCOLESTR>(braces.utf16()), &target)))
        throw std::runtime_error("虚拟桌面ID无效");
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IVirtualDesktopManager *manager = nullptr;
    HRESULT result = CoCreateInstance(CLSID_VirtualDesktopManager, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&manager));
    if (SUCCEEDED(result)) {
        const auto handle = reinterpret_cast<HWND>(window.winId());
        result = manager->MoveWindowToDesktop(handle, target);
        GUID actual{};
        const auto verified = manager->GetWindowDesktopId(handle, &actual);
        if (SUCCEEDED(verified) && IsEqualGUID(actual, target))
            result = S_OK;
        manager->Release();
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    if (FAILED(result))
        throw std::runtime_error(("无法将测试窗口放到指定虚拟桌面，已停止显示窗口；HRESULT=" +
                                  QString::number(quint32(result), 16))
                                     .toStdString());
#else
    throw std::runtime_error("指定虚拟桌面功能仅支持Windows");
#endif
}
} // namespace campus
