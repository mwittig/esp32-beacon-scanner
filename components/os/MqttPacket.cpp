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

#include "os/MqttPacket.hpp"

static const char tag[] = "MQTT";

using namespace os;

void
MqttPacket::add(uint8_t value)
{
  data_.push_back(value);
}

void
MqttPacket::append(std::string s)
{
  data_.append(s);
}

void
MqttPacket::add(std::string str)
{
  std::size_t size = str.size();
  data_.push_back(static_cast<uint8_t>(size >> 8));
  data_.push_back(static_cast<uint8_t>(size & 0xff));
  data_.append(str);
}

void
MqttPacket::add_length(std::size_t size)
{
  do
    {
      uint8_t b = size % 128;
      size >>= 7;

      if (size > 0)
        {
          b |= 128;
        }
      data_.push_back(b);
    } while (size > 0);
}

void
MqttPacket::add_fixed_header(os::PacketType type, std::uint8_t flags)
{
  data_.push_back((static_cast<std::uint8_t>(type) << 4) | (flags & 0x0f));
}

Buffer
MqttPacket::to_buffer()
{
  return Buffer(data(), size());
}

uint8_t *
MqttPacket::data() noexcept
{
  return data_.size() ? reinterpret_cast<uint8_t *>(&data_[0]) : nullptr;
}

std::size_t
MqttPacket::size() const noexcept
{
  return data_.size();
}
