#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace campus {

// Community configuration and local user preferences are separate concerns.
// A catalog entry may describe a source whose crawler is not ready yet.
struct SourceDescription {
    std::string schoolId;
    std::string id;
    std::string name;
    std::string entryUrl;
    std::string discoveryUrl;
    std::vector<std::string> categories;
    int maxPages = 1;
    bool configuredEnabled = false;
    bool ready = false;
    std::string pendingReason;
    bool requiresLogin = false;
    std::string loginUrl;
};

struct SourceState {
    std::string schoolId;
    std::string sourceId;
    std::string status = "never_checked";
    bool paused = false;
    std::string lastAttemptAt;
    std::string lastSuccessAt;
    std::string latestPublishedDate;
    std::string error;
    int successfulPages = 0;
    int rowCount = 0;
};

struct SourceView {
    SourceDescription description;
    SourceState state;

    std::string effectiveStatus() const {
        if (description.requiresLogin)
            return "login_required";
        if (!description.configuredEnabled || !description.ready)
            return "not_ready";
        if (state.paused)
            return "paused";
        return state.status;
    }
};

class SourceRepository {
  public:
    virtual ~SourceRepository() = default;
    virtual SourceState state(const std::string &schoolId, const std::string &sourceId) const = 0;
    virtual void setPaused(const std::string &schoolId, const std::string &sourceId,
                           bool paused) = 0;
    virtual std::int64_t beginRun(const std::string &schoolId, const std::string &sourceId,
                                  const std::string &timestamp) = 0;
    virtual void recordPage(std::int64_t runId, int rows, const std::string &latestDate) = 0;
    virtual void finishRun(std::int64_t runId, const std::string &error,
                           const std::string &timestamp) = 0;
    virtual void recoverInterrupted(const std::string &schoolId, const std::string &timestamp) = 0;
};

} // namespace campus
