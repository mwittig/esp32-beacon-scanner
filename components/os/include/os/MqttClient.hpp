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

#ifndef OS_MQTT_HPP
#define OS_MQTT_HPP

#include <string>
#include <memory>
#include <list>
#include <system_error>

#include "os/TLSStream.hpp"
#include "os/bitmask.hpp"

namespace os
{
  enum class PublishOptions : uint8_t
    {
      None          = 0,
      Retain        = 0b00000001u,
    };
  DEFINE_BITMASK(PublishOptions);

  // TODO: split class
  class MqttClient : public std::enable_shared_from_this<MqttClient>
  {
  public:
    using subscribe_function_t = void(std::string topic, std::string payload);
    using subscribe_callback_t = std::function<subscribe_function_t>;
    using subscribe_slot_t = os::Slot<subscribe_function_t>;

    MqttClient(std::shared_ptr<MainLoop> loop, std::string client_id, std::string host, int port);
    ~MqttClient() = default;

  public:
    void set_client_certificate(const char *cert, const char *key);
    void set_ca_certificate(const char *cert);
    void set_callback(subscribe_callback_t callback);
    void set_callback(subscribe_slot_t slot);
    void set_client_id(std::string client_id);
    void set_username(std::string username);
    void set_password(std::string username);
    void set_will(std::string topic, std::string data);
    void set_will_retain(bool retain);

    void connect();
    void publish(std::string topic, std::string payload, PublishOptions options = PublishOptions::None);
    void subscribe(std::string topic);
    void unsubscribe(std::string topic);

    os::Property<bool> &connected();

  private:
    void send_connect();
    void send_ping();
    void send_publish(std::string topic, std::string payload, PublishOptions options = PublishOptions::None);
    void send_subscribe(std::list<std::string> topics);
    void send_unsubscribe(std::list<std::string> topics);

    void async_read_control_packet();
    void async_read_remaining_length();
    void async_read_payload();

    void handle_control_packet();
    void handle_remaining_length();

    std::error_code handle_payload();
    std::error_code handle_connect_ack();
    std::error_code handle_publish();
    std::error_code handle_publish_ack();
    std::error_code handle_publish_received();
    std::error_code handle_publish_release();
    std::error_code handle_publish_complete();
    std::error_code handle_subscribe_ack();
    std::error_code handle_unsubscribe_ack();
    std::error_code handle_ping_response();

    void handle_error(std::string what, std::error_code ec);
    std::error_code verify(std::string what, std::size_t actual_size, std::size_t expect_size, std::error_code ec = std::error_code());

  private:
    std::shared_ptr<MainLoop> loop;
    std::shared_ptr<os::TLSStream> sock;
    std::string client_id;
    std::string host;
    int port = 0;
    std::string username;
    std::string password;
    std::string will_topic;
    std::string will_data;
    bool will_retain = false;
    const char *client_cert = nullptr;
    const char *client_key = nullptr;
    const char *ca_cert = nullptr;
    uint8_t header_buffer = 0;
    std::string payload_buffer;
    std::size_t remaining_length = 0;
    int remaining_length_multiplier = 1;
    uint8_t fixed_header = 0;;
    MainLoop::timer_id ping_timer = 0;
    int packet_id = 0;
    subscribe_slot_t subscribe_slot;
    os::Property<bool> connected_property { false };
    int pending_ping_count = 0;

    static constexpr int ping_interval_sec = 15;
    static constexpr int keep_alive_sec = 60;
    static constexpr int pending_ping_count_limit = 5;
  };
}

#endif // OS_MQTT_HPP
