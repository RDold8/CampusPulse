#include "adapters/AiProviderProbe.h"
#include <QHostInfo>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <memory>

namespace campus {
namespace {
constexpr qsizetype responseLimit = 2 * 1024 * 1024;
struct ResponseBuffer {
    QByteArray bytes;
    bool tooLarge = false;
};
void redact(AiProbeResult &result, const QString &key) {
    result.message.replace(key, "[已隐藏]", Qt::CaseSensitive);
    result.responseModel.replace(key, "[已隐藏]", Qt::CaseSensitive);
    for (auto &model : result.modelIds)
        model.replace(key, "[已隐藏]", Qt::CaseSensitive);
    QJsonObject usage;
    for (auto entry = result.usage.begin(); entry != result.usage.end(); ++entry) {
        auto name = entry.key();
        name.replace(key, "[已隐藏]", Qt::CaseSensitive);
        usage.insert(name, entry.value());
    }
    result.usage = usage;
}
bool validModel(const QString &model) {
    return !model.isEmpty() && model.size() <= 160 &&
           !model.contains(QRegularExpression("[\\x00-\\x20\\x7f]"));
}
}

AiProviderProbe::AiProviderProbe(QObject *parent) : QObject(parent) {
    qRegisterMetaType<AiProbeResult>();
    deadline_ = new QTimer(this);
    deadline_->setSingleShot(true);
    connect(deadline_, &QTimer::timeout, this, [this] {
        if (!busy_)
            return;
        ++generation_;
        if (lookupId_ != -1)
            QHostInfo::abortHostLookup(lookupId_);
        lookupId_ = -1;
        busy_ = false;
        if (reply_)
            reply_->abort();
        AiProbeResult result;
        result.operation = operation_;
        result.elapsedMs = elapsed_.elapsed();
        result.message = "API 请求超过 30 秒上限，没有自动重试";
        emit finished(result);
    });
}
bool AiProviderProbe::busy() const {
    return busy_;
}
void AiProviderProbe::cancel() {
    if (!busy_)
        return;
    ++generation_;
    deadline_->stop();
    if (lookupId_ != -1)
        QHostInfo::abortHostLookup(lookupId_);
    lookupId_ = -1;
    busy_ = false;
    if (reply_)
        reply_->abort();
    AiProbeResult result;
    result.operation = operation_;
    result.elapsedMs = elapsed_.elapsed();
    result.message = "已取消 API 请求；服务器已处理的请求仍可能计费";
    emit finished(result);
}
QUrl AiProviderProbe::endpoint(const AiProviderConfig &provider, AiProbeOperation operation) {
    return operation == AiProbeOperation::Models ? AiProviderConfig::modelsEndpoint(provider)
                                                : AiProviderConfig::requestEndpoint(provider);
}
QJsonObject AiProviderProbe::connectionBody(const AiProviderConfig &provider) {
    return {{"model", provider.model}, {"max_tokens", 128}, {"stream", false},
            {"messages", QJsonArray{QJsonObject{{"role", "user"},
                                               {"content", "Reply with OK only."}}}}};
}
AiProbeResult AiProviderProbe::evaluate(AiApiProtocol protocol, AiProbeOperation operation,
                                      int status, const QByteArray &bytes, qint64 elapsedMs,
                                      const QString &transportError) {
    AiProbeResult result;
    result.operation = operation;
    result.httpStatus = status;
    result.elapsedMs = elapsedMs;
    if (status >= 300 && status < 400) {
        result.message = "API 返回重定向；为防止凭据跨地址发送，未跟随跳转";
        return result;
    }
    if (status == 401 || status == 403) {
        result.message = QString("HTTP %1：鉴权或权限失败，请核对 Key 与供应商地址").arg(status);
        return result;
    }
    if (status == 429) {
        result.message = "HTTP 429：接口限流或配额不足，没有自动重试";
        return result;
    }
    if (operation == AiProbeOperation::Models && (status == 404 || status == 405)) {
        result.message = "供应商不提供此模型列表接口，请手动填写模型名称";
        return result;
    }
    if (status != 200 || !transportError.isEmpty()) {
        result.message = QString("API 请求失败：HTTP %1 · %2").arg(status).arg(
            transportError.isEmpty() ? "接口未返回成功响应" : transportError.left(240));
        return result;
    }
    if (bytes.size() > responseLimit) {
        result.message = "API 响应超过 2 MiB 上限";
        return result;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        document.object().contains("error")) {
        result.message = "API 返回无效 JSON 或错误对象，不能确认接口可用";
        return result;
    }
    const auto object = document.object();
    if (operation == AiProbeOperation::Models) {
        if (!object.value("data").isArray() || object.value("data").toArray().size() > 1000) {
            result.message = "模型列表结构不符合 data 数组，请手动填写模型名称";
            return result;
        }
        QStringList models;
        for (const auto &entry : object.value("data").toArray()) {
            if (!entry.isObject() || !entry.toObject().value("id").isString() ||
                !validModel(entry.toObject().value("id").toString())) {
                result.message = "模型列表含无效模型名称，请手动填写";
                return result;
            }
            models.append(entry.toObject().value("id").toString());
        }
        models.removeDuplicates();
        models.sort();
        if (models.isEmpty()) {
            result.message = "接口返回空模型列表，请手动填写模型名称";
            return result;
        }
        result.success = true;
        result.modelIds = models;
        result.message = QString("已读取 %1 个模型；模型列表不证明网页搜索能力").arg(models.size());
        return result;
    }
    bool hasText = false;
    if (protocol == AiApiProtocol::DeepSeekNative && object.value("content").isArray()) {
        for (const auto &entry : object.value("content").toArray()) {
            if (!entry.isObject())
                continue;
            const auto block = entry.toObject();
            if (block.value("type").toString() == "text" &&
                block.value("text").isString() && !block.value("text").toString().isEmpty())
                hasText = true;
        }
    } else if (protocol == AiApiProtocol::OpenAiCompatible &&
               object.value("choices").isArray()) {
        for (const auto &entry : object.value("choices").toArray()) {
            if (!entry.isObject())
                continue;
            const auto choice = entry.toObject();
            const auto message = choice.value("message").toObject();
            if (message.value("content").isString() &&
                !message.value("content").toString().isEmpty())
                hasText = true;
        }
    }
    if (!hasText) {
        result.message = "接口未返回可识别的模型文本响应，不能确认调用成功";
        return result;
    }
    if (object.contains("model") &&
        (!object.value("model").isString() || !validModel(object.value("model").toString()))) {
        result.message = "接口返回的模型名字段无效，不能采用此响应";
        return result;
    }
    result.success = true;
    result.responseModel = object.value("model").toString();
    // Keep only numeric usage counts. Arbitrary provider fields must not become log content.
    const auto usage = object.value("usage").toObject();
    for (auto entry = usage.begin(); entry != usage.end(); ++entry)
        if (entry.value().isDouble() && entry.value().toDouble() >= 0)
            result.usage.insert(entry.key().left(80), entry.value());
    result.message = result.responseModel.isEmpty()
        ? "已收到模型文本响应；服务端未回显模型名。只测试连接，没有执行网页检索"
        : "已收到模型文本响应；本次只测试连接，没有执行网页检索";
    return result;
}
void AiProviderProbe::probe(const AiProviderConfig &provider, const QString &key) {
    start(provider, key, AiProbeOperation::Connection);
}
void AiProviderProbe::fetchModels(const AiProviderConfig &provider, const QString &key) {
    start(provider, key, AiProbeOperation::Models);
}
void AiProviderProbe::start(const AiProviderConfig &provider, const QString &key,
                            AiProbeOperation operation) {
    if (busy_)
        return;
    const auto error = AiProviderConfig::validationError(provider);
    AiProbeResult rejected;
    rejected.operation = operation;
    if (!error.isEmpty()) {
        rejected.message = error;
        emit finished(rejected);
        return;
    }
    if (key.isEmpty() || key.size() > 4096 ||
        key.contains(QRegularExpression("[^\\x21-\\x7e]"))) {
        rejected.message = "请填写不含空白的 API Key；没有发起网络请求";
        emit finished(rejected);
        return;
    }
    if (operation == AiProbeOperation::Connection && provider.model.isEmpty()) {
        rejected.message = "请先选择或手动填写模型名称";
        emit finished(rejected);
        return;
    }
    const auto target = endpoint(provider, operation);
    if (target.isEmpty()) {
        rejected.message = "此完整请求地址没有已知模型目录路由，请手动填写模型 ID；没有发送请求";
        emit finished(rejected);
        return;
    }
    busy_ = true;
    operation_ = operation;
    elapsed_.start();
    deadline_->start(30000);
    const auto generation = ++generation_;
    lookupId_ = QHostInfo::lookupHost(target.host(), this,
        [this, provider, key, operation, target, generation](const QHostInfo &info) {
            if (generation != generation_ || !busy_)
                return;
            lookupId_ = -1;
            const auto failLookup = [this, operation](const QString &message) {
                deadline_->stop();
                busy_ = false;
                AiProbeResult result;
                result.operation = operation;
                result.elapsedMs = elapsed_.elapsed();
                result.message = message;
                emit finished(result);
            };
            if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
                failLookup("API 域名解析失败，没有发送 Key");
                return;
            }
            for (const auto &address : info.addresses()) {
                if (!AiProviderConfig::isPublicAddress(address)) {
                    failLookup("API 域名包含内网、回环或保留地址，没有发送 Key");
                    return;
                }
            }
            auto pinned = target;
            // Avoid a second DNS lookup. Host and TLS peer remain the original public hostname.
            auto chosen = info.addresses().first();
            for (const auto &address : info.addresses())
                if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                    chosen = address;
                    break;
                }
            pinned.setHost(chosen.toString());
            // Separate pools for each operation: a pinned CDN IP must not become
            // a shared TLS/cookie/auth identity for two different API hostnames.
            auto *transport = new QNetworkAccessManager(this);
            transport->setTransferTimeout(30000);
            transport->setProxy(QNetworkProxy::NoProxy);
            QNetworkRequest request(pinned);
            request.setPeerVerifyName(target.host());
            request.setRawHeader("Host", target.host().toLatin1());
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::ManualRedirectPolicy);
            request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
            // The wire URL contains a pinned IP. Never reuse cookies or HTTP-auth state
            // across original API hostnames which happen to share a CDN address.
            request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
            request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
            request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            const auto headers = AiProviderConfig::credentialHeaders(provider, key);
            for (auto header = headers.begin(); header != headers.end(); ++header)
                request.setRawHeader(header.key(), header.value());
            request.setRawHeader("User-Agent", "CampusPulse/0.1 provider-probe");
            request.setRawHeader("Connection", "close");
            auto *reply = operation == AiProbeOperation::Models
                ? transport->get(request)
                : transport->post(request, QJsonDocument(connectionBody(provider))
                                             .toJson(QJsonDocument::Compact));
            reply_ = reply;
            reply->setReadBufferSize(responseLimit + 1);
            const auto buffer = std::make_shared<ResponseBuffer>();
            const auto drain = [reply, buffer] {
                if (buffer->tooLarge)
                    return;
                buffer->bytes.append(reply->read(responseLimit - buffer->bytes.size() + 1));
                if (buffer->bytes.size() > responseLimit) {
                    buffer->tooLarge = true;
                    buffer->bytes.clear();
                    reply->abort();
                }
            };
            connect(reply, &QIODevice::readyRead, this, drain);
            connect(reply, &QNetworkReply::finished, this,
                [this, provider, key, operation, generation, reply, transport, buffer, drain] {
                    drain();
                    if (generation == generation_ && busy_) {
                        deadline_->stop();
                        busy_ = false;
                        const auto status = reply->attribute(
                            QNetworkRequest::HttpStatusCodeAttribute).toInt();
                        auto result = evaluate(provider.protocol, operation, status,
                                               buffer->bytes, elapsed_.elapsed(),
                                               reply->error() == QNetworkReply::NoError
                                                   ? QString{} : reply->errorString());
                        if (buffer->tooLarge) {
                            result.success = false;
                            result.message = "API 响应超过 2 MiB 上限，没有自动重试";
                        }
                        redact(result, key);
                        emit finished(result);
                    }
                    reply->deleteLater();
                    transport->deleteLater();
                });
        });
}
} // namespace campus
