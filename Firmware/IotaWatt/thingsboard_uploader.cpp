#include "thingsboard_uploader.h"
#include "splitstr.h"

/*****************************************************************************************
 *          handle_query_s()
 * **************************************************************************************/
uint32_t thingsboard_uploader::handle_query_s(){
     trace(T_thingsboard,20);
    String endpoint = "/api/v1/";
    endpoint += _token;
    endpoint += "/attributes?sharedKeys=lastTelemetry";
    //log("%s: endpoint  %s", _id, endpoint.c_str());
    //log("%s: url  %s", _id, _url->build().c_str());
    _encrypted = false;
    // if (_request){
    //     delete _request;
    //     _request = nullptr;
    // }
    HTTPPGet(endpoint.c_str(), checkQuery_s, "application/json");
    return 1;
}

/*****************************************************************************************
 *          handle_checkQuery_s()
 * **************************************************************************************/
uint32_t thingsboard_uploader::handle_checkQuery_s(){
    trace(T_thingsboard,30);
    if(_request->responseHTTPcode() != 200){
        log("%s: Query failed %d", _id, _request->responseHTTPcode());
        log("%s: Query failed %s", _id, _request->responseText().c_str());
        delay(5, query_s);
        return 15;
    }
    String response = _request->responseText();
    DynamicJsonBuffer Json;
    JsonObject& results = Json.parseObject(response);
    results.printTo(Message_log);
    if(results.success()){
        log("%s: LAST TELEMETRY %s", _id, results["shared"]["lastTelemetry"].as<String>().substring(0,10));
        uint32_t _time = strtoul(results["shared"]["lastTelemetry"].as<String>().substring(0,10).c_str(), NULL, 0);
        _lastSent = MAX(_lastSent, _time);
    } else {
        trace(T_thingsboard,31);
        log("%s: Attribute not found.", _id);
        _lastSent = Current_log.lastKey();
        if(_uploadStartDate){
            _lastSent = _uploadStartDate;
        }
        else {
            _lastSent = Current_log.lastKey();
        }
    }
    //log("%s: LAST SENT %u", _id, _lastSent);


    _lastSent = MAX(_lastSent, Current_log.firstKey());
    _lastSent -= _lastSent % _interval;
    log("%s: Start posting at %s", _id, localDateString(_lastSent + _interval).c_str());
    _state = write_s;
    return 1;
}

/*****************************************************************************************
 *          handle_write_s())
 * **************************************************************************************/
uint32_t thingsboard_uploader::handle_write_s(){
    trace(T_thingsboard,60);
    
    // This is the predominant state of the uploader
    // It will wait until there is enough data for the next post
    // then build the payload and initiate the post operation.

    if(_stop){
        stop();
        return 1;
    }

    // If not enough data to post, set wait and return.

    if(Current_log.lastKey() < (_lastSent + _interval + (_interval * _bulkSend))){
        return UTCtime() + 1;
    }

    // If datalog buffers not allocated, do so now and prime latest.

    trace(T_thingsboard,60);
    if(! oldRecord){
        trace(T_thingsboard,61);
        oldRecord = new IotaLogRecord;
        newRecord = new IotaLogRecord;
        newRecord->UNIXtime = _lastSent + _interval;
        Current_log.readKey(newRecord);
    }

    // Build post transaction from datalog records.

    while(reqData.available() < uploaderBufferLimit && newRecord->UNIXtime < Current_log.lastKey()){

        if(micros() > bingoTime){
            return 10;
        }
        
        // Swap newRecord top oldRecord, read next into newRecord.

        trace(T_thingsboard,60);
        IotaLogRecord *swap = oldRecord;
        oldRecord = newRecord;
        newRecord = swap;
        newRecord->UNIXtime = oldRecord->UNIXtime + _interval;
        Current_log.readKey(newRecord);

        // Compute the time difference between log entries.
        // If zero, don't bother.

        trace(T_thingsboard,62);    
        double elapsedHours = newRecord->logHours - oldRecord->logHours;
        if(elapsedHours == 0){
            trace(T_thingsboard,63);
            if((newRecord->UNIXtime + _interval) <= Current_log.lastKey()){
                return 1;
            }
            return UTCtime() + 1;
        }

        // Build measurements for this interval

        trace(T_thingsboard,62); 
        Script *script = _outputs->first();
        DynamicJsonBuffer Json;
        JsonObject& doc = Json.createObject();
        JsonObject& value = Json.createObject();
        trace(T_thingsboard,63);     
        char ts[10];
        char tsms[13];
        sprintf(tsms, "%u000", oldRecord->UNIXtime);
        //log("%s: Timestamp SECONDS %s \n", _id, tsms);
        doc["ts"] = tsms;
        while(script){
            //log("%s: While %s \n", _id, script->name());
            double value1 = script->run(oldRecord, newRecord);
            if(value1 == value1){
                //log("%s: INI VAL %d \n", _id, value1);
                //doc["ts"]["values"][script->name()] = value1;
                if(script->precision()){
                    char valstr[20];
                    JsonVariant valuefloat;
                    int end = sprintf(valstr,"%.*f", script->precision(), value1);
                    //log("%s: SPRINTF %s \n", _id, valstr);
                    while(valstr[--end] == '0'){
                        valstr[end] = 0;
                    }
                    if(valstr[end] == '.'){
                        valstr[end] = 0;
                    }
                    //log("%s: VALUE %s \n", _id, valstr);
                    //doc["values"][script->name()] = valstr;
                    valuefloat = valstr;
                    value[script->name()] = valuefloat.as<double>();
                }
                else {
                    char valstr[20];
                    JsonVariant valuefloat;
                    valuefloat = sprintf(valstr,"%.*f", script->precision(), value1);
                    value[script->name()] = valuefloat.as<double>();
                }
            } else {
                value[script->name()] = 0;
            }
            script = script->next();
            doc["values"] = value;
            //doc.prettyPrintTo(Message_log);
        }
        _lastPost = oldRecord->UNIXtime;
        //Message_log.println(reqData.peekString(reqData.available()));
        doc.printTo(reqData);
        //Message_log.println(reqData.peekString(reqData.available()));
        break;
    }

    _lastPost = oldRecord->UNIXtime;
    

    // Free the buffers

    delete oldRecord;
    oldRecord = nullptr;
    delete newRecord;
    newRecord = nullptr;

    // if not encrypted protocol, send plaintext payload.

    if(!_encrypt){
        _encrypted = false;
        String endpoint = "/api/v1/";
        endpoint += _token;
        endpoint += "/telemetry";
        //log("%s: endpoint  %s", _id, endpoint.c_str());
        //log("%s: url  %s", _id, _url->build().c_str());
        _encrypted = false;
        // if (_request){
        //     delete _request;
        //     _request = nullptr;
        // }
        HTTPPost(endpoint.c_str(), checkWrite_s, "application/json");
        return 1;
    }

    // // Encrypted protocol, encrypt the payload

    // trace(T_thingsboard,70);  
    
    // uint8_t iv[16];
    // for (int i = 0; i < 16; i++){
    //     iv[i] = random(256);
    // }

    //     // Initialize sha256, shaHMAC and cypher

    // trace(T_thingsboard, 70);  
    // SHA256* sha256 = new SHA256;
    // sha256->reset();
    // SHA256* shaHMAC = new SHA256;
    // shaHMAC->resetHMAC(_cryptoKey,16);
    // CBC<AES128>* cypher = new CBC<AES128>;
    // cypher->setIV(iv, 16);
    // cypher->setKey(_cryptoKey, 16);

    // // Process payload while
    // // updating SHAs and encrypting. 

    // trace(T_thingsboard,70);     
    // uint8_t* temp = new uint8_t[64+16];
    // size_t supply = reqData.available();
    // reqData.write(iv, 16);
    // while(supply){
    //     size_t len = supply < 64 ? supply : 64;
    //     reqData.read(temp, len);
    //     supply -= len;
    //     sha256->update(temp, len);
    //     shaHMAC->update(temp, len);
    //     if(len < 64 || supply == 0){
    //         size_t padlen = 16 - (len % 16);
    //         for(int i=0; i<padlen; i++){
    //             temp[len+i] = padlen;
    //         }
    //         len += padlen;
    //     }
    //     cypher->encrypt(temp, temp, len);
    //     reqData.write(temp, len);
    // }
    // trace(T_thingsboard,70);
    // delete[] temp;
    // delete cypher;
    
    // // finalize the Sha256 and shaHMAC

    // trace(T_thingsboard,71); 
    // sha256->finalize(_sha256, 32);
    // delete[] _base64Sha;
    // _base64Sha = charstar(base64encode(_sha256, 32).c_str());
    // shaHMAC->finalizeHMAC(_cryptoKey, 16, _sha256, 32);
    // delete sha256;
    // delete shaHMAC;

    // // Now base64 encode and send

    // trace(T_thingsboard,71); 
    // base64encode(&reqData); 
    // trace(T_thingsboard,71);
    // _encrypted = true;
    // HTTPPost("/input/bulk", checkWrite_s, "aes128cbc");
    return 1;
}

/*****************************************************************************************
 *          handle_checkWrite_s()
 * **************************************************************************************/
uint32_t thingsboard_uploader::handle_checkWrite_s(){
    trace(T_thingsboard,91);

    // Check the result of a write transaction.
    // Usually success (204) just note the lastSent and
    // return to buildPost.

    if(_request->responseHTTPcode() == 200){
        delete[] _statusMessage;
        _statusMessage = nullptr;
        String response = _request->responseText();
        if((!_encrypted ) || (_encrypted && response.startsWith(_base64Sha))){
            //log("%s: CHECK WRITE EXITOSO", _id);
            _lastSent = _lastPost; 
            _state = write_s;
            trace(T_thingsboard,93);
            return 1;
        }
        Serial.println(response.substring(0, 80));
        String msg = "Invalid response: " + response.substring(0, 80);
        _statusMessage = charstar(msg.c_str());
    }
    
    // Deal with failure.

    else {
        trace(T_thingsboard,92);
        char msg[100];
        sprintf_P(msg, PSTR("Post failed %d"), _request->responseHTTPcode());
        delete[] _statusMessage;
        _statusMessage = charstar(msg);
        delete _request;
        _request = nullptr;
    }

    // Try it again in awhile;

    delay(10, write_s);
    return 1;
}

void thingsboard_uploader::HTTPPost(const char* endpoint, states completionState, const char* contentType){
    
    // Build a request control block for this request,
    // set state to handle the request and return to caller.
    // Actual post is done in next tick handler.
    
    if( ! _POSTrequest){
        _POSTrequest = new POSTrequest;
    }
    delete _POSTrequest->endpoint;
    _POSTrequest->endpoint = charstar(endpoint);
    delete _POSTrequest->contentType;
    _POSTrequest->contentType = charstar(contentType);
    _POSTrequest->completionState = completionState;
    _state = HTTPpost_s;
    _POSTrequest->method = "POST";
}

void thingsboard_uploader::HTTPPGet(const char* endpoint, states completionState, const char* contentType){
    
    // Build a request control block for this request,
    // set state to handle the request and return to caller.
    // Actual post is done in next tick handler.

    // if(_request->readyState() == 0 || _request->readyState() == 4){
    //     log("%s: READY STATE IN  %d", _id, _request->readyState());
    //     if(!_request->open("GET", "http://digital.fulmen.mx/api/v1/CcwBooj6BCB48l72HCf9/attributes?sharedKeys=lastTelemetry")){
    //         log("%s: OPEN FAIL", _id);
    //     }
    //     if(!_request->send()){
    //         log("%s: SEND FAIL", _id);
    //     }
    //     _state = HTTPpost_s;
    // } else {
    //     log("%s: READY STATE ELSE  %d", _id, _request->readyState());
    // }
    
    
    if( ! _POSTrequest){
        _POSTrequest = new POSTrequest;
    }
    delete _POSTrequest->endpoint;
    _POSTrequest->endpoint = charstar(endpoint);
    delete _POSTrequest->contentType;
    _POSTrequest->contentType = charstar(contentType);
    _POSTrequest->completionState = completionState;
    _state = HTTPpost_s;
    _POSTrequest->method = "GET";
}

uint32_t thingsboard_uploader::handle_HTTPpost_s(){

    // Initiate the post request.
    // If WiFi not connected or can't get semaphore
    // just return.

    trace(T_uploader,120);
    if( ! WiFi.isConnected()){
        return UTCtime() + 1;
    }

    _HTTPtoken = HTTPreserve(T_uploader);
    if( ! _HTTPtoken){
        return 15;
    }

    // Setup request.
    if( ! _request){
        _request = new asyncHTTPrequest;
    }
    _request->setTimeout(3);
    _request->setDebug(false);
    if(_request->debug())    {
        Serial.println(ESP.getFreeHeap()); 
        Serial.println(datef(localTime(),"hh:mm:ss"));
        Serial.println(reqData.peekString(reqData.available()));
    }
    trace(T_uploader,120);
    char URL[128];
    if(_useProxyServer){
        trace(T_uploader,121);
        size_t len = sprintf_P(URL, PSTR("%s%s"), HTTPSproxy, _POSTrequest->endpoint);
    }
    else {
        trace(T_uploader,122);
        size_t len = sprintf_P(URL, PSTR("%s%s"),  _url->build().c_str(), _POSTrequest->endpoint);
    }
    trace(T_uploader,123);
    log("%s: UPLOADERCPP  %s /// %s", _id, _POSTrequest->method, URL);
    if( ! _request->open(_POSTrequest->method, URL)){
        trace(T_uploader,123);
        HTTPrelease(_HTTPtoken);
        delete _request;
        _request = nullptr;
        return UTCtime() + 5;
    }

    if(_useProxyServer){
        _request->setReqHeader(F("X-proxypass"),  _url->build().c_str());
    }
    if(_POSTrequest->method == "POST"){
        _request->setReqHeader(F("Content-type"), _POSTrequest->contentType);
        trace(T_uploader,124);
        setRequestHeaders();
        //log("%s: THINGS POST  %s -1-- %s", _id, _request->headers().c_str(), reqData.peekString(reqData.available()).c_str());
        if(! _request->send(&reqData, reqData.available())){
            trace(T_uploader,125);
            HTTPrelease(_HTTPtoken);
            reqData.flush();
            delete _request;
            _request = nullptr;
            _lastPost = _lastSent;
            return UTCtime() + 5;
        }
    }else{
        //log("%s: GET  %s --- %s", _id, _request->headers().c_str(), reqData.peekString(reqData.available()));
        if( ! _request->send()){
            trace(T_uploader,125);
            HTTPrelease(_HTTPtoken);
            reqData.flush();
            delete _request;
            _request = nullptr;
            _lastPost = _lastSent;
            return UTCtime() + 5;
        }
    }
    reqData.flush();
    trace(T_uploader,126);
    _state = HTTPwait_s;
    return 10; 
}

uint32_t thingsboard_uploader::handle_HTTPwait_s(){
    trace(T_uploader,90);
    if(_request && _request->readyState() == 4){
        HTTPrelease(_HTTPtoken);
        trace(T_uploader,91);
        delete[] _statusMessage;
        _statusMessage = nullptr;
        _state = _POSTrequest->completionState;
        delete _POSTrequest;
        _POSTrequest = nullptr;
        trace(T_uploader,9);
        return 1;
    }
    trace(T_uploader,93);
    return 10;
}

/*****************************************************************************************
 *          setRequestHeaders()
 * **************************************************************************************/
void thingsboard_uploader::setRequestHeaders(){
    trace(T_thingsboard,95);
    // if(_encrypted){
    //     String auth(_userID);
    //     auth += ':' + bin2hex(_sha256, 32);
    //     _request->setReqHeader("Authorization", auth.c_str());
    // }
    // else {
    //     String auth("Bearer ");
    //     auth += bin2hex(_cryptoKey, 16).c_str();
    //     _request->setReqHeader("Authorization", auth.c_str());
    // }
    _request->setDebug(false);
    //log("%s: HEADER  %s", _id, _request->headers().c_str());
    
    trace(T_thingsboard,95);
}

//********************************************************************************************************************
//
//               CCC     OOO    N   N   FFFFF   III    GGG    CCC   BBBB
//              C   C   O   O   NN  N   F        I    G      C   C  B   B
//              C       O   O   N N N   FFF      I    G  GG  C      BBBB
//              C   C   O   O   N  NN   F        I    G   G  C   C  B   B
//               CCC     OOO    N   N   F       III    GGG    CCC   B BBB
//
//********************************************************************************************************************
bool thingsboard_uploader::configCB(JsonObject& config){
    
    trace(T_thingsboard,100);
    const char *accessToken = config["accessToken"].as<char *>();
    if(!accessToken){
        log("%s: accessToken not specified.", _id);
        return false;
    }

    trace(T_thingsboard,101);
    delete[] _token;
    _token = charstar(config.get<const char *>(F("accessToken")));
    if (!_token || strlen(_token) != 20)
    {
        log("%s: Invalid access token", _id);
        return false;
    }
    // Script* script = _outputs->first();
    // int index = 0;
    // while(script){
    //     if(String(script->name()).toInt() <= index){
    //         log("%s: output sequence error, stopping.");
    //         return false;
    //     }
    //     else {
    //         index = String(script->name()).toInt();
    //     }
    //     script = script->next();
    // }
    trace(T_thingsboard,102);
    return true; 
}
