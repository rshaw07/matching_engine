#include "webSocket.h"

void webSocket::handleNewConnection(const HttpRequestPtr &req,
                                       const WebSocketConnectionPtr &conn) {
    auto path = req->path();
    auto symbol = req->getParameter("symbol");
    if (path == "/ws/marketfeed") {
        if(!symbol.empty()){
            symbolMarketClients[symbol].insert(conn);
            conn->setContext(make_shared<string>(symbol));
        }
        else{
            marketClients.insert(conn);
        }
    } else if (path == "/ws/tradefeed") {
        if(!symbol.empty()){
            symbolTradeLogs[symbol].insert(conn);
            conn->setContext(make_shared<string>(symbol));
        }
        else{
            tradeLogs.insert(conn);
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

void webSocket::broadcastMarketUpdate(const string &jsonMsg, const string &symbol) {
    for (auto &client : marketClients) {
        if (client && client->connected()) {
            client->send(jsonMsg);
        }
    }
    auto it = symbolMarketClients.find(symbol);
    if(it != symbolMarketClients.end()){
        for (auto &client : it->second) {
            if (client && client->connected()) {
                client->send(jsonMsg);
            }
        }
    }
}

void webSocket::broadcastTradeUpdate(const string &jsonMsg, const string &symbol) {

    for (auto &client : tradeLogs) {
        if (client && client->connected()) {
            client->send(jsonMsg);
        }
    }
    auto it = symbolTradeLogs.find(symbol);
    if(it != symbolTradeLogs.end()){
        for (auto &client : it->second) {
            if (client && client->connected()) {
                client->send(jsonMsg);
            }
        }
    }

}
