#pragma once

// Start the Unix domain socket acceptor on a dedicated thread.
// Binds to DAEMON_SOCKET_PATH and handles SubscribeRequest / SubscribeResponse.
void start_acceptor();

// Stop the acceptor thread and clean up the socket file.
void stop_acceptor();
