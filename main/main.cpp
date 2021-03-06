// Copyright (C) 2017 Rob Caelers <rob.caelers@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#define _GLIBCXX_USE_C99

#include <iostream>
#include <string>

#include "os/Wifi.hpp"
#include "os/BLEScanner.hpp"
#include "os/Slot.hpp"
#include "os/Task.hpp"
#include "os/MainLoop.hpp"
#include "os/MqttClient.hpp"
#include "os/hexdump.hpp"


extern "C"
{
#include "esp_heap_trace.h"
}
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "nvs_flash.h"

#include "user_config.h"

static const char tag[] = "BEACON-SCANNER";

extern const uint8_t ca_start[] asm("_binary_CA_crt_start");
extern const uint8_t ca_end[] asm("_binary_CA_crt_end");
extern const uint8_t certificate_start[] asm("_binary_esp32_crt_start");
extern const uint8_t certificate_end[] asm("_binary_esp32_crt_end");
extern const uint8_t private_key_start[] asm("_binary_esp32_key_start");
extern const uint8_t private_key_end[] asm("_binary_esp32_key_end");

// #define NUM_RECORDS 100
// static heap_trace_record_t trace_record[NUM_RECORDS]; // This buffer must be in internal RAM

#define ENDIAN_CHANGE_U16(x) ((((x)&0xFF00)>>8) + (((x)&0xFF)<<8))

class Main
{
public:
  Main():
#ifdef  CONFIG_BT_ENABLED
    beacon_scanner(os::BLEScanner::instance()),
#endif
    wifi(os::Wifi::instance()),
    loop(std::make_shared<os::MainLoop>()),
    mqtt(std::make_shared<os::MqttClient>(loop, "BLEScanner", MQTT_HOST, 8883)),
    task("main_task", std::bind(&Main::main_task, this))
  {
    gpio_pad_select_gpio(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    std::string mac = wifi.get_mac();
    topic_config = "/beaconscanner/" + mac + "/config";
  }

private:
  void on_wifi_system_event(system_event_t event)
  {
    ESP_LOGI(tag, "-> System event %d", event.event_id);
  }

  void on_wifi_timeout()
  {
    ESP_LOGI(tag, "-> Wifi timer");
    wifi_timer = 0;
    if (!wifi.connected().get())
      {
        ESP_LOGI(tag, "-> Wifi failed to connect in time. Reset");
        wifi.reconnect();
        wifi_timer = loop->add_timer(std::chrono::milliseconds(5000), std::bind(&Main::on_wifi_timeout, this));
      }
  }

  void on_wifi_connected(bool connected)
  {
    if (connected)
      {
        ESP_LOGI(tag, "-> Wifi connected");
        loop->cancel_timer(wifi_timer);
        mqtt->set_client_certificate(reinterpret_cast<const char *>(certificate_start), reinterpret_cast<const char *>(private_key_start));
        mqtt->set_ca_certificate(reinterpret_cast<const char *>(ca_start));
        mqtt->set_callback(os::make_slot(loop, [this] (std::string topic, std::string payload) { on_mqtt_data(topic, payload);} ));
        mqtt->connected().connect(loop, std::bind(&Main::on_mqtt_connected, this, std::placeholders::_1));
        mqtt->connect();
      }
    else
      {
        ESP_LOGI(tag, "-> Wifi disconnected");
      }
  }

  void on_mqtt_connected(bool connected)
  {
    if (connected)
      {
        ESP_LOGI(tag, "-> MQTT connected");
        ESP_LOGI(tag, "-> Requesting configuration at %s", topic_config.c_str());
        mqtt->subscribe(topic_config);
        start_beacon_scan();
      }
    else
      {
        ESP_LOGI(tag, "-> MQTT disconnected");
        stop_beacon_scan();
      }
  }

  void on_mqtt_data(std::string topic, std::string payload)
  {
    ESP_LOGI(tag, "-> MQTT %s -> %s (free %d)", topic.c_str(), payload.c_str(), heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

    if (topic == topic_config)
      {
// #ifdef  CONFIG_BT_ENABLED
//         beacon_scanner.start();
// #endif
      }
  }

#ifdef  CONFIG_BT_ENABLED
  void on_beacon_scanner_scan_result(os::BLEScanner::ScanResult result)
  {
    static int led_state = 0;

    if (is_ibeacon(result.adv_data))
      {
        typedef struct
        {
          uint8_t flags[3];
          uint8_t length;
          uint8_t type;
          uint16_t company_id;
          uint16_t beacon_type;
          uint8_t proximity_uuid[16];
          uint16_t major;
          uint16_t minor;
          int8_t measured_power;
        } __attribute__((packed)) ibeacon_data_t;


        const ibeacon_data_t *ibeacon_data = reinterpret_cast<const ibeacon_data_t *>(result.adv_data.data());
        ESP_LOGI(tag, "----------iBeacon Found----------");
        ESP_LOGI(tag, "BLE: Device address: %s", result.mac.c_str());
        esp_log_buffer_hex("BLE: Proximity UUID:", ibeacon_data->proximity_uuid, ESP_UUID_LEN_128);
        uint16_t major = ENDIAN_CHANGE_U16(ibeacon_data->major);
        uint16_t minor = ENDIAN_CHANGE_U16(ibeacon_data->minor);
        ESP_LOGI(tag, "Major: 0x%04x (%d)", major, major);
        ESP_LOGI(tag, "Minor: 0x%04x (%d)", minor, minor);
        ESP_LOGI(tag, "Measured power (RSSI at a 1m distance):%d dbm", ibeacon_data->measured_power);
        ESP_LOGI(tag, "RSSI of packet:%d dbm", result.rssi);
        ESP_LOGI(tag, "---------------------------------");
      }
    else
      {
        ESP_LOGI(tag, "---------- Found----------");
        os::hexdump(tag, reinterpret_cast<const uint8_t *>(result.adv_data.data()), result.adv_data.size());
        ESP_LOGI(tag, "--------------------------");
      }



    ESP_LOGI(tag, "-> BT result %s %d (free %d)", result.mac.c_str(), result.rssi, heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    led_state ^= 1;
    gpio_set_level(LED_GPIO, led_state);
    scan_results[result.mac] = result.rssi;
  }

#endif

  void on_scan_timer()
  {
    ESP_LOGI(tag, "-> Scan timer");
    std::string payload;

    for (auto kv : scan_results)
      {
        payload += kv.first + ":" + std::to_string(kv.second) + "\n";
      }

    if (mqtt->connected().get())
      {
        mqtt->publish("test/beacon", payload);
      }
    scan_results.clear();
  }

  void start_beacon_scan()
  {
    scan_timer = loop->add_periodic_timer(std::chrono::milliseconds(1000), std::bind(&Main::on_scan_timer, this));
#ifdef  CONFIG_BT_ENABLED
    beacon_scanner.start();
#endif
  }

  void stop_beacon_scan()
  {
    loop->cancel_timer(scan_timer);
    scan_timer = 0;

#ifdef  CONFIG_BT_ENABLED
    beacon_scanner.stop();
#endif
  }

  bool is_ibeacon(std::string adv_data)
  {
    static uint8_t ibeacon_prefix[] =
      {
        0x02, 0x01, 0x00, 0x1A, 0xFF, 0x4C, 0x00, 0x02, 0x15
      };

    if (adv_data.size() != 30)
      {
        return false;
      }

    for (int i = 0; i < sizeof(ibeacon_prefix); i++)
      {
        if ( (adv_data[i] != ibeacon_prefix[i]) && (i != 2))
          {
            return false;
          }
      }

    return true;
  }

  void main_task()
  {
    wifi.set_ssid(WIFI_SSID);
    wifi.set_passphase(WIFI_PASS);
    wifi.set_host_name("scan");
    wifi.set_auto_connect(true);
    wifi.system_event_signal().connect(loop, std::bind(&Main::on_wifi_system_event, this, std::placeholders::_1));
    wifi.connected().connect(loop, std::bind(&Main::on_wifi_connected, this, std::placeholders::_1));

#ifdef CONFIG_BT_ENABLED
    beacon_scanner.scan_result_signal().connect(loop, std::bind(&Main::on_beacon_scanner_scan_result, this, std::placeholders::_1));
#endif

    wifi_timer = loop->add_timer(std::chrono::milliseconds(5000), std::bind(&Main::on_wifi_timeout, this));
    wifi.connect();

    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
    // heap_trace_init_standalone(trace_record, NUM_RECORDS);
    // ESP_ERROR_CHECK( heap_trace_start(HEAP_TRACE_ALL) );
    loop->run();
  }

#ifdef  CONFIG_BT_ENABLED
  os::BLEScanner &beacon_scanner;
#endif
  os::Wifi &wifi;
  std::shared_ptr<os::MainLoop> loop;
  std::shared_ptr<os::MqttClient> mqtt;
  os::Task task;
  os::MainLoop::timer_id wifi_timer = 0;
  os::MainLoop::timer_id scan_timer = 0;
  std::map<std::string, int> scan_results;
  std::string topic_config;

  const static gpio_num_t LED_GPIO = GPIO_NUM_5;
};



extern "C" void
app_main()
{
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES)
    {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(tag, "HEAP: startup");
  heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);

  new Main();

  while(1)
    {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
