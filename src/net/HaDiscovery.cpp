#include "HaDiscovery.h"

#include <ArduinoJson.h>

#include "config.h"

namespace HaDiscovery {

namespace {

void addDevice(JsonDocument& doc) {
    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = DEVICE_ID;
    dev["name"] = DEVICE_NAME;
    dev["manufacturer"] = "anita-adrc";
    dev["model"] = "ESP32-C3 ADRC boiler controller";
}

void common(JsonDocument& doc, const char* name, const char* id) {
    doc["name"] = name;
    doc["unique_id"] = String(DEVICE_ID "_") + id;
    doc["state_topic"] = MQTT_TOPIC_STATE;
    doc["availability_topic"] = MQTT_TOPIC_STATUS;
    addDevice(doc);
}

void publish(PubSubClient& mqtt, const char* component, const char* id,
             JsonDocument& doc) {
    const String topic =
        String(HA_DISCOVERY_PREFIX "/") + component + "/" DEVICE_ID "/" + id + "/config";
    String payload;
    serializeJson(doc, payload);
    mqtt.publish(topic.c_str(), payload.c_str(), true);
}

void tempSensor(PubSubClient& mqtt, const char* name, const char* id,
                const char* tmpl, bool diagnostic = false) {
    JsonDocument doc;
    common(doc, name, id);
    doc["device_class"] = "temperature";
    doc["unit_of_measurement"] = "°C";
    doc["value_template"] = tmpl;
    doc["suggested_display_precision"] = 1;
    if (diagnostic) doc["entity_category"] = "diagnostic";
    publish(mqtt, "sensor", id, doc);
}

}  // namespace

void publishAll(PubSubClient& mqtt) {
    tempSensor(mqtt, "Group temperature", "group_temp", "{{ value_json.group }}");
    tempSensor(mqtt, "Boiler temperature", "boiler_temp", "{{ value_json.boiler }}");
    tempSensor(mqtt, "Boost", "boost", "{{ value_json.boost }}", true);
    tempSensor(mqtt, "Learned offset", "offset", "{{ value_json.offset }}", true);

    {
        JsonDocument doc;
        common(doc, "Heater duty", "duty");
        doc["unit_of_measurement"] = "%";
        doc["value_template"] = "{{ (value_json.duty * 100) | round(1) }}";
        publish(mqtt, "sensor", "duty", doc);
    }
    {
        JsonDocument doc;
        common(doc, "Disturbance z2", "z2");
        doc["unit_of_measurement"] = "K/s";
        doc["value_template"] = "{{ value_json.z2 }}";
        doc["entity_category"] = "diagnostic";
        publish(mqtt, "sensor", "z2", doc);
    }
    {
        JsonDocument doc;
        common(doc, "State", "state");
        doc["value_template"] = "{{ value_json.state }}";
        publish(mqtt, "sensor", "state", doc);
    }
    {
        JsonDocument doc;
        common(doc, "Fault", "fault");
        doc["value_template"] = "{{ value_json.fault }}";
        doc["entity_category"] = "diagnostic";
        publish(mqtt, "sensor", "fault", doc);
    }
    {
        JsonDocument doc;
        common(doc, "Draw detected", "draw");
        doc["value_template"] =
            "{{ 'ON' if value_json.draw else 'OFF' }}";
        publish(mqtt, "binary_sensor", "draw", doc);
    }
    {
        JsonDocument doc;
        common(doc, "Brew setpoint", "setpoint");
        doc["command_topic"] = MQTT_TOPIC_SET_SETPOINT;
        doc["value_template"] = "{{ value_json.set }}";
        doc["min"] = 85.0;
        doc["max"] = 98.0;
        doc["step"] = 0.5;
        doc["unit_of_measurement"] = "°C";
        doc["mode"] = "box";
        publish(mqtt, "number", "setpoint", doc);
    }
    {
        JsonDocument doc;
        common(doc, "k_boost", "kboost");
        doc["command_topic"] = MQTT_TOPIC_SET_KBOOST;
        doc["value_template"] = "{{ value_json.kboost }}";
        doc["min"] = 0.0;
        doc["max"] = 5.0;
        doc["step"] = 0.1;
        doc["entity_category"] = "config";
        publish(mqtt, "number", "kboost", doc);
    }
}

}  // namespace HaDiscovery
