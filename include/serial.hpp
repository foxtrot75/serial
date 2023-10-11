#pragma once

#include <string>
#include <vector>

#include <boost/asio/serial_port.hpp>

namespace Serial
{

using namespace std::chrono_literals;
using BoostSerial = boost::asio::serial_port_base;

class Serial
{
public:
    static constexpr auto DefaultTimeout = 5s;

    explicit Serial();

    bool open(
        std::string const& devname,
        uint baudrate,
        BoostSerial::parity parity =
            BoostSerial::parity(
                BoostSerial::parity::none),
        BoostSerial::character_size characterSize =
            BoostSerial::character_size(8),
        BoostSerial::flow_control flowControl =
            BoostSerial::flow_control(
                BoostSerial::flow_control::none),
        BoostSerial::stop_bits stopBits =
            BoostSerial::stop_bits(
                BoostSerial::stop_bits::one));
    void close();

    bool isOpen();

    bool write(std::vector<uint8_t>& buffer);
    bool read(std::vector<uint8_t>& buffer);
    bool readBytes(std::vector<uint8_t>& buffer, uint len);
    bool readUntil(std::vector<uint8_t>& buffer, std::string const& delim);

    void setTimeout(std::chrono::milliseconds timeout);

private:
    class Impl;
    std::shared_ptr<Impl> _impl;
};

}
