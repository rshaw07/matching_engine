#include "matching_engine.h"
#include "webSocket.h"
#include<iostream>  
#include <algorithm>
#include <deque>
#include <map>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <ctime>

// Add definition of your processing function here
void matching_engine::recordTrades(const Order &buyOrder, const Order &sellOrder, double tradeQuantity, double tradePrice, string aggressorSide){
    cout<<"recordTrades called."<<endl;
    Trade newTrade;
    newTrade.tradeId = to_string(rand());
    newTrade.makerOrderId = (aggressorSide == "buy") ? sellOrder.orderId : buyOrder.orderId;
    newTrade.takerOrderId = (aggressorSide == "buy") ? buyOrder.orderId : sellOrder.orderId;
    newTrade.aggressor = aggressorSide;
    newTrade.symbol = buyOrder.symbol;
    newTrade.price = to_string(tradePrice);
    newTrade.quantity = to_string(tradeQuantity);
    newTrade.timestamp = to_string(time(nullptr));
    // Store or process the trade as needed
    tradeHistory[buyOrder.symbol].push_back(newTrade);
    // Broadcast trade update via WebSocket
    Json::Value tradeJson;
    tradeJson["trade_id"] = newTrade.tradeId;
    tradeJson["maker_order_id"] = newTrade.makerOrderId;
    tradeJson["taker_order_id"] = newTrade.takerOrderId;
    tradeJson["aggressor"] = newTrade.aggressor;
    tradeJson["symbol"] = newTrade.symbol;
    tradeJson["price"] = newTrade.price;
    tradeJson["quantity"] = newTrade.quantity;
    tradeJson["timestamp"] = newTrade.timestamp;
    Json::StreamWriterBuilder writer;
    string jsonString = Json::writeString(writer, tradeJson);
    webSocket::broadcastTradeUpdate(jsonString, buyOrder.symbol);

}

void matching_engine::submitOrder(Order parsedOrder){
    if(pendingOrders.find(parsedOrder.symbol) == pendingOrders.end()){
        pendingOrders[parsedOrder.symbol] = make_shared<OrderBook>();
    }
    auto currentBook = pendingOrders[parsedOrder.symbol];
    lock_guard<mutex> lock(currentBook->bookMutex);
    if(parsedOrder.orderType == "limit"){
        double price = stod(parsedOrder.price);
        double quantity = stod(parsedOrder.quantity);
        if(parsedOrder.side == "buy"){
            if(currentBook->asks.empty() || price < currentBook->asks.begin()->first){
                currentBook->bids[price].orders.push_back(make_shared<Order>(parsedOrder));
                currentBook->bids[price].totalQuantity += quantity;
            }
            else{
                auto it = currentBook->asks.begin();
                while(it != currentBook->asks.end() && price >= it->first && quantity > 0){
                    auto &level = it->second;
                    while(!level.orders.empty() && quantity > 0){
                        auto &sellOrder = level.orders.front();
                        double sellQuantity = stod(sellOrder->quantity);
                        double tradeQuantity = min(sellQuantity, quantity);
                        recordTrades(parsedOrder, *sellOrder, tradeQuantity, it->first, "buy");
                        if(sellQuantity <= quantity){
                            quantity -= sellQuantity;
                            level.totalQuantity -= sellQuantity;
                            level.orders.pop_front();
                        }
                        else{
                            sellOrder->quantity = to_string(sellQuantity - quantity);
                            level.totalQuantity -= quantity;
                            quantity = 0;
                        }

                    }
                    if(level.orders.empty()){
                        it = currentBook->asks.erase(it);
                    }
                    else{
                        ++it;
                    }
                }
                if(quantity > 0){
                    parsedOrder.quantity = to_string(quantity);
                    currentBook->bids[price].orders.push_back(make_shared<Order>(parsedOrder));
                    currentBook->bids[price].totalQuantity += quantity;
                }   
            }
        }
        else{
            if(currentBook->bids.empty() || price > currentBook->bids.begin()->first){
                currentBook->asks[price].orders.push_back(make_shared<Order>(parsedOrder));
                currentBook->asks[price].totalQuantity += quantity;
            }
            else{
                auto it = currentBook->bids.begin();
                while(it != currentBook->bids.end() && price <= it->first && quantity > 0){
                    auto &level = it->second;
                    while(!level.orders.empty() && quantity > 0){
                        auto &buyOrder = level.orders.front();
                        double buyQuantity = stod(buyOrder->quantity);
                        double tradeQuantity = min(buyQuantity, quantity);
                        recordTrades(*buyOrder, parsedOrder, tradeQuantity, it->first, "sell");
                        if(buyQuantity <= quantity){
                            quantity -= buyQuantity;
                            level.totalQuantity -= buyQuantity;
                            level.orders.pop_front();
                        }
                        else{
                            buyOrder->quantity = to_string(buyQuantity - quantity);
                            level.totalQuantity -= quantity;
                            quantity = 0;
                        }

                    }
                    if(level.orders.empty()){
                        it = currentBook->bids.erase(it);
                    }
                    else{
                        ++it;
                    }
                }
                if(quantity > 0){
                    parsedOrder.quantity = to_string(quantity);
                    currentBook->asks[price].orders.push_back(make_shared<Order>(parsedOrder));
                    currentBook->asks[price].totalQuantity += quantity;
                }   
            }
        }
    }
    else if(parsedOrder.orderType == "market"){
        double quantity = stod(parsedOrder.quantity);
        if(parsedOrder.side == "buy"){
            auto it = currentBook->asks.begin();
            while(it != currentBook->asks.end() && quantity > 0){
                auto &level = it->second;
                while(!level.orders.empty() && quantity > 0){
                    auto &sellOrder = level.orders.front();
                    double sellQuantity = stod(sellOrder->quantity);
                    double tradeQuantity = min(sellQuantity, quantity);
                    recordTrades(parsedOrder, *sellOrder, tradeQuantity, it->first, "buy");
                    if(sellQuantity <= quantity){
                        quantity -= sellQuantity;
                        level.totalQuantity -= sellQuantity;
                        level.orders.pop_front();
                    }
                    else{
                        sellOrder->quantity = to_string(sellQuantity - quantity);
                        level.totalQuantity -= quantity;
                        quantity = 0;
                    }

                }
                if(level.orders.empty()){
                    it = currentBook->asks.erase(it);
                }
                else{
                    ++it;
                }
            }
            if(quantity > 0){
                // cancel remaining quantity or handle as per your logic
            }
        }
        else{
            auto it = currentBook->bids.begin();
            while(it != currentBook->bids.end() && quantity > 0){
                auto &level = it->second;
                while(!level.orders.empty() && quantity > 0){
                    auto &buyOrder = level.orders.front();
                    double buyQuantity = stod(buyOrder->quantity);
                    double tradeQuantity = min(buyQuantity, quantity);
                    recordTrades(*buyOrder, parsedOrder, tradeQuantity, it->first, "sell");
                    if(buyQuantity <= quantity){
                        quantity -= buyQuantity;
                        level.totalQuantity -= buyQuantity;
                        level.orders.pop_front();
                    }
                    else{
                        buyOrder->quantity = to_string(buyQuantity - quantity);
                        level.totalQuantity -= quantity;
                        quantity = 0;
                    }

                }
                if(level.orders.empty()){
                    it = currentBook->bids.erase(it);
                }
                else{
                    ++it;
                }
            }
            if(quantity > 0){
                // cancel remaining quantity or handle as per your logic
            }
        }
    }
    else if(parsedOrder.orderType == "IOC"){
        double price = stod(parsedOrder.price);
        double quantity = stod(parsedOrder.quantity);
        if(parsedOrder.side == "buy"){
            auto it = currentBook->asks.begin();
            while(it != currentBook->asks.end() && price >= it->first && quantity > 0){
                auto &level = it->second;
                while(!level.orders.empty() && quantity > 0){
                    auto &sellOrder = level.orders.front();
                    double sellQuantity = stod(sellOrder->quantity);
                    double tradeQuantity = min(sellQuantity, quantity);
                    recordTrades(parsedOrder, *sellOrder, tradeQuantity, it->first, "buy");
                    if(sellQuantity <= quantity){
                        quantity -= sellQuantity;
                        level.totalQuantity -= sellQuantity;
                        level.orders.pop_front();
                    }
                    else{
                        sellOrder->quantity = to_string(sellQuantity - quantity);
                        level.totalQuantity -= quantity;
                        quantity = 0;
                    }

                }
                if(level.orders.empty()){
                    it = currentBook->asks.erase(it);
                }
                else{
                    ++it;
                }
            }
            // Cancel remaining quantity
        }
        else{
            auto it = currentBook->bids.begin();
            while(it != currentBook->bids.end() && price <= it->first && quantity > 0){
                auto &level = it->second;
                while(!level.orders.empty() && quantity > 0){
                    auto &buyOrder = level.orders.front();
                    double buyQuantity = stod(buyOrder->quantity);
                    double tradeQuantity = min(buyQuantity, quantity);
                    recordTrades(*buyOrder, parsedOrder, tradeQuantity, it->first, "sell");
                    if(buyQuantity <= quantity){
                        quantity -= buyQuantity;
                        level.totalQuantity -= buyQuantity;
                        level.orders.pop_front();
                    }
                    else{
                        buyOrder->quantity = to_string(buyQuantity - quantity);
                        level.totalQuantity -= quantity;
                        quantity = 0;
                    }

                }
                if(level.orders.empty()){
                    it = currentBook->bids.erase(it);
                }
                else{
                    ++it;
                }
            }
            // Cancel remaining quantity
        }
    }
    else if(parsedOrder.orderType == "FOK"){
        double price = stod(parsedOrder.price);
        double quantity = stod(parsedOrder.quantity);
        bool canFill = false;
        if(parsedOrder.side == "buy"){
            double availableQuantity = 0.0;
            auto it = currentBook->asks.begin();
            while(it != currentBook->asks.end() && price >= it->first){
                availableQuantity += it->second.totalQuantity;
                if(availableQuantity >= quantity){
                    canFill = true;
                    break;
                }
                ++it;
            }
            if(canFill){
                while(it != currentBook->asks.end() && quantity > 0){
                    auto &level = it->second;
                    while(!level.orders.empty() && quantity > 0){
                        auto &sellOrder = level.orders.front();
                        double sellQuantity = stod(sellOrder->quantity);
                        double tradeQuantity = min(sellQuantity, quantity);
                        recordTrades(parsedOrder, *sellOrder, tradeQuantity, it->first, "buy");
                        if(sellQuantity <= quantity){
                            quantity -= sellQuantity;
                            level.totalQuantity -= sellQuantity;
                            level.orders.pop_front();
                        }
                        else{
                            sellOrder->quantity = to_string(sellQuantity - quantity);
                            level.totalQuantity -= quantity;
                            quantity = 0;
                        }

                    }
                    if(level.orders.empty()){
                        it = currentBook->asks.erase(it);
                    }
                    else{
                        ++it;
                    }
                }
            }
            else{
                // Cancel order
            }
        }
        else{
            double availableQuantity = 0.0;
            auto it = currentBook->bids.begin();
            while(it != currentBook->bids.end() && price <= it->first){
                availableQuantity += it->second.totalQuantity;
                if(availableQuantity >= quantity){
                    canFill = true;
                    break;
                }
                ++it;
            }
            if(canFill){
                while(it != currentBook->bids.end() && quantity > 0){
                    auto &level = it->second;
                    while(!level.orders.empty() && quantity > 0){
                        auto &buyOrder = level.orders.front();
                        double buyQuantity = stod(buyOrder->quantity);
                        double tradeQuantity = min(buyQuantity, quantity);
                        recordTrades(*buyOrder, parsedOrder, tradeQuantity, it->first, "sell");
                        if(buyQuantity <= quantity){
                            quantity -= buyQuantity;
                            level.totalQuantity -= buyQuantity;
                            level.orders.pop_front();
                        }
                        else{
                            buyOrder->quantity = to_string(buyQuantity - quantity);
                            level.totalQuantity -= quantity;
                            quantity = 0;
                        }

                    }
                    if(level.orders.empty()){
                        it = currentBook->bids.erase(it);
                    }
                    else{
                        ++it;
                    }
                }
            }
            else{
                // Cancel order
            }
        }
    }

}

void matching_engine::order(const HttpRequestPtr &req,
                                      std::function<void (const HttpResponsePtr &)> &&callback)
{
    cout<<"Order endpoint called."<<endl;
    auto json = req->getJsonObject();
    if (!json || !json->isMember("symbol") || !json->isMember("order_type") || !json->isMember("side") || !json->isMember("quantity")) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("Missing fields. Required: symbol, order_type, side, quantity");
        callback(resp);
        return;
    }

    Order parsedOrder;
    parsedOrder.orderId = to_string(rand());
    parsedOrder.symbol = (*json)["symbol"].asString();
    parsedOrder.orderType = (*json)["order_type"].asString();
    parsedOrder.side = (*json)["side"].asString();
    parsedOrder.quantity = (*json)["quantity"].asString();
    parsedOrder.price = "";
    if(json->isMember("price")) {
        parsedOrder.price = (*json)["price"].asString();
    }
     
    submitOrder(parsedOrder);  
    Json::Value resp;
    // resp["status"] = "Order received";
    resp["order_id"] = parsedOrder.orderId;
    resp["symbol"] = parsedOrder.symbol;
    resp["order_type"] = parsedOrder.orderType;
    resp["side"] = parsedOrder.side;
    resp["quantity"] = parsedOrder.quantity;
    resp["price"] = parsedOrder.price;
    auto httpResp = HttpResponse::newHttpJsonResponse(resp);
    callback(httpResp);
}


void matching_engine::tradeBook(const drogon::HttpRequestPtr &req,
                                std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                const std::string &symbol)
{
    
    Json::Value jsonArray(Json::arrayValue);
    for(const auto& trade : tradeHistory[symbol]) {
        Json::Value tradeJson;
        tradeJson["trade_id"] = trade.tradeId;
        tradeJson["maker_order_id"] = trade.makerOrderId;
        tradeJson["taker_order_id"] = trade.takerOrderId;
        tradeJson["aggressor"] = trade.aggressor;
        tradeJson["symbol"] = trade.symbol;
        tradeJson["price"] = trade.price;
        tradeJson["quantity"] = trade.quantity;
        tradeJson["timestamp"] = trade.timestamp;
        jsonArray.append(tradeJson);
    }
    auto resp = drogon::HttpResponse::newHttpJsonResponse(jsonArray);
    callback(resp);
}

void matching_engine::orderDepth(const drogon::HttpRequestPtr &req,
                                std::function<void (const drogon::HttpResponsePtr &)> &&callback,
                                const std::string &symbol)
{
    Json::Value resp;
    if(pendingOrders.find(symbol) == pendingOrders.end()){
        resp["bids"] = Json::Value(Json::arrayValue);
        resp["asks"] = Json::Value(Json::arrayValue);
        auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
        callback(httpResp);
        return;
    }
    auto currentBook = pendingOrders[symbol];
    lock_guard<mutex> lock(currentBook->bookMutex);
    Json::Value bidsArray(Json::arrayValue);
    for(const auto& [price, level] : currentBook->bids){
        Json::Value levelJson;
        levelJson["price"] = price;
        levelJson["total_quantity"] = level.totalQuantity;
        bidsArray.append(levelJson);
    }
    Json::Value asksArray(Json::arrayValue);
    for(const auto& [price, level] : currentBook->asks){
        Json::Value levelJson;
        levelJson["price"] = price;
        levelJson["total_quantity"] = level.totalQuantity;
        asksArray.append(levelJson);
    }
    string bestBid = "None";
    string bestAsk = "None";
    if(!currentBook->bids.empty()){
        bestBid = to_string(currentBook->bids.begin()->first);
    }
    if(!currentBook->asks.empty()){
        bestAsk = to_string(currentBook->asks.begin()->first);
    }
    resp["Best Bid"] = bestBid;
    resp["Best Offer"] = bestAsk;
    resp["bids"] = bidsArray;
    resp["asks"] = asksArray;
    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
    callback(httpResp);
}