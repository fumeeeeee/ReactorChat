#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <algorithm>
#include <mutex>

struct MSG_header
{
    char sender_name[64];
    int Type;
    size_t length;
};

enum MSG_type
{
    REGISTER,
    REGISTER_success,
    REGISTER_failed,
    LOGIN,
    LOGIN_success,
    LOGIN_failed,
    INITIAL,
    JOIN,
    EXIT,
    GROUP_MSG,
    FILE_MSG,
    FILE_DATA,
    FILE_END,
    TEST,
    TEST_success
};

std::atomic<long long> total_sent(0);
std::atomic<long long> send_failed(0);
std::atomic<long long> total_requests(0);
std::atomic<long long> recv_failed(0);

std::mutex latency_mutex;
std::vector<long long> latencies_us; // 单次请求延迟，微秒

bool create_connection(const char *ip, int port, int &sockfd)
{
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        return false;

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serv_addr.sin_addr);

    if (connect(sockfd, (sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        close(sockfd);
        return false;
    }

    // 设置1秒超时
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return true;
}

void worker_thread(const char *ip, int port, int conn_per_thread, int duration_sec, int thread_idx)
{
    std::vector<int> sockets;
    sockets.reserve(conn_per_thread);

    for (int i = 0; i < conn_per_thread; ++i)
    {
        int sockfd;
        if (create_connection(ip, port, sockfd))
        {
            sockets.push_back(sockfd);
        }
    }

    auto end_time = std::chrono::steady_clock::now() + std::chrono::seconds(duration_sec);

    while (std::chrono::steady_clock::now() < end_time)
    {
        for (size_t i = 0; i < sockets.size(); ++i)
        {
            int sock = sockets[i];

            std::string uname = "user_" + std::to_string(thread_idx) + "_" + std::to_string(i);
            std::string msg = "this is a test";

            MSG_header header{};
            strncpy(header.sender_name, uname.c_str(), sizeof(header.sender_name) - 1);
            header.Type = TEST;
            header.length = msg.size();

            size_t total_size = sizeof(header) + msg.size();
            std::vector<char> send_buf(total_size);
            memcpy(send_buf.data(), &header, sizeof(header));
            memcpy(send_buf.data() + sizeof(header), msg.data(), msg.size());

            auto send_start = std::chrono::steady_clock::now();
            ssize_t sent_bytes = send(sock, send_buf.data(), send_buf.size(), 0);
            if (sent_bytes != (ssize_t)send_buf.size())
            {
                send_failed++;
                continue;
            }
            total_sent++;

            MSG_header response{};
            ssize_t recved = recv(sock, &response, sizeof(response), 0);
            auto recv_end = std::chrono::steady_clock::now();

            if (recved != sizeof(response))
            {
                recv_failed++;
                continue;
            }
            total_requests++;

            if (response.Type == TEST_success)
            {
                // 计算延迟，单位微秒
                long long latency_us = std::chrono::duration_cast<std::chrono::microseconds>(recv_end - send_start).count();
                {
                    std::lock_guard<std::mutex> lock(latency_mutex);
                    latencies_us.push_back(latency_us);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 每10ms轮询一次
    }

    for (int sock : sockets)
    {
        close(sock);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 5)
    {
        std::cout << "用法: " << argv[0] << " <IP> <端口> <线程数> <每线程连接数> [持续时间秒]\n";
        return 1;
    }

    const char *ip = argv[1];
    int port = std::atoi(argv[2]);
    int thread_num = std::atoi(argv[3]);
    int conn_per_thread = std::atoi(argv[4]);
    int duration = argc > 5 ? std::atoi(argv[5]) : 10;

    auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < thread_num; ++i)
    {
        threads.emplace_back(worker_thread, ip, port, conn_per_thread, duration, i);
    }

    for (auto &t : threads)
        t.join();

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    // 计算延迟统计数据
    double avg_latency_ms = 0;
    long long max_latency_us = 0;
    {
        std::lock_guard<std::mutex> lock(latency_mutex);
        if (!latencies_us.empty())
        {
            long long sum_us = 0;
            max_latency_us = *std::max_element(latencies_us.begin(), latencies_us.end());
            for (auto l : latencies_us)
                sum_us += l;
            avg_latency_ms = (sum_us / (double)latencies_us.size()) / 1000.0;
        }
    }

    std::cout << "总发送请求数: " << total_sent.load() << "\n";
    std::cout << "发送失败数: " << send_failed.load() << "\n";
    std::cout << "总收到响应数: " << total_requests.load() << "\n";
    std::cout << "接收失败数: " << recv_failed.load() << "\n";
    std::cout << "QPS: " << static_cast<long long>(total_requests.load() / elapsed.count()) << "\n";
    std::cout << "平均响应延迟: " << avg_latency_ms << " ms\n";
    std::cout << "最大响应延迟: " << max_latency_us / 1000.0 << " ms\n";
    std::cout << "实际运行时长: " << elapsed.count() << " 秒\n";

    return 0;
}
