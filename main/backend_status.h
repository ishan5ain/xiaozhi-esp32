#ifndef _BACKEND_STATUS_H_
#define _BACKEND_STATUS_H_

#include <mutex>
#include <string>

namespace opencharm {

// Polls the open-charm backend status endpoint (CONFIG_OPENCHARM_STATUS_URL,
// the open-charm FastAPI /v1/status on rpi5) every few seconds and exposes a
// short summary for the UI status bar: "rpi5 0.42" when online, "offline" when
// the backend is unreachable. S1 vertical: live homelab load on the charm screen.
class BackendStatus {
public:
    static BackendStatus& GetInstance();

    // Spawns the polling task (idempotent). Safe to call after network init.
    void Start();

    // Short one-line summary for the display status bar.
    std::string GetSummary() const;
    bool IsOnline() const;

private:
    BackendStatus() = default;
    void PollLoop();

    mutable std::mutex mutex_;
    float load_ = -1.0f;
    bool online_ = false;
    bool started_ = false;
};

}  // namespace opencharm

#endif  // _BACKEND_STATUS_H_
