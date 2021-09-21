#pragma once

#include <chrono>
#include <list>
#include <memory>
#include <string>

namespace agile {
namespace unix_tcp_client {

// 连接key类型
using ConnKey = int32_t;

/**
 * @brief unix_tcp_client 连接实例类
 */
class UnixTcpClient : public std::enable_shared_from_this<UnixTcpClient>
{
   public:
    UnixTcpClient() = delete;
    /**
     * @brief UnixTcpClient 构造函数
     * @param key 连接key
     * @param socket_file unix socket file
     */
    UnixTcpClient(const ConnKey& key, const std::string& socket_file) : key_(key), socket_file_(socket_file) {}

    virtual ~UnixTcpClient();

    /**
     * @brief 开启连接函数
     * @param conn_timeout 连接超时
     * @param opt_timeout SND RCV 超时
     * @return bool
     */
    bool Start(const timeval& conn_timeout, const timeval& opt_timeout);

    /**
     * @brief 发送消息函数
     * @param msg string消息内容
     * @param cur_time 当前时间
     * @return bool
     */
    bool DoWrite(const std::shared_ptr<std::string>& msg, const std::chrono::steady_clock::time_point& cur_time);

    /**
     * @brief 读取消息函数
     * @param cur_time 当前时间
     * @return bool
     */
    bool DoRead(const std::chrono::steady_clock::time_point& cur_time);

    /**
     * @brief 外部主动断开连接函数
     * @param reason 原因描述
     */
    void Close(const std::string& reason);

    /**
     * @brief 是否连接中
     * @return bool
     */
    bool IsConnected() const { return connected_; }

    /**
     * @brief 获取最近一次成功发送数据的时间点
     * @return time_point
     */
    std::chrono::steady_clock::time_point GetLastSendTime() const { return last_send_time_; }

   private:
    /**
     * @brief 设置options选项
     * @param opt_timeout SND RCV 超时
     */
    void SetOptions(const timeval& opt_timeout);

    /**
     * @brief 设置socket为非阻塞
     * @param on bool true为打开非阻塞
     * @return bool 是否成功
     */
    bool SetNoBlocking(bool on);

   private:
    // 连接key
    ConnKey key_ = 0;
    // 是否调用start开启connect
    bool started_ = false;
    // 是否已经连接
    bool connected_ = false;
    // socket id
    int socket_id_ = 0;
    // socket file
    std::string socket_file_;
    // 读消息缓冲大小
    static constexpr uint32_t kTcpReadBufferSize = 5120;
    // 读消息缓冲
    unsigned char tcp_read_buffer_[kTcpReadBufferSize];
    // 最近一次成功发送数据的时间点
    std::chrono::steady_clock::time_point last_send_time_;
};

}  // namespace unix_tcp_client
}  // namespace agile