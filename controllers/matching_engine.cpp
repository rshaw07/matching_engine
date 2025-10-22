#include "matching_engine.h"
#include "webSocket.h"
#include<iostream>  
#include <algorithm>
#include <format>
#include <chrono>
#include <deque>
#include <map>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <ctime>
#include <atomic>



string getTime(){
    using namespace std::chrono;
    auto now = system_clock::now();
    auto utc = floor<microseconds>(now);
    return std::format("{:%FT%T}Z", utc);
}

atomic<uint64_t> matching_engine::orderCounter{0};
atomic<uint64_t> matching_engine::tradeCounter{0};

double makerFeeRate = 0.002; // 0.2%
double takerFeeRate = 0.0035; // 0.35%


void matching_engine::recordTrades(const Order &buyOrder, const Order &sellOrder, double tradeQuantity, double tradePrice, string aggressorSide){
    cout<<"recordTrades called."<<endl;
    double makerFee = tradeQuantity * tradePrice * makerFeeRate;
    double takerFee = tradeQuantity * tradePrice * takerFeeRate;

    Json::Value tradeJson;
    tradeJson["trade_id"] = getNextTradeId();
    tradeJson["maker_order_id"] = (aggressorSide == "buy") ? sellOrder.orderId : buyOrder.orderId;
    tradeJson["taker_order_id"] = (aggressorSide == "buy") ? buyOrder.orderId : sellOrder.orderId;
    tradeJson["aggressor"] = aggressorSide;
    tradeJson["symbol"] = buyOrder.symbol;
    tradeJson["price"] = tradePrice;
    tradeJson["quantity"] = tradeQuantity;
    tradeJson["maker_fee"] = makerFee;
    tradeJson["taker_fee"] = takerFee;
    tradeJson["timestamp"] = getTime();
    Json::StreamWriterBuilder writer;
    string jsonString = Json::writeString(writer, tradeJson);
    webSocket::broadcastTradeUpdate(jsonString, buyOrder.symbol);

}

void matching_engine::updateRecords(shared_ptr<OrderBook> currentBook, const string& symbol){
    Json::Value marketJson;
    marketJson["timestamp"] = getTime();
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
    int counter = 0;
    for(const auto& [price, level] : currentBook->bids){
        if(counter++ >= 10) break;
        Json::Value levelJson;
        levelJson["price"] = price;
        levelJson["total_quantity"] = level.totalQuantity;
        bidsArray.append(levelJson);
    }
    counter = 0;
    Json::Value asksArray(Json::arrayValue);
    for(const auto& [price, level] : currentBook->asks){
        if(counter++ >= 10) break;
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

OrderResult matching_engine::submitOrder(Order parsedOrder){
    OrderResult result;
    result.orderId = parsedOrder.orderId;
    result.executedQuantity = 0.0;
    result.remainingQuantity = parsedOrder.quantity;
    result.averagePrice = 0.0;
    result.status = "open";

    double totalTradedValue = 0.0;
    lock_guard<mutex> engineLock(engineMutex);
    if(pendingOrders.find(parsedOrder.symbol) == pendingOrders.end()){
        pendingOrders[parsedOrder.symbol] = make_shared<OrderBook>();
    }
    auto currentBook = pendingOrders[parsedOrder.symbol];
    lock_guard<mutex> lock(currentBook->bookMutex);
    if(parsedOrder.orderType == "limit"){
        double price = parsedOrder.price;
        double quantity = parsedOrder.quantity;
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
                        double sellQuantity = sellOrder->quantity;
                        double tradeQuantity = min(sellQuantity, quantity);
                        recordTrades(parsedOrder, *sellOrder, tradeQuantity, it->first, "buy");
                        totalTradedValue += tradeQuantity * it->first;
                        if(sellQuantity <= quantity){
                            quantity -= sellQuantity;
                            level.totalQuantity -= sellQuantity;
                            level.orders.pop_front();
                        }
                        else{
                            sellOrder->quantity = sellQuantity - quantity;
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
                result.executedQuantity = parsedOrder.quantity - quantity;
                result.remainingQuantity = quantity;
                if(quantity > 0){
                    parsedOrder.quantity = quantity;
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
                        double buyQuantity = buyOrder->quantity;
                        double tradeQuantity = min(buyQuantity, quantity);
                        recordTrades(*buyOrder, parsedOrder, tradeQuantity, it->first, "sell");
                        totalTradedValue += tradeQuantity * it->first;
                        if(buyQuantity <= quantity){
                            quantity -= buyQuantity;
                            level.totalQuantity -= buyQuantity;
                            level.orders.pop_front();
                        }
                        else{
                            buyOrder->quantity = buyQuantity - quantity;
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
                result.executedQuantity = parsedOrder.quantity - quantity;
                result.remainingQuantity = quantity;
                if(quantity > 0){
                    parsedOrder.quantity = quantity;
                    currentBook->asks[price].orders.push_back(make_shared<Order>(parsedOrder));
                    currentBook->asks[price].totalQuantity += quantity;
                }   
            }
        }
    }
    else if(parsedOrder.orderType == "market"){
        double quantity = parsedOrder.quantity;
        if(parsedOrder.side == "buy"){
            auto it = currentBook->asks.begin();
            while(it != currentBook->asks.end() && quantity > 0){
                auto &level = it->second;
                while(!level.orders.empty() && quantity > 0){
                    auto &sellOrder = level.orders.front();
                    double sellQuantity = sellOrder->quantity;
                    double tradeQuantity = min(sellQuantity, quantity);
                    recordTrades(parsedOrder, *sellOrder, tradeQuantity, it->first, "buy");
                    totalTradedValue += tradeQuantity * it->first;
                    if(sellQuantity <= quantity){
                        quantity -= sellQuantity;
                        level.totalQuantity -= sellQuantity;
                        level.orders.pop_front();
                    }
                    else{
                        sellOrder->quantity = sellQuantity - quantity;
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
            result.executedQuantity = parsedOrder.quantity - quantity;
            result.remainingQuantity = quantity;
        }
        else{
            auto it = currentBook->bids.begin();
            while(it != currentBook->bids.end() && quantity > 0){
                auto &level = it->second;
                while(!level.orders.empty() && quantity > 0){
                    auto &buyOrder = level.orders.front();
                    double buyQuantity = buyOrder->quantity;
                    double tradeQuantity = min(buyQuantity, quantity);
                    recordTrades(*buyOrder, parsedOrder, tradeQuantity, it->first, "sell");
                    totalTradedValue += tradeQuantity * it->first;
                    if(buyQuantity <= quantity){
                        quantity -= buyQuantity;
                        level.totalQuantity -= buyQuantity;
                        level.orders.pop_front();
                    }
                    else{
                        buyOrder->quantity = buyQuantity - quantity;
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
                // cancel remaining quantity
            }
            result.executedQuantity = parsedOrder.quantity - quantity;
            result.remainingQuantity = quantity;
        }
    }
    else if(parsedOrder.orderType == "IOC"){
        double price = parsedOrder.price;
        double quantity = parsedOrder.quantity;
        if(parsedOrder.side == "buy"){
            auto it = currentBook->asks.begin();
            while(it != currentBook->asks.end() && price >= it->first && quantity > 0){
                auto &level = it->second;
                while(!level.orders.empty() && quantity > 0){
                    auto &sellOrder = level.orders.front();
                    double sellQuantity = sellOrder->quantity;
                    double tradeQuantity = min(sellQuantity, quantity);
                    recordTrades(parsedOrder, *sellOrder, tradeQuantity, it->first, "buy");
                    totalTradedValue += tradeQuantity * it->first;
                    if(sellQuantity <= quantity){
                        quantity -= sellQuantity;
                        level.totalQuantity -= sellQuantity;
                        level.orders.pop_front();
                    }
                    else{
                        sellOrder->quantity = sellQuantity - quantity;
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
            result.executedQuantity = parsedOrder.quantity - quantity;
            result.remainingQuantity = quantity;
        }
        else{
            auto it = currentBook->bids.begin();
            while(it != currentBook->bids.end() && price <= it->first && quantity > 0){
                auto &level = it->second;
                while(!level.orders.empty() && quantity > 0){
                    auto &buyOrder = level.orders.front();
                    double buyQuantity = buyOrder->quantity;
                    double tradeQuantity = min(buyQuantity, quantity);
                    recordTrades(*buyOrder, parsedOrder, tradeQuantity, it->first, "sell");
                    totalTradedValue += tradeQuantity * it->first;
                    if(buyQuantity <= quantity){
                        quantity -= buyQuantity;
                        level.totalQuantity -= buyQuantity;
                        level.orders.pop_front();
                    }
                    else{
                        buyOrder->quantity = buyQuantity - quantity;
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
            result.executedQuantity = parsedOrder.quantity - quantity;
            result.remainingQuantity = quantity;
        }
    }
    else if(parsedOrder.orderType == "FOK"){
        double price = parsedOrder.price;
        double quantity = parsedOrder.quantity;
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
                it = currentBook->asks.begin();
                while(it != currentBook->asks.end() && quantity > 0){
                    auto &level = it->second;
                    while(!level.orders.empty() && quantity > 0){
                        auto &sellOrder = level.orders.front();
                        double sellQuantity = sellOrder->quantity;
                        double tradeQuantity = min(sellQuantity, quantity);
                        recordTrades(parsedOrder, *sellOrder, tradeQuantity, it->first, "buy");
                        totalTradedValue += tradeQuantity * it->first;
                        if(sellQuantity <= quantity){
                            quantity -= sellQuantity;
                            level.totalQuantity -= sellQuantity;
                            level.orders.pop_front();
                        }
                        else{
                            sellOrder->quantity = sellQuantity - quantity;
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
            result.remainingQuantity = quantity;
            result.executedQuantity = parsedOrder.quantity - quantity;
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
                it = currentBook->bids.begin();
                while(it != currentBook->bids.end() && quantity > 0){
                    auto &level = it->second;
                    while(!level.orders.empty() && quantity > 0){
                        auto &buyOrder = level.orders.front();
                        double buyQuantity = buyOrder->quantity;
                        double tradeQuantity = min(buyQuantity, quantity);
                        recordTrades(*buyOrder, parsedOrder, tradeQuantity, it->first, "sell");
                        totalTradedValue += tradeQuantity * it->first;
                        if(buyQuantity <= quantity){
                            quantity -= buyQuantity;
                            level.totalQuantity -= buyQuantity;
                            level.orders.pop_front();
                        }
                        else{
                            buyOrder->quantity = buyQuantity - quantity;
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
            result.remainingQuantity = quantity;
            result.executedQuantity = parsedOrder.quantity - quantity;
        }
    }

    if(result.executedQuantity == 0.0){
        if(parsedOrder.orderType == "IOC" || parsedOrder.orderType == "FOK"){
            result.status = "cancelled";
        }
        else{
            result.status = "open";
        }
    } else if(result.remainingQuantity == 0.0){
        result.status = "filled";
    } else{
        result.status = "partially_filled";
    }

    if(result.executedQuantity > 0.0){
        result.averagePrice = totalTradedValue / result.executedQuantity;
    }

    updateRecords(currentBook, parsedOrder.symbol);
    return result;

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
    parsedOrder.orderId = getNextOrderId();
    parsedOrder.symbol = (*json)["symbol"].asString();
    parsedOrder.orderType = (*json)["order_type"].asString();
    parsedOrder.side = (*json)["side"].asString();
    parsedOrder.quantity = (*json)["quantity"].asDouble();
    parsedOrder.price = 0.0;
    parsedOrder.timestamp = getTime();
    if(json->isMember("price")) {
        parsedOrder.price = (*json)["price"].asDouble();
    }

    auto result = submitOrder(parsedOrder);
    Json::Value resp;
    // resp["status"] = "Order received";
    resp["order_id"] = result.orderId;
    resp["symbol"] = parsedOrder.symbol;
    resp["order_type"] = parsedOrder.orderType;
    resp["side"] = parsedOrder.side;
    resp["quantity"] = parsedOrder.quantity;
    resp["price"] = parsedOrder.price;
    resp["status"] = result.status;
    resp["executed_quantity"] = result.executedQuantity;
    resp["remaining_quantity"] = result.remainingQuantity;
    resp["average_price"] = result.averagePrice;
    resp["timestamp"] = parsedOrder.timestamp;
    auto httpResp = HttpResponse::newHttpJsonResponse(resp);
    callback(httpResp);
}