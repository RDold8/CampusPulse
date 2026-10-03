#include "application/SourceService.h"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace campus {

SourceService::SourceService(std::string schoolId, std::vector<SourceDescription> sources,
                             SourceRepository &repository)
    : schoolId_(std::move(schoolId)), sources_(std::move(sources)), repository_(repository) {
    if (schoolId_.empty())
        throw std::invalid_argument("来源目录缺少学校标识");
    std::set<std::string> ids;
    for (const auto &source : sources_) {
        if (source.schoolId != schoolId_ || source.id.empty() || !ids.insert(source.id).second)
            throw std::invalid_argument("来源目录的学校或来源标识无效");
    }
}

std::vector<SourceView> SourceService::list() const {
    std::vector<SourceView> result;
    result.reserve(sources_.size());
    for (const auto &source : sources_)
        result.push_back({source, repository_.state(schoolId_, source.id)});
    return result;
}

std::optional<SourceView> SourceService::find(const std::string &sourceId) const {
    const auto found = std::find_if(sources_.begin(), sources_.end(),
                                    [&](const auto &source) { return source.id == sourceId; });
    if (found == sources_.end())
        return std::nullopt;
    return SourceView{*found, repository_.state(schoolId_, sourceId)};
}

bool SourceService::isRunnable(const std::string &sourceId) const {
    const auto source = find(sourceId);
    return source && source->description.configuredEnabled && source->description.ready &&
           !source->description.requiresLogin && !source->state.paused &&
           source->state.status != "updating";
}

void SourceService::setPaused(const std::string &sourceId, bool paused) {
    const auto source = find(sourceId);
    if (!source)
        throw std::invalid_argument("来源不属于当前学校目录");
    if (!paused && source->description.requiresLogin)
        throw std::runtime_error("此来源需要登录，仅支持在官方浏览器入口查看，不能恢复采集");
    if (source->state.status == "updating")
        throw std::runtime_error("来源正在更新，请等待更新结束后修改暂停状态");
    repository_.setPaused(schoolId_, sourceId, paused);
}

std::int64_t SourceService::begin(const std::string &sourceId, const std::string &timestamp) {
    if (!isRunnable(sourceId))
        throw std::runtime_error("来源尚未就绪、已暂停或正在更新");
    const auto runId = repository_.beginRun(schoolId_, sourceId, timestamp);
    activeRuns_.emplace(runId, sourceId);
    return runId;
}

void SourceService::page(std::int64_t runId, int rows, const std::string &latestDate) {
    if (!activeRuns_.contains(runId))
        throw std::invalid_argument("采集任务不属于当前来源服务");
    repository_.recordPage(runId, rows, latestDate);
}

void SourceService::finish(std::int64_t runId, const std::string &error,
                           const std::string &timestamp) {
    if (!activeRuns_.contains(runId))
        throw std::invalid_argument("采集任务不属于当前来源服务");
    repository_.finishRun(runId, error, timestamp);
    activeRuns_.erase(runId);
}

void SourceService::recover(const std::string &timestamp) {
    if (!activeRuns_.empty())
        throw std::runtime_error("当前会话仍有采集任务，不能执行启动恢复");
    repository_.recoverInterrupted(schoolId_, timestamp);
}

} // namespace campus
