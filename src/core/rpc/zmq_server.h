#pragma once

#include <functional>
#include <thread>

#include "core/rpc/packet.h"

#define ZMQ_STATIC
#include <zmq.hpp>

namespace RPC {

class ZMQServer {
public:
    explicit ZMQServer(std::function<void(std::unique_ptr<Packet>)> new_request_callback);
    ~ZMQServer();

private:
    void WorkerLoop();
    void SendReply(Packet& request);

    std::thread worker_thread;
    std::atomic_bool running = true;

    std::unique_ptr<zmq::context_t> zmq_context;
    std::unique_ptr<zmq::socket_t> zmq_socket;

    std::function<void(std::unique_ptr<Packet>)> new_request_callback;
};

} // namespace RPC
