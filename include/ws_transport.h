#ifndef WS_TRANSPORT_H
#define WS_TRANSPORT_H

#include <string>
#include <vector>

/**
 * @file ws_transport.h
 * @brief Transport seam for the Warframe.Market WebSocket.
 *
 * The protocol layer (subscribe, dispatch, event ordering) is the part with
 * logic worth testing, and it should not need a live socket to be exercised.
 * Implementations of this interface carry JSON text frames; tests supply
 * frames directly.
 */
class WsTransport
{
public:
    virtual ~WsTransport() {}

    /**
     * @brief Open the connection.
     * @param url WebSocket URL, e.g. wss://ws.warframe.market/socket.
     * @param subprotocol Value for Sec-WebSocket-Protocol; the server rejects
     *        connections that do not request `wfm`.
     * @throws std::runtime_error when the handshake fails.
     */
    virtual void connect(const std::string &url, const std::string &subprotocol) = 0;

    /// Close the connection. Safe to call when already closed.
    virtual void close() = 0;

    virtual bool isConnected() const = 0;

    /**
     * @brief Send one text frame.
     * @throws std::runtime_error when the connection is not open.
     */
    virtual void sendText(const std::string &message) = 0;

    /**
     * @brief Receive one text frame, blocking up to `timeout_ms`.
     *
     * Binary frames, pings, and pongs are handled by the implementation and
     * are not surfaced.
     *
     * @param timeout_ms How long to wait before giving up.
     * @param out Set to the frame payload when one arrives.
     * @return True when a frame was received; false on timeout.
     */
    virtual bool receive(std::string &out, int timeout_ms) = 0;
};

#endif // WS_TRANSPORT_H
