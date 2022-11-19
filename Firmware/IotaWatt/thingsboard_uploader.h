#ifndef thingsboard_uploader_h
#define thingsboard_uploader_h

#include "IotaWatt.h"

extern uint32_t thingsboard_dispatch(struct serviceBlock *serviceBlock);

class thingsboard_uploader : public uploader 
{
    public:
        thingsboard_uploader() :_token(0),
                            _userID(0),
                            _sha256(0),
                            _base64Sha(0),
                            _revision(0),
                            _encrypt(false),
                            _encrypted(false)
        {
            _id = charstar("thingsboard");
        };

        ~thingsboard_uploader(){
            delete _outputs;
            thingsboard = nullptr;
        };

        bool configCB(const char *JsonText);
        uint32_t dispatch(struct serviceBlock *serviceBlock);

    protected:
        char *_token;
        char *_userID;
        uint8_t *_sha256;
        char *_base64Sha;
        uint8_t _cryptoKey[16];
        int _revision;
        char *_measurement;
        char *_method;
        bool _encrypt;
        bool _encrypted;

        void     queryLast();
        uint32_t handle_query_s();
        uint32_t handle_checkQuery_s();
        uint32_t handle_write_s();
        uint32_t handle_checkWrite_s();
        bool     configCB(JsonObject &);

        uint32_t handle_HTTPwait_s();
        uint32_t handle_HTTPpost_s();
        void HTTPPGet(const char* endpoint, states completionState, const char* contentType);
        void HTTPPost(const char* endpoint, states completionState, const char* contentType);

        void     setRequestHeaders();
        int      scriptCompare(Script *a, Script *b);
};

#endif