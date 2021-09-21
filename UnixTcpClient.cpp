#include "report_client/UnixTcpClient.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace robata {
namespace report_client {

UnixTcpClient::~UnixTcpClient()
{
    Close("tcp conn release");
    std::cout << " release socket_file: " << socket_file_ << ", key:" << key_ << std::endl;
}

bool UnixTcpClient::Start(const timeval& conn_timeout, const timeval& opt_timeout)
{
    if (started_)
    {
        std::cout << " key:" << key_ << ", socket_file:" << socket_file_ << " started " << std::endl;
        return false;
    }

    started_ = true;

    std::cout << "Start key:" << key_ << ", socket_file:" << socket_file_ << std::endl;

    socket_id_ = socket(AF_UNIX, SOCK_STREAM, 0);

    struct sockaddr_un servaddr;
    servaddr.sun_family = AF_UNIX;
    strcpy(servaddr.sun_path, socket_file_.c_str());

    int ret = setsockopt(socket_id_, SOL_SOCKET, SO_SNDTIMEO, (char*)&conn_timeout, sizeof(struct timeval));
    if (ret != 0)
    {
        std::cout << "SNDTIMEO errno:" << (int)errno << ", err:" << strerror(errno) << std::endl;
    }

    ret = connect(socket_id_, (struct sockaddr*)&servaddr, sizeof(servaddr));
    if (0 == ret)
    {
        connected_ = true;
        last_send_time_ = std::chrono::steady_clock::now();

        SetOptions(opt_timeout);
        SetNoBlocking(true);

        std::cout << "OnConnected socket_file:" << socket_file_ << ", key:" << key_ << std::endl;
        return true;
    }
    else
    {
        std::cout << "fail to connect socket_file:" << socket_file_ << ", key:" << key_
                                     << " ret:" << ret << " errno:" << (int)errno << ", err:" << strerror(errno) << std::endl;
        close(socket_id_);
        return false;
    }
}

bool UnixTcpClient::SetNoBlocking(bool on)
{
    int flags = fcntl(socket_id_, F_GETFL, 0);
    if (-1 == flags)
    {
        std::cout << "fail to fcntl socket_file:" << socket_file_ << ", key:" << key_
                                     << ", errno:" << (int)errno << ", err:" << strerror(errno) << std::endl;
        return false;
    }

    int val = on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (-1 == fcntl(socket_id_, F_SETFL, val))
    {
        std::cout << "fail to fcntl socket_file:" << socket_file_ << ", key:" << key_
                                     << ", errno:" << (int)errno << ", err:" << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

void UnixTcpClient::SetOptions(const timeval& opt_timeout)
{
    int ret = setsockopt(socket_id_, SOL_SOCKET, SO_SNDTIMEO, (char*)&opt_timeout, sizeof(struct timeval));
    if (0 != ret)
    {
        std::cout << "SNDTIMEO errno:" << (int)errno << ", err:" << strerror(errno) << std::endl;
    }

    ret = setsockopt(socket_id_, SOL_SOCKET, SO_RCVTIMEO, (char*)&opt_timeout, sizeof(struct timeval));
    if (0 != ret)
    {
        std::cout << "RCVTIMEO errno:" << (int)errno << ", err:" << strerror(errno) << std::endl;
    }

    int optval = 1;
    ret = setsockopt(socket_id_, IPPROTO_TCP, TCP_NODELAY, (const char*)&optval, sizeof(int));
    if (0 != ret)
    {
        std::cout << "NODELAY errno:" << (int)errno << ", err:" << strerror(errno) << std::endl;
    }

    ret = setsockopt(socket_id_, SOL_SOCKET, SO_KEEPALIVE, (const char*)&optval, sizeof(int));
    if (0 != ret)
    {
        std::cout << "KEEPALIVE errno:" << (int)errno << ", err:" << strerror(errno) << std::endl;
    }

    linger linger_val;
    linger_val.l_onoff = optval;
    linger_val.l_linger = 0;
    ret = setsockopt(socket_id_, SOL_SOCKET, SO_LINGER, (const char*)&linger_val, sizeof(linger));
    if (0 != ret)
    {
        std::cout << "LINGER errno:" << (int)errno << ", err:" << strerror(errno) << std::endl;
    }

    size_t buffer_size = kTcpReadBufferSize;
    ret = setsockopt(socket_id_, SOL_SOCKET, SO_SNDBUF, (const char*)&buffer_size, sizeof(size_t));
    if (0 != ret)
    {
        std::cout << "SNDBUF errno:" << (int)errno << ", err:" << strerror(errno) << std::endl;
    }
}

void UnixTcpClient::Close(const std::string& reason)
{
    if (!started_)
    {
        return;
    }
    started_ = false;

    if (connected_)
    {
        close(socket_id_);
    }
    connected_ = false;

    std::cout << " TcpConn Close socket_file: " << socket_file_ << ", key:" << key_
                                 << ", reason:" << reason << std::endl;
}

bool UnixTcpClient::DoRead(const std::chrono::steady_clock::time_point& cur_time)
{
    if (!connected_)
    {
        return true;
    }

    int len = recv(socket_id_, tcp_read_buffer_, sizeof(tcp_read_buffer_), 0);
    if (len < 0)
    {
        if (errno == EAGAIN || errno == EINTR)
        {
            return true;
        }
        else
        {
            std::cout << " DoRead key:" << key_ << ", socket_file:" << socket_file_
                                         << " errno:" << (int)errno << ", err:" << strerror(errno) << std::endl;
            connected_ = false;
            return false;
        }
    }

    if (0 == len)
    {
        connected_ = false;
        return false;
    }
    return true;
}

bool UnixTcpClient::DoWrite(const std::shared_ptr<std::string>& msg,
                           const std::chrono::steady_clock::time_point& cur_time)
{
    if (!msg)
    {
        return false;
    }

    if (!connected_)
    {
        return false;
    }

    static constexpr uint32_t kSendUsleepTime = 10000;
    static constexpr int kSendLoopLimit = 10;

    int loop_limit = kSendLoopLimit;
    size_t trace_index = 0;
    size_t data_len = msg->length();
    while (trace_index < data_len && --loop_limit > 0)
    {
        size_t send_size = data_len - trace_index;
        int len = send(socket_id_, msg->c_str() + trace_index, send_size, MSG_NOSIGNAL);
        if (len > 0)
        {
            last_send_time_ = cur_time;
            trace_index += len;
            if (send_size == (size_t)len)
            {
                return true;
            }
            else
            {
                usleep(kSendUsleepTime);
                continue;
            }
        }
        if (EAGAIN == errno || EINTR == errno || EINPROGRESS == errno)
        {
            usleep(kSendUsleepTime);
            continue;
        }
        std::cout << " DoWrite key:" << key_ << ", socket_file:" << socket_file_
                                     << " errno:" << (int)errno << ", err:" << strerror(errno) << std::endl;
        return false;
    }
    std::cout << "fail to DoWrite key:" << key_ << ", socket_file:" << socket_file_
                                 << ", loop_limit:" << loop_limit << std::endl;
    return false;
}

}  // namespace report_client
}  // namespace robata