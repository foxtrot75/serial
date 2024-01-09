#include <mutex>

#include <boost/bind.hpp>
#include <boost/asio.hpp>

#include <loguru.hpp>

#include "serial.hpp"

namespace boost::asio
{
using steady_deadline_timer = basic_waitable_timer<std::chrono::steady_clock>;
}

namespace Serial
{

namespace asio = boost::asio;
namespace system = boost::system;

class Serial::Impl
    : public std::enable_shared_from_this<Impl>
{
public:
    Impl()
        : _work(_ioc)
        , _port(_ioc)
        , _timer(_ioc)
    {
        for(std::size_t i = 0; i < 2; ++i)
            _threads.push_back(std::make_shared<std::thread>([this]() { _ioc.run(); }));
    }

    virtual ~Impl()
    {
        close();

        _ioc.stop();
        for(auto& thread: _threads)
            thread->join();
    }

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
                BoostSerial::stop_bits::one))
    {
        close();

        try {
            _port.open(devname);
            _port.set_option(BoostSerial::baud_rate(baudrate));
            _port.set_option(parity);
            _port.set_option(characterSize);
            _port.set_option(flowControl);
            _port.set_option(stopBits);
        }
        catch(std::exception const& e) {
            LOG(error) << "Serial port open error: " << e.what();
            return false;
        }

        return true;
    }

    void close()
    {
        if(_port.is_open()) {
            system::error_code ec;
            _port.close(ec);
        }
    }

    bool isOpen()
    {
        return _port.is_open();
    }

    bool write(std::vector<uint8_t>& buffer)
    {
        std::scoped_lock lock(_mutex);
        std::scoped_lock res(_resultMutex);

        _timer.expires_from_now(_timeout);
        _timer.async_wait(
            boost::bind(
                &Impl::_timeoutHandle,
                shared_from_this(),
                asio::placeholders::error));

        asio::async_write(
            _port,
            asio::buffer(buffer),
            boost::bind(
                &Impl::_handle,
                shared_from_this(),
                asio::placeholders::error));

        _condition.wait(_resultMutex);

        if(_ec) {
            LOG(error) << "Write error: " << _ec;
            return false;
        }

        return true;
    }

    bool read(std::vector<uint8_t>& buffer)
    {
        std::scoped_lock lock(_mutex);
        std::scoped_lock res(_resultMutex);

        _timer.expires_from_now(_timeout);
        _timer.async_wait(
            boost::bind(
                &Impl::_timeoutHandle,
                shared_from_this(),
                asio::placeholders::error));

        asio::async_read(
            _port,
            asio::buffer(buffer),
            boost::bind(
                &Impl::_handle,
                shared_from_this(),
                asio::placeholders::error));

        _condition.wait(_resultMutex);

        if(_ec) {
            LOG(error) << "Read error: " << _ec;
            return false;
        }

        return true;
    }

    bool readBytes(std::vector<uint8_t>& buffer, uint len)
    {
        std::scoped_lock lock(_mutex);
        std::scoped_lock res(_resultMutex);

        _timer.expires_from_now(_timeout);
        _timer.async_wait(
            boost::bind(
                &Impl::_timeoutHandle,
                shared_from_this(),
                asio::placeholders::error));

        if(buffer.size() < len)
            buffer.resize(len);

        asio::async_read(
            _port,
            asio::buffer(buffer),
            asio::transfer_at_least(len),
            boost::bind(
                &Impl::_handle,
                shared_from_this(),
                asio::placeholders::error));

        _condition.wait(_resultMutex);

        if(_ec) {
            LOG(error) << "Read bytes error: " << _ec;
            return false;
        }

        return true;
    }

    bool readUntil(std::vector<uint8_t>& buffer, std::string const& delim)
    {
        std::scoped_lock lock(_mutex);
        std::scoped_lock res(_resultMutex);

        _timer.expires_from_now(_timeout);
        _timer.async_wait(
            boost::bind(
                &Impl::_timeoutHandle,
                shared_from_this(),
                asio::placeholders::error));

        boost::asio::streambuf streambuf;
        asio::async_read_until(
            _port,
            streambuf,
            delim,
            boost::bind(
                &Impl::_handle,
                shared_from_this(),
                asio::placeholders::error));

        _condition.wait(_resultMutex);

        buffer.resize(streambuf.size());
        boost::asio::buffer_copy(boost::asio::buffer(buffer), streambuf.data());

        if(_ec) {
            LOG(error) << "Read until error: " << _ec;
            return false;
        }

        return true;
    }

    void setTimeout(std::chrono::milliseconds timeout)
    {
        _timeout = timeout;
    }

    Impl(Impl const&) = delete;
    Impl& operator=(Impl const&) = delete;

private:
    void _handle(system::error_code const& ec)
    {
        _ec = ec;

        if(ec == asio::error::operation_aborted)
            return;

        std::scoped_lock lock(_resultMutex);
        if(ec)
            close();

        _timer.cancel();
        _condition.notify_all();
    }

    void _timeoutHandle(system::error_code const& ec)
    {
        if(ec == asio::error::operation_aborted)
            return;

        std::scoped_lock lock(_resultMutex);
        close();
        _condition.notify_all();
    }

private:
    asio::io_context                            _ioc;
    asio::io_context::work                      _work;
    asio::serial_port                           _port;
    asio::steady_deadline_timer                 _timer;
    std::vector<std::shared_ptr<std::thread>>   _threads;
    std::condition_variable_any                 _condition;
    std::mutex                                  _mutex;
    std::mutex                                  _resultMutex;
    std::chrono::milliseconds                   _timeout = Serial::DefaultTimeout;
    system::error_code                          _ec;
};

Serial::Serial()
    : _impl(std::make_shared<Impl>())
{}

bool Serial::open(
    std::string const& devname,
    uint baudrate,
    BoostSerial::parity parity,
    BoostSerial::character_size characterSize ,
    BoostSerial::flow_control flowControl,
    BoostSerial::stop_bits stopBits)
{
    return _impl->open(devname, baudrate, parity, characterSize, flowControl, stopBits);
}

void Serial::close()
{
    return _impl->close();
}

bool Serial::isOpen()
{
    return _impl->isOpen();
}

bool Serial::write(std::vector<uint8_t>& buffer)
{
    return _impl->write(buffer);
}

bool Serial::read(std::vector<uint8_t>& buffer)
{
    return _impl->read(buffer);
}

bool Serial::readBytes(std::vector<uint8_t>& buffer, uint len)
{
    return _impl->readBytes(buffer, len);
}

bool Serial::readUntil(std::vector<uint8_t>& buffer, std::string const& delim)
{
    return _impl->readUntil(buffer, delim);
}

void Serial::setTimeout(std::chrono::milliseconds timeout)
{
    return _impl->setTimeout(timeout);
}

}
