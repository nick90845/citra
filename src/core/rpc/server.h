#pragma once

#include "common/threadsafe_queue.h"
#include "core/rpc/packet.h"
#include "core/rpc/zmq_server.h"

namespace RPC {

class RPCServer;

class Server {
public:
    Server(RPCServer& rpc_server);
    void Start();
    void Stop();
    void NewRequestCallback(std::unique_ptr<RPC::Packet> new_request);

private:
    RPCServer& rpc_server;
    std::unique_ptr<ZMQServer> zmq_server;
};

} // namespace RPC
