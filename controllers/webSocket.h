#pragma once
#include <drogon/WebSocketController.h>
#include <json/json.h>
#include <iostream>
using namespace drogon;
using namespace std;


class webSocket : public drogon::WebSocketController<webSocket> {
public:
    static inline unordered_set<WebSocketConnectionPtr> marketClients;
    static inline unordered_set<WebSocketConnectionPtr> tradeLogs;
    static inline unordered_map<string, unordered_set<WebSocketConnectionPtr>> symbolTradeLogs;

    void handleNewConnection(const HttpRequestPtr &req,
                             const WebSocketConnectionPtr &conn) override;

    void handleConnectionClosed(const WebSocketConnectionPtr &conn) override;

    void handleNewMessage(const WebSocketConnectionPtr &conn,
                        std::string &&message,
                       const WebSocketMessageType &type) override;

    static void broadcastMarketUpdate(const string &jsonMsg);
    static void broadcastTradeUpdate(const string &jsonMsg, const string &symbol = "");

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/marketfeed");
    WS_PATH_ADD("/ws/tradefeed"); // tradefeed?symbol=BTCUSDT
    WS_PATH_LIST_END
};
