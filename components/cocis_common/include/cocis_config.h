#ifndef COCIS_CONFIG_H
#define COCIS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_SECRET_KEY                "cocis_secure_key_2026"
#define CFG_MQTT_BROKER_HOST          "broker.hivemq.com"
#define CFG_MQTT_BROKER_PORT          1883
#define CFG_MQTT_TOPIC_PREFIX         "cocis/smart_ctrl_mak"

#define CFG_WIFI_SSID                 "Wokwi-GUEST"
#define CFG_WIFI_PASS                 ""

#define CFG_HEARTBEAT_INTERVAL_SEC    5
#define CFG_NODE_OFFLINE_TIMEOUT_SEC  30
#define CFG_MAX_STORE_FORWARD_EVENTS  32
#ifndef CFG_LOCK_TIMEOUT_MS
#define CFG_LOCK_TIMEOUT_MS           1000
#endif

#define CFG_TIMEZONE                  "EAT-3"
#define CFG_SNTP_SERVER               "pool.ntp.org"

#ifdef __cplusplus
}
#endif

#endif /* COCIS_CONFIG_H */
