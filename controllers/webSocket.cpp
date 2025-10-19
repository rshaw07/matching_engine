#include "webSocket.h"

void webSocket::handleNewConnection(const HttpRequestPtr &req,
                                       const WebSocketConnectionPtr &conn) {
    auto path = req->path();
    if (path == "/ws/marketfeed") {
        marketClients.insert(conn);
    } else if (path == "/ws/tradefeed") {
        auto symbol = req->getParameter("symbol");
        tradeLogs.insert(conn);
        if(!symbol.empty()){
            symbolTradeLogs[symbol].insert(conn);
            conn->setContext(make_shared<string>(symbol));
        }
    }
}

void webSocket::handleConnectionClosed(const WebSocketConnectionPtr &conn) {
    marketClients.erase(conn);
    tradeLogs.erase(conn);

    auto context = conn->getContext<string>();
    if(context){
        auto symbol = *context;
        symbolTradeLogs[symbol].erase(conn);

        if(symbolTradeLogs[symbol].empty()){
            symbolTradeLogs.erase(symbol);
        }
    }
}

void webSocket::handleNewMessage(const WebSocketConnectionPtr &conn,
                                 std::string &&message,
                                 const WebSocketMessageType &type) {
    if (type == WebSocketMessageType::Text) {   
        cout << "Received message: " << message << endl;
    }
}

void webSocket::broadcastMarketUpdate(const string &jsonMsg) {
    for (auto &client : marketClients) {
        if (client && client->connected()) {
            client->send(jsonMsg);
        }
    }
}

void webSocket::broadcastTradeUpdate(const string &jsonMsg, const string &symbol) {
    auto it = symbolTradeLogs.find(symbol);
    if (it != symbolTradeLogs.end()) {
        for (auto &client : it->second) {
            if (client && client->connected()) {
                client->send(jsonMsg);
            }
        }
    }
}
