#include "webSocket.h"
#include "matching_engine.h"
#include <json/json.h>
#include <mutex>
#include <format>
#include <chrono>
#include <iostream>
using namespace std;

void webSocket::sendFullMarketSnapshot(const WebSocketConnectionPtr &conn, const std::string &symbol) {
    auto it = matching_engine::getPendingOrders().find(symbol);
    if (it == matching_engine::getPendingOrders().end()) return;

    auto currentBook = it->second;
    std::lock_guard<std::mutex> lock(currentBook->bookMutex);

    Json::Value snapshot;
    snapshot["timestamp"] = getTime();
    snapshot["symbol"] = symbol;

    Json::Value bidsArray(Json::arrayValue);
    for (const auto &[price, level] : currentBook->bids) {
        Json::Value levelJson;
        levelJson["price"] = price;
        levelJson["total_quantity"] = level.totalQuantity;
        bidsArray.append(levelJson);
    }
    snapshot["bids"] = bidsArray;

    Json::Value asksArray(Json::arrayValue);
    for (const auto &[price, level] : currentBook->asks) {
        Json::Value levelJson;
        levelJson["price"] = price;
        levelJson["total_quantity"] = level.totalQuantity;
        asksArray.append(levelJson);
    }
    snapshot["asks"] = asksArray;
    snapshot["best_bid"] = currentBook->bids.empty() ? "None" : to_string(currentBook->bids.begin()->first);
    snapshot["best_ask"] = currentBook->asks.empty() ? "None" : to_string(currentBook->asks.begin()->first);

    Json::StreamWriterBuilder writer;
    std::string jsonString = Json::writeString(writer, snapshot);
    conn->send(jsonString);
}

void webSocket::handleNewConnection(const HttpRequestPtr &req,
                                    const WebSocketConnectionPtr &conn) {
    auto path = req->path();
    auto symbol = req->getParameter("symbol");

    if (path == "/ws/marketfeed") {
        if (!symbol.empty()) {
            symbolMarketClients[symbol].insert(conn);
            conn->setContext(make_shared<string>(symbol));
            sendFullMarketSnapshot(conn, symbol);
        } else {
            marketClients.insert(conn);
            for (const auto &[sym, _] : matching_engine::getPendingOrders()) {
                sendFullMarketSnapshot(conn, sym);
            }
        }
    } 
    else if (path == "/ws/tradefeed") {
        if (!symbol.empty()) {
            symbolTradeLogs[symbol].insert(conn);
            conn->setContext(make_shared<string>(symbol));
        } else {
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

