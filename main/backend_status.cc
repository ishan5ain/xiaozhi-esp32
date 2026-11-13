#include "backend_status.h"

#include <cJSON.h>
#include <cstdio>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>

#include "board.h"
#include "http.h"

namespace opencharm {

static const char* TAG = "BackendStatus";

BackendStatus& BackendStatus::GetInstance() {
    static BackendStatus instance;
    return instance;
}

void BackendStatus::Start() {
    if (started_) {
        return;
    }
    started_ = true;
    BaseType_t created = xTaskCreate([](void*) { GetInstance().PollLoop(); }, "backend_status", 8192,
                                     nullptr, 5, nullptr);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create backend_status task");
        started_ = false;
    }
}

std::string BackendStatus::GetSummary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!online_) {
        return "backend offline";
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "rpi5 %.2f", load_);
    return buf;
}

bool BackendStatus::IsOnline() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return online_;
}

void BackendStatus::PollLoop() {
    const std::string url = CONFIG_OPENCHARM_STATUS_URL;
    constexpr int kIntervalMs = 5000;
    int failures = 0;
    while (true) {
        // GetNetwork() returns a static EspNetwork (never null), but its internals are
        // not ready until Board::StartNetwork() has run — calling CreateHttp() earlier
        // crashed the device (boot loop, 2026-11-12). The guard is defensive; the real
        // fix is the Start() placement after StartNetwork() in Application::Initialize().
        NetworkInterface* network = Board::GetInstance().GetNetwork();
        if (network == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(kIntervalMs));
            continue;
        }

        bool ok = false;
        float load = -1.0f;
        auto http = network->CreateHttp(0);
        if (http != nullptr) {
            http->SetTimeout(2000);
            http->SetHeader("Accept", "application/json");
            auto opened = http->Open("GET", url);
            if (opened) {
                auto status = http->GetStatusCode();
                if (status && *status == 200) {
                    std::string body = http->ReadAll();
                    cJSON* root = cJSON_Parse(body.c_str());
                    if (root != nullptr) {
                        cJSON* load_item = cJSON_GetObjectItem(root, "load");
                        if (cJSON_IsNumber(load_item)) {
                            load = static_cast<float>(load_item->valuedouble);
                            ok = true;
                        }
                        cJSON_Delete(root);
                    }
                }
            }
            http->Close();
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            online_ = ok;
            if (ok) {
                load_ = load;
            }
        }
        if (ok) {
            failures = 0;
            ESP_LOGI(TAG, "backend online, rpi5 load %.2f", load);
        } else {
            ++failures;
            ESP_LOGW(TAG, "backend status fetch failed (%d consecutive)", failures);
        }
        vTaskDelay(pdMS_TO_TICKS(kIntervalMs));
    }
}

}  // namespace opencharm
