#pragma once

// TCP server for remote pub/sub clients.
// Runs on a dedicated thread, spawns a thread per client connection.
// Each client thread bridges the remote client to a local shm ring buffer.

void start_tcp_server();
void stop_tcp_server();
