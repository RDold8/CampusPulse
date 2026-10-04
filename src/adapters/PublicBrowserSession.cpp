#include "adapters/PublicBrowserSession.h"
#include "adapters/PublicUniversityNetwork.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>
#include <QTimeZone>
#include <memory>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#if defined(_MSC_VER) && !defined(__clang__)
// Qt supplies a function-like __has_attribute fallback. This SDK header tests
// only its presence and would otherwise select GNU attributes under MSVC.
#pragma push_macro("__has_attribute")
#undef __has_attribute
#endif
#include <WebView2EnvironmentOptions.h>
#if defined(_MSC_VER) && !defined(__clang__)
#pragma pop_macro("__has_attribute")
#endif
#endif

namespace campus {
namespace {

#ifdef Q_OS_WIN
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

QString takeString(LPWSTR value) {
    const QString result = value ? QString::fromWCharArray(value) : QString();
    CoTaskMemFree(value);
    return result;
}

bool samePublicHost(const QUrl &url, const QString &host) {
    QHostAddress literal;
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           url.host().compare(host, Qt::CaseInsensitive) == 0 &&
           url.port() == -1 && url.userInfo().isEmpty() &&
           !literal.setAddress(url.host());
}

bool verificationDocument(const QString &html) {
    // Do not treat a successful execution of the verification script as a page.
    // A real public HTML response must replace the temporary verification DOM.
    static const QRegularExpression markedMeta(
        QStringLiteral(R"(<meta\b[^>]*\br\s*=\s*["']m["'])"),
        QRegularExpression::CaseInsensitiveOption);
    return PublicUniversityNetwork::isBrowserVerification(412, html.toUtf8()) ||
           html.contains(QStringLiteral("$_ts.nsd")) ||
           (html.contains(QStringLiteral("$_ts")) && markedMeta.match(html).hasMatch());
}

// Chromium releases its profile handles asynchronously after Close(). Retry only
// removal of the exact temporary directory created here, never a user profile.
void removeTemporaryProfile(const QString &path, int attempts = 0) {
    const QFileInfo target(path);
    const auto temporaryRoot = QFileInfo(QDir::tempPath()).canonicalFilePath();
    const auto resolved = target.canonicalFilePath();
    if (path.isEmpty() || !target.fileName().startsWith(QStringLiteral("CampusPulse-public-")) ||
        target.isSymLink() || resolved.isEmpty() ||
        QFileInfo(resolved).absolutePath() != temporaryRoot)
        return;
    if (QDir(path).removeRecursively() || attempts >= 2)
        return;
    if (auto *app = QCoreApplication::instance())
        QTimer::singleShot(1000, app, [path, attempts] { removeTemporaryProfile(path, attempts + 1); });
}

class Verification final : public QObject {
  public:
    Verification(const QUrl &url, const QHostAddress &address, QObject *owner,
                 PublicBrowserSession::Callback callback, int maxBytes, int timeoutMs)
        : QObject(owner), url_(url), address_(address), callback_(std::move(callback)),
          maxBytes_(maxBytes), timeoutMs_(timeoutMs) {
        deadline_.setSingleShot(true);
        poll_.setInterval(300);
        connect(&deadline_, &QTimer::timeout, this, [this] {
            fail(QStringLiteral("官网浏览器验证超时；网站可能需要人工验证。可在浏览器查看官方公开页面后重试。"));
        });
        connect(&poll_, &QTimer::timeout, this, [this] { readDocument(); });
    }

    ~Verification() override {
        close();
        if (comInitialized_)
            CoUninitialize();
    }

    void start() {
        if (!samePublicHost(url_, url_.host()) ||
            !PublicUniversityNetwork::isPublicAddress(address_) ||
            maxBytes_ < 1 || timeoutMs_ < 1) {
            fail(QStringLiteral("官网浏览器验证参数无效。"));
            return;
        }
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(initialized)) {
            fail(QStringLiteral("无法启动官网浏览器验证：当前线程未使用 Windows STA。"));
            return;
        }
        comInitialized_ = true;
        profile_ = std::make_unique<QTemporaryDir>(
            QDir::tempPath() + QStringLiteral("/CampusPulse-public-XXXXXX"));
        profile_->setAutoRemove(false);
        if (!profile_->isValid()) {
            fail(QStringLiteral("无法创建官网浏览器验证的临时目录。"));
            return;
        }
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC",
                                 L"CampusPulse public verification", WS_POPUP,
                                 0, 0, 1024, 768, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window_) {
            fail(QStringLiteral("无法创建官网浏览器验证窗口。"));
            return;
        }

        auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        QString destination = address_.toString();
        if (address_.protocol() == QAbstractSocket::IPv6Protocol)
            destination = QStringLiteral("[") + destination + QStringLiteral("]");
        const QString arguments = QStringLiteral(
            "--host-resolver-rules=\"MAP %1 %2, MAP * ~NOTFOUND\" "
            "--no-proxy-server --disable-quic --disable-background-networking "
            "--disable-component-update --disable-sync --disable-default-apps "
            "--disable-breakpad --no-first-run "
            "--force-webrtc-ip-handling-policy=disable_non_proxied_udp")
            .arg(url_.host(), destination);
        options->put_AdditionalBrowserArguments(arguments.toStdWString().c_str());
        options->put_AllowSingleSignOnUsingOSPrimaryAccount(FALSE);
        options->put_Language(L"zh-CN");
        ComPtr<ICoreWebView2EnvironmentOptions6> extensions;
        if (SUCCEEDED(options.As(&extensions)))
            extensions->put_AreBrowserExtensionsEnabled(FALSE);
        deadline_.start(timeoutMs_);
        const QPointer<Verification> weak(this);
        const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, profile_->path().toStdWString().c_str(), options.Get(),
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [weak](HRESULT status, ICoreWebView2Environment *environment) -> HRESULT {
                    if (weak && !weak->done_) {
                        if (FAILED(status) || !environment)
                            weak->fail(QStringLiteral("官网需要浏览器验证；请安装或更新 Microsoft Edge WebView2 Runtime 后重试。"));
                        else
                            weak->createController(environment);
                    }
                    return S_OK;
                }).Get());
        if (FAILED(result))
            fail(QStringLiteral("官网需要浏览器验证；请安装或更新 Microsoft Edge WebView2 Runtime 后重试。"));
    }

  private:
    void createController(ICoreWebView2Environment *environment) {
        environment_ = environment;
        const QPointer<Verification> weak(this);
        const HRESULT result = environment_->CreateCoreWebView2Controller(
            window_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [weak](HRESULT status, ICoreWebView2Controller *controller) -> HRESULT {
                    if (!weak || weak->done_) {
                        if (controller)
                            controller->Close();
                        return S_OK;
                    }
                    if (FAILED(status) || !controller) {
                        weak->fail(QStringLiteral("无法创建官网浏览器验证；请更新 WebView2 Runtime 后重试。"));
                        return S_OK;
                    }
                    weak->controller_ = controller;
                    RECT bounds{0, 0, 1024, 768};
                    controller->put_Bounds(bounds);
                    // The native host is never shown or focused; visibility here
                    // only lets the isolated browser run its ordinary page scripts.
                    controller->put_IsVisible(TRUE);
                    if (FAILED(controller->get_CoreWebView2(&weak->view_)) || !weak->view_) {
                        weak->fail(QStringLiteral("无法获取官网浏览器验证实例。"));
                        return S_OK;
                    }
                    weak->configure();
                    return S_OK;
                }).Get());
        if (FAILED(result))
            fail(QStringLiteral("无法创建官网浏览器验证实例。"));
    }

    void configure() {
        ComPtr<ICoreWebView2Settings> settings;
        ComPtr<ICoreWebView2Settings4> settings4;
        ComPtr<ICoreWebView2Settings2> settings2;
        ComPtr<ICoreWebView2_22> filters;
        ComPtr<ICoreWebView2_10> auth;
        ComPtr<ICoreWebView2_14> certificates;
        ComPtr<ICoreWebView2_4> downloads;
        if (FAILED(view_->get_Settings(&settings)) ||
            FAILED(settings.As(&settings4)) || FAILED(settings.As(&settings2)) ||
            FAILED(view_.As(&filters)) || FAILED(view_.As(&view2_)) ||
            FAILED(view_.As(&auth)) || FAILED(view_.As(&certificates)) ||
            FAILED(view_.As(&downloads))) {
            fail(QStringLiteral("WebView2 Runtime 版本过旧，无法安全验证官网；请更新后重试。"));
            return;
        }
        settings->put_IsScriptEnabled(TRUE);
        settings->put_IsWebMessageEnabled(FALSE);
        settings->put_AreHostObjectsAllowed(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsBuiltInErrorPageEnabled(FALSE);
        settings4->put_IsPasswordAutosaveEnabled(FALSE);
        settings4->put_IsGeneralAutofillEnabled(FALSE);
        LPWSTR agent = nullptr;
        settings2->get_UserAgent(&agent);
        userAgent_ = takeString(agent).toUtf8();
        if (userAgent_.isEmpty()) {
            fail(QStringLiteral("无法读取官网浏览器验证的请求信息。"));
            return;
        }
        EventRegistrationToken token{};
        const QPointer<Verification> weak(this);
        // This newer filter covers document, frame, service-worker and shared-worker
        // requests. Older frame-incomplete filters are intentionally unsupported.
        const auto requestEnvironment = environment_;
        if (FAILED(filters->AddWebResourceRequestedFilterWithRequestSourceKinds(
                L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL,
                COREWEBVIEW2_WEB_RESOURCE_REQUEST_SOURCE_KINDS_ALL)) ||
            FAILED(view_->add_WebResourceRequested(
                Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                    [weak, requestEnvironment](ICoreWebView2 *, ICoreWebView2WebResourceRequestedEventArgs *args) -> HRESULT {
                        if (weak)
                            weak->resourceRequested(args);
                        else {
                            ComPtr<ICoreWebView2WebResourceResponse> denied;
                            if (SUCCEEDED(requestEnvironment->CreateWebResourceResponse(
                                    nullptr, 403, L"Public session closed", L"Content-Type: text/plain", &denied)))
                                args->put_Response(denied.Get());
                        }
                        return S_OK;
                    }).Get(), &resourceToken_))) {
            fail(QStringLiteral("无法启用官网浏览器验证的请求保护。"));
            return;
        }
        const auto navigation = Callback<ICoreWebView2NavigationStartingEventHandler>(
            [weak](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                const QUrl target(takeString(uri), QUrl::StrictMode);
                if (!weak || weak->done_ || !samePublicHost(target, weak->url_.host())) {
                    args->put_Cancel(TRUE);
                } else if (++weak->navigations_ > 8) {
                    args->put_Cancel(TRUE);
                    weak->fail(QStringLiteral("官网浏览器验证跳转次数过多。"));
                } else {
                    weak->status_ = 0;
                }
                return S_OK;
            });
        view_->add_NavigationStarting(navigation.Get(), &token);
        view_->add_FrameNavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [weak](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                    LPWSTR uri = nullptr;
                    args->get_Uri(&uri);
                    const QUrl target(takeString(uri), QUrl::StrictMode);
                    if (!weak || weak->done_ || !samePublicHost(target, weak->url_.host()))
                        args->put_Cancel(TRUE);
                    return S_OK;
                }).Get(), &token);
        view_->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [weak](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                    if (!weak || weak->done_)
                        return S_OK;
                    ComPtr<ICoreWebView2NavigationCompletedEventArgs2> detailed;
                    if (FAILED(args->QueryInterface(IID_PPV_ARGS(&detailed)))) {
                        weak->fail(QStringLiteral("WebView2 无法核实官网的 HTTP 状态。"));
                        return S_OK;
                    }
                    detailed->get_HttpStatusCode(&weak->status_);
                    BOOL succeeded = FALSE;
                    COREWEBVIEW2_WEB_ERROR_STATUS networkError = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                    args->get_IsSuccess(&succeeded);
                    args->get_WebErrorStatus(&networkError);
                    if (weak->status_ == 0 && !succeeded &&
                        networkError != COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED) {
                        weak->fail(QStringLiteral("官网浏览器 HTTPS 请求失败（网络错误 %1）。")
                            .arg(static_cast<int>(networkError)));
                        return S_OK;
                    }
                    // A 412 challenge is expected here; its ordinary script may
                    // navigate again. Only a subsequent HTTP 200 can be accepted.
                    if (weak->status_ == 401)
                        weak->fail(QStringLiteral("官网拒绝公开访问或需要登录；浏览器验证不会提交学校账号。"));
                    else if (weak->status_ == 200 || weak->status_ == 403 ||
                             weak->status_ == 412 || weak->status_ == 503)
                        weak->readDocument();
                    return S_OK;
                }).Get(), &token);
        view_->add_PermissionRequested(
            Callback<ICoreWebView2PermissionRequestedEventHandler>(
                [](ICoreWebView2 *, ICoreWebView2PermissionRequestedEventArgs *args) -> HRESULT {
                    args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                    return S_OK;
                }).Get(), &token);
        view_->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
                    args->put_Handled(TRUE);
                    return S_OK;
                }).Get(), &token);
        auth->add_BasicAuthenticationRequested(
            Callback<ICoreWebView2BasicAuthenticationRequestedEventHandler>(
                [](ICoreWebView2 *, ICoreWebView2BasicAuthenticationRequestedEventArgs *args) -> HRESULT {
                    args->put_Cancel(TRUE);
                    return S_OK;
                }).Get(), &token);
        certificates->add_ServerCertificateErrorDetected(
            Callback<ICoreWebView2ServerCertificateErrorDetectedEventHandler>(
                [weak](ICoreWebView2 *, ICoreWebView2ServerCertificateErrorDetectedEventArgs *args) -> HRESULT {
                    args->put_Action(COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_CANCEL);
                    if (weak && !weak->done_)
                        weak->fail(QStringLiteral("官网 TLS 证书校验失败，已停止浏览器验证。"));
                    return S_OK;
                }).Get(), &token);
        downloads->add_DownloadStarting(
            Callback<ICoreWebView2DownloadStartingEventHandler>(
                [](ICoreWebView2 *, ICoreWebView2DownloadStartingEventArgs *args) -> HRESULT {
                    args->put_Cancel(TRUE);
                    args->put_Handled(TRUE);
                    return S_OK;
                }).Get(), &token);
        view_->add_ProcessFailed(
            Callback<ICoreWebView2ProcessFailedEventHandler>(
                [weak](ICoreWebView2 *, ICoreWebView2ProcessFailedEventArgs *) -> HRESULT {
                    if (weak && !weak->done_)
                        weak->fail(QStringLiteral("官网浏览器验证进程退出，请重试。"));
                    return S_OK;
                }).Get(), &token);
        // Explicitly prohibit non-HTTP page network channels, including WebSocket.
        const HRESULT protectedChannels = view_->CallDevToolsProtocolMethod(
            L"Network.setBlockedURLs", L"{\"urls\":[\"ws://*\",\"wss://*\",\"http://*\"]}",
            Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                [weak](HRESULT status, LPCWSTR) -> HRESULT {
                    if (weak && !weak->done_) {
                        if (FAILED(status))
                            weak->fail(QStringLiteral("无法启用官网浏览器验证的连接保护。"));
                        else {
                            weak->poll_.start();
                            if (FAILED(weak->view_->Navigate(weak->url_.toString(QUrl::FullyEncoded).toStdWString().c_str())))
                                weak->fail(QStringLiteral("无法打开官网浏览器验证页面。"));
                        }
                    }
                    return S_OK;
                }).Get());
        if (FAILED(protectedChannels))
            fail(QStringLiteral("无法启用官网浏览器验证的连接保护。"));
    }

    void resourceRequested(ICoreWebView2WebResourceRequestedEventArgs *args) {
        ComPtr<ICoreWebView2WebResourceRequest> request;
        LPWSTR uri = nullptr, method = nullptr;
        if (SUCCEEDED(args->get_Request(&request)) && request) {
            request->get_Uri(&uri);
            request->get_Method(&method);
        }
        const QUrl target(takeString(uri), QUrl::StrictMode);
        const QString verb = takeString(method);
        COREWEBVIEW2_WEB_RESOURCE_CONTEXT context = COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL;
        args->get_ResourceContext(&context);
        // Verification needs document/JS/styles only. Reject forms, account login,
        // attachments and large media; every permitted request stays on pinned host.
        const bool eligible = !done_ && samePublicHost(target, url_.host()) && verb == QStringLiteral("GET") &&
            (context == COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT ||
             context == COREWEBVIEW2_WEB_RESOURCE_CONTEXT_SCRIPT ||
             context == COREWEBVIEW2_WEB_RESOURCE_CONTEXT_STYLESHEET ||
             context == COREWEBVIEW2_WEB_RESOURCE_CONTEXT_XML_HTTP_REQUEST ||
             context == COREWEBVIEW2_WEB_RESOURCE_CONTEXT_FETCH);
        // Rejected media do not consume a network-request slot: a normal campus
        // homepage can contain hundreds of image elements without fetching them.
        const bool allowed = eligible && ++requests_ <= 64;
        if (allowed)
            return;
        ComPtr<ICoreWebView2WebResourceResponse> denied;
        if (SUCCEEDED(environment_->CreateWebResourceResponse(
                nullptr, 403, L"Blocked by public-site policy", L"Content-Type: text/plain", &denied)))
            args->put_Response(denied.Get());
        else
            fail(QStringLiteral("无法拦截官网验证中的越界请求。"));
        if (requests_ > 64)
            fail(QStringLiteral("官网浏览器验证请求次数过多。"));
    }

    void readDocument() {
        if (done_ || reading_ || !view_ ||
            (status_ != 200 && status_ != 403 && status_ != 412 && status_ != 503))
            return;
        reading_ = true;
        const int navigation = navigations_;
        const QString script = QStringLiteral(
            "(()=>{if(document.readyState!=='complete')return null;"
            "const d=document.documentElement?document.documentElement.cloneNode(true):null;"
            "if(!d)return null;"
            "d.querySelectorAll('meta').forEach(m=>{if(m.hasAttribute('charset')||"
            "(m.getAttribute('http-equiv')||'').toLowerCase()==='content-type')m.remove();});"
            "const head=d.querySelector('head');if(head){const m=document.createElement('meta');"
            "m.setAttribute('charset','UTF-8');head.prepend(m);}const h=d.outerHTML;"
            "return {url:location.href,html:h.length>%1?'':h,tooLarge:h.length>%1};})()")
            .arg(maxBytes_);
        const QPointer<Verification> weak(this);
        const HRESULT result = view_->ExecuteScript(script.toStdWString().c_str(),
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [weak, navigation](HRESULT status, LPCWSTR result) -> HRESULT {
                    if (!weak || weak->done_)
                        return S_OK;
                    weak->reading_ = false;
                    // A verification script can start the next navigation while
                    // this DOM read is in flight. Its response belongs to the old
                    // document and must not be mistaken for a failed new page.
                    if (FAILED(status) || !result || weak->navigations_ != navigation ||
                        weak->status_ == 0)
                        return S_OK;
                    const QJsonObject document = QJsonDocument::fromJson(
                        QString::fromWCharArray(result).toUtf8()).object();
                    if (document.isEmpty())
                        return S_OK;
                    const QUrl location(document.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
                    if (!samePublicHost(location, weak->url_.host())) {
                        weak->fail(QStringLiteral("官网浏览器验证跳转到了未经允许的地址。"));
                        return S_OK;
                    }
                    if (document.value(QStringLiteral("tooLarge")).toBool()) {
                        weak->fail(QStringLiteral("官网浏览器验证页面超过大小限制。"));
                        return S_OK;
                    }
                    const QString html = document.value(QStringLiteral("html")).toString();
                    const QByteArray bytes = html.toUtf8();
                    if (bytes.size() > weak->maxBytes_) {
                        weak->fail(QStringLiteral("官网浏览器验证页面超过大小限制。"));
                        return S_OK;
                    }
                    if (html.size() < 100 || verificationDocument(html))
                        return S_OK;
                    if (weak->status_ != 200) {
                        weak->fail(QStringLiteral("官网浏览器验证后仍返回 HTTP %1。")
                                   .arg(weak->status_));
                        return S_OK;
                    }
                    // Login forms must stay in the official browser, never become
                    // an automatically accepted public university identity page.
                    static const QRegularExpression passwordInput(
                        QStringLiteral(R"(<input\b[^>]*\btype\s*=\s*["']?password\b)"),
                        QRegularExpression::CaseInsensitiveOption);
                    if (passwordInput.match(html).hasMatch()) {
                        weak->fail(QStringLiteral("官网页面需要账号登录；请使用学校官方登录入口。"));
                        return S_OK;
                    }
                    weak->html_ = bytes;
                    weak->finalUrl_ = location;
                    weak->poll_.stop();
                    weak->collectCookies(location);
                    return S_OK;
                }).Get());
        if (FAILED(result))
            reading_ = false;
    }

    void collectCookies(const QUrl &location) {
        ComPtr<ICoreWebView2CookieManager> cookies;
        if (FAILED(view2_->get_CookieManager(&cookies)) || !cookies) {
            fail(QStringLiteral("无法读取官网浏览器验证会话。"));
            return;
        }
        const QPointer<Verification> weak(this);
        const HRESULT result = cookies->GetCookies(location.toString(QUrl::FullyEncoded).toStdWString().c_str(),
            Callback<ICoreWebView2GetCookiesCompletedHandler>(
                [weak](HRESULT status, ICoreWebView2CookieList *list) -> HRESULT {
                    if (!weak || weak->done_)
                        return S_OK;
                    if (FAILED(status) || !list) {
                        weak->fail(QStringLiteral("无法读取官网浏览器验证会话。"));
                        return S_OK;
                    }
                    PublicBrowserSession::Result answer;
                    answer.html = weak->html_;
                    answer.userAgent = weak->userAgent_;
                    answer.finalUrl = weak->finalUrl_;
                    answer.status = 200;
                    UINT32 count = 0;
                    list->get_Count(&count);
                    int cookieBytes = 0;
                    for (UINT32 index = 0; index < count && index < 32; ++index) {
                        ComPtr<ICoreWebView2Cookie> cookie;
                        if (FAILED(list->GetValueAtIndex(index, &cookie)) || !cookie)
                            continue;
                        LPWSTR name = nullptr, value = nullptr, domain = nullptr, path = nullptr;
                        cookie->get_Name(&name); cookie->get_Value(&value);
                        cookie->get_Domain(&domain); cookie->get_Path(&path);
                        const QByteArray cookieName = takeString(name).toUtf8();
                        const QByteArray cookieValue = takeString(value).toUtf8();
                        QString cookieDomain = takeString(domain).toLower();
                        const QString cookiePath = takeString(path);
                        if (cookieDomain.startsWith(QLatin1Char('.')))
                            cookieDomain.remove(0, 1);
                        const QString host = weak->url_.host().toLower();
                        if (cookieDomain.isEmpty() || !(host == cookieDomain ||
                                host.endsWith(QLatin1Char('.') + cookieDomain)) ||
                            cookieName.isEmpty() || cookieName.contains('\r') || cookieName.contains('\n') ||
                            cookieValue.contains('\r') || cookieValue.contains('\n'))
                            continue;
                        BOOL session = FALSE, secure = FALSE, httpOnly = FALSE;
                        double expires = 0;
                        cookie->get_IsSession(&session); cookie->get_IsSecure(&secure);
                        cookie->get_IsHttpOnly(&httpOnly); cookie->get_Expires(&expires);
                        if (!session && expires <= QDateTime::currentSecsSinceEpoch())
                            continue;
                        cookieBytes += cookieName.size() + cookieValue.size();
                        if (cookieBytes > 8192)
                            break;
                        QNetworkCookie saved(cookieName, cookieValue);
                        // Narrow even a school-domain cookie to this exact verified
                        // host. The transport also stores this result by exact host.
                        saved.setDomain(host);
                        saved.setPath(cookiePath.isEmpty() ? QStringLiteral("/") : cookiePath);
                        saved.setSecure(secure);
                        saved.setHttpOnly(httpOnly);
                        if (!session)
                            saved.setExpirationDate(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(expires), QTimeZone::UTC));
                        answer.cookies.append(saved);
                    }
                    weak->finish(std::move(answer));
                    return S_OK;
                }).Get());
        if (FAILED(result))
            fail(QStringLiteral("无法读取官网浏览器验证会话。"));
    }

    void fail(const QString &message) {
        PublicBrowserSession::Result result;
        result.error = message;
        result.status = status_;
        finish(std::move(result));
    }

    void finish(PublicBrowserSession::Result result) {
        if (done_)
            return;
        done_ = true;
        deadline_.stop();
        poll_.stop();
        auto callback = std::move(callback_);
        // Closing/releasing the controller inside a WebView2 COM event can
        // invalidate the runtime's current callback stack. Complete only after
        // returning to Qt's event loop, including the owner's callback.
        QTimer::singleShot(0, this, [this, callback = std::move(callback),
                                    result = std::move(result)]() mutable {
            close();
            deleteLater();
            if (callback)
                callback(std::move(result));
        });
    }

    void close() {
        if (view_)
            view_->remove_WebResourceRequested(resourceToken_);
        if (controller_)
            controller_->Close();
        view2_.Reset();
        view_.Reset();
        controller_.Reset();
        environment_.Reset();
        if (window_) {
            DestroyWindow(window_);
            window_ = nullptr;
        }
        if (profile_) {
            const QString path = profile_->path();
            profile_.reset();
            if (auto *app = QCoreApplication::instance())
                QTimer::singleShot(1000, app, [path] { removeTemporaryProfile(path); });
            else
                removeTemporaryProfile(path);
        }
    }

    QUrl url_;
    QHostAddress address_;
    PublicBrowserSession::Callback callback_;
    int maxBytes_;
    int timeoutMs_;
    QTimer deadline_;
    QTimer poll_;
    std::unique_ptr<QTemporaryDir> profile_;
    HWND window_ = nullptr;
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> view_;
    ComPtr<ICoreWebView2_2> view2_;
    EventRegistrationToken resourceToken_{};
    QByteArray userAgent_;
    QByteArray html_;
    QUrl finalUrl_;
    int status_ = 0;
    int requests_ = 0;
    int navigations_ = 0;
    bool comInitialized_ = false;
    bool reading_ = false;
    bool done_ = false;
};
#endif
} // namespace

QObject *PublicBrowserSession::verify(const QUrl &url, const QHostAddress &pinnedAddress,
                                     QObject *owner, Callback callback, int maxBytes,
                                     int timeoutMs) {
#ifdef Q_OS_WIN
    auto *operation = new Verification(url, pinnedAddress, owner, std::move(callback), maxBytes, timeoutMs);
    QTimer::singleShot(0, operation, [operation] { operation->start(); });
    return operation;
#else
    auto *operation = new QObject(owner);
    QTimer::singleShot(0, operation, [operation, callback = std::move(callback)]() mutable {
        Result result;
        result.error = QStringLiteral("该官网需要浏览器验证；当前平台暂不支持自动验证，请在官方浏览器查看。 ");
        operation->deleteLater();
        if (callback)
            callback(std::move(result));
    });
    Q_UNUSED(url)
    Q_UNUSED(pinnedAddress)
    Q_UNUSED(maxBytes)
    Q_UNUSED(timeoutMs)
    return operation;
#endif
}

} // namespace campus
