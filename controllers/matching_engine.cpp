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
    // Broadcast trade update via WebSocket
    Json::Value tradeJson;
    tradeJson["trade_id"] = to_string(rand());
    tradeJson["maker_order_id"] = (aggressorSide == "buy") ? sellOrder.orderId : buyOrder.orderId;
    tradeJson["taker_order_id"] = (aggressorSide == "buy") ? buyOrder.orderId : sellOrder.orderId;
    tradeJson["aggressor"] = aggressorSide;
    tradeJson["symbol"] = buyOrder.symbol;
    tradeJson["price"] = to_string(tradePrice);
    tradeJson["quantity"] = to_string(tradeQuantity);
    tradeJson["timestamp"] = to_string(time(nullptr));
    Json::StreamWriterBuilder writer;
    string jsonString = Json::writeString(writer, tradeJson);
    webSocket::broadcastTradeUpdate(jsonString, buyOrder.symbol);

}

void matching_engine::updateRecords(shared_ptr<OrderBook> currentBook, const string& symbol){
    // Broadcast market update via WebSocket
    Json::Value marketJson;
    marketJson["timestamp"] = to_string(time(nullptr));
    marketJson["symbol"] = symbol;
    if(!currentBook->bids.empty()){
        marketJson["best_bid"] = currentBook->bids.begin()->first;
    }
    else{
        marketJson["best_bid"] = "None";
    }
    if(!currentBook->asks.empty()){
        marketJson["best_ask"] = currentBook->asks.begin()->first;
    }
    else{
        marketJson["best_ask"] = "None";
    }
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
    marketJson["bids"] = bidsArray;
    marketJson["asks"] = asksArray;
    Json::StreamWriterBuilder writer;
    string jsonString = Json::writeString(writer, marketJson);
    webSocket::broadcastMarketUpdate(jsonString, symbol);
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
    updateRecords(currentBook, parsedOrder.symbol);

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