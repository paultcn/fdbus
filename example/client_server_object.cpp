/*
 * Copyright (C) 2015   Jeremy Chen jeremy_cz@yahoo.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <vector>
#define FDB_LOG_TAG "FDBTEST"
#include <common_base/fdbus.h>
#include FDB_IDL_EXAMPLE_H
#include "CFdbProtoMsgBuilder.h"

#define OBJ_FROM_SERVER_TO_CLIENT 1

/*
 * 该文件由描述文件workspace/pb_idl/common.base.Example.proto自动生成。
 * 项目所有的接口描述文件都统一放在该目录下，在使用时包含进来。
 */

#if 1
CBaseNotificationCenter<void *> nc;
CBaseNotification<void *> nf;

class ntf_test
{
public:
    void process(CMethodNotification<void *, ntf_test> *ntf, void * &data)
    {
    }
};

ntf_test aaa;
CMethodNotification<void *, ntf_test> m_ntf(&aaa, &ntf_test::process);
CGenericMethodNotification<ntf_test> m_ntf1(&aaa, &ntf_test::process);
#endif

#if __WIN32__
// Need to link with Ws2_32.lib
#pragma comment(lib, "ws2_32.lib")
#endif

static CBaseWorker main_worker;
static CBaseWorker mediaplayer_worker("mediaplayer_worker");

enum EMessageId
{
    REQ_METADATA,
    REQ_RAWDATA,
    REQ_CREATE_MEDIAPLAYER,
    NTF_ELAPSE_TIME,
    NTF_MEDIAPLAYER_CREATED,
    NTF_MANUAL_UPDATE
};

class CMyMessage : public CBaseMessage 
{
public:
    CMyMessage(FdbMsgCode_t code = FDB_INVALID_ID)
        : CBaseMessage(code)
    {
        memset(my_buf, 0, sizeof(my_buf));
    }
private:
    uint8_t my_buf[1024 * 1024];
};

void printMetadata(FdbObjectId_t obj_id, const CFdbMsgMetadata &metadata)
{
    uint64_t time_c2s;
    uint64_t time_s2r;
    uint64_t time_r2c;
    uint64_t time_total;
    CFdbMessage::parseTimestamp(metadata, time_c2s, time_s2r, time_r2c, time_total);
    FDB_LOG_I("OBJ %d, client->server: %" PRIu64 ", arrive->reply: %" PRIu64 ", reply->receive: %" PRIu64 ", total: %" PRIu64 "\n",
                obj_id, time_c2s, time_s2r, time_r2c, time_total);
}

template <typename T>
class CMyServer;
static std::vector<CMyServer<CFdbBaseObject> *> my_server_objects;
template <typename T>
class CBroadcastTimer : public CMethodLoopTimer<CMyServer<T> > 
{
public:
    CBroadcastTimer(CMyServer<T> *server)
        : CMethodLoopTimer<CMyServer<T> >(1500, true, server, &CMyServer<T>::broadcastElapseTime)
    {}
};

template <class T>
class CMyServer : public T
{
public:
    CMyServer(const char *name, CBaseWorker *worker = 0)
        : T(name, worker)
    {
        mTimer = new CBroadcastTimer<T>(this);
        mTimer->attach(&main_worker, false);
    }

    void broadcastElapseTime(CMethodLoopTimer<CMyServer<T> > *timer)
    {
        NFdbExample::ElapseTime et;
        et.set_hour(1);
        et.set_minute(10);
        et.set_second(35);
        CFdbProtoMsgBuilder builder(et);
        this->broadcast(NTF_ELAPSE_TIME, builder, "my_filter");
        char raw_data[1920];
        memset(raw_data, '=', sizeof(raw_data));
        raw_data[1919] = '\0';
        this->broadcast(NTF_ELAPSE_TIME, "raw_buffer", raw_data, 1920);
        this->broadcast(NTF_ELAPSE_TIME);
    }
    
protected:
    void onOnline(FdbSessionId_t sid, bool is_first)
    {
        FDB_LOG_I("OBJ %d server session up: %d\n", this->objId(), sid);
        if (is_first)
        {
            FDB_LOG_I("timer enabled\n");
            if (!this->isPrimary())
            {
                mTimer->enable();
            }
        }
    }

    void onOffline(FdbSessionId_t sid, bool is_last)
    {
        FDB_LOG_I("OBJ %d server session shutdown: %d\n", this->objId(), sid);
        if (is_last)
        {
            mTimer->disable();
        }
    }

    void onInvoke(CBaseJob::Ptr &msg_ref)
    {
        auto *msg = castToMessage<CBaseMessage *>(msg_ref);
        static int32_t elapse_time = 0;
        switch (msg->code())
        {
            case REQ_METADATA:
            {
                NFdbExample::SongId song_id;
                CFdbProtoMsgParser parser(song_id);
                if (!msg->deserialize(parser))
                {
                    msg->status(msg_ref, NFdbBase::FDB_ST_MSG_DECODE_FAIL, "Fail to decode request!");
                    return;
                }

                FDB_LOG_I("OBJ %d requested song id is: %d\n", this->objId(), song_id.id());
                NFdbExample::NowPlayingDetails rep_msg;
#if 0
                rep_msg.set_artist("刘德华");
                rep_msg.set_album("十年华语金曲");
                rep_msg.set_genre("乡村");
                rep_msg.set_title("忘情水");
#else
                rep_msg.set_artist("Liu Dehua");
                rep_msg.set_album("Ten Year's Golden Song");
                rep_msg.set_genre("County");
                rep_msg.set_title("Wang Qing Shui");
#endif
                rep_msg.set_file_name("Lau Dewa");
                rep_msg.set_elapse_time(elapse_time++);
                CFdbProtoMsgBuilder builder(rep_msg);
                msg->reply(msg_ref, builder);
            }
            break;
            case REQ_RAWDATA:
            {
                // const char *buffer = (char *)msg->getPayloadBuffer();
                int32_t size = msg->getPayloadSize();
                FDB_LOG_I("OBJ %d Invoke of raw buffer is received: size: %d\n", this->objId(), size);
                msg->status(msg_ref, NFdbBase::FDB_ST_OK, "REQ_RAWDATA is processed successfully!");
            }
            break;
            case REQ_CREATE_MEDIAPLAYER:
            {
                #if 0
                for (int32_t i = 0; i < 5; ++i)
                {
                    CMyServer<CFdbBaseObject> *obj = new CMyServer<CFdbBaseObject>("mediaplayer", &mediaplayer_worker);
                    FdbObjectId_t obj_id = obj->bind(dynamic_cast<CBaseEndpoint *>(this));
                    NFdbExample::FdbMsgObjectInfo obj_info;
                    obj_info.set_obj_id(obj_id);
                    this->broadcast(NTF_MEDIAPLAYER_CREATED, obj_info);
                    my_server_objects.push_back(obj);
                }
                #endif
            }
            break;
            default:
            break;
        }
    }

    void onSubscribe(CBaseJob::Ptr &msg_ref)
    {
        auto *msg = castToMessage<CFdbMessage *>(msg_ref);
        const CFdbMsgSubscribeItem *sub_item;
        FDB_BEGIN_FOREACH_SIGNAL(msg, sub_item)
        {
            auto msg_code = sub_item->msg_code();
            const char *filter = "";
            if (sub_item->has_filter())
            {
                filter = sub_item->filter().c_str();
            }
            FdbSessionId_t sid = msg->session();

            FDB_LOG_I("OBJ %d message %d, filter %s of session %d is registered!\n", this->objId(), msg_code, filter, sid);

            switch (msg_code)
            {
                case NTF_ELAPSE_TIME:
                {
                    // Warning! filter might be NULL!
                    std::string str_filter(filter);
                    if (!str_filter.compare("my_filter"))
                    {
                        NFdbExample::ElapseTime et;
                        et.set_hour(1);
                        et.set_minute(10);
                        et.set_second(35);
                        CFdbProtoMsgBuilder builder(et);
                        msg->broadcast(msg_code, builder, filter);
                    }
                    else if (!str_filter.compare("raw_buffer"))
                    {
                        std::string raw_data = "raw buffer test for broadcast.";
                        msg->broadcast(NTF_ELAPSE_TIME, "raw_buffer", raw_data.c_str(), raw_data.length() + 1);
                    }
                }
                break;
                case NTF_MANUAL_UPDATE:
                {
                    msg->broadcast(NTF_MANUAL_UPDATE);
                }
                default:
                break;
            }

        }
        FDB_END_FOREACH_SIGNAL()
    }

    void onCreateObject(CBaseEndpoint *endpoint, CFdbMessage *msg)
    {
        FDB_LOG_I("Create object %d because message %d is received.\n", msg->objectId(), msg->code());
        auto *obj = new CMyServer<CFdbBaseObject>("mediaplayer", &mediaplayer_worker);
        obj->bind(dynamic_cast<CBaseEndpoint *>(this), msg->objectId());
    }
private:
    CBroadcastTimer<T> *mTimer;
};

template <typename T>
class CMyClient;
static std::vector<CMyClient<CFdbBaseObject> *> my_client_objects;
template <typename T>
class CInvokeTimer : public CMethodLoopTimer<CMyClient<T> >
{
public:
    CInvokeTimer(CMyClient<T> *client)
        : CMethodLoopTimer<CMyClient<T> >(1000, true, client, &CMyClient<T>::callServer)
    {}
};

template <typename T>
class CMyClient : public T
{
public:
    CMyClient(const char *name, CBaseWorker *worker = 0)
        : T(name, worker)
    {
        mTimer = new CInvokeTimer<T>(this);
        mTimer->attach(&main_worker, false);
    }

    void callServer(CMethodLoopTimer<CMyClient<T> > *timer)
    {
        NFdbExample::SongId song_id;
        song_id.set_id(1234);
        CBaseJob::Ptr ref(new CMyMessage(REQ_METADATA));
        CFdbProtoMsgBuilder builder(song_id);
        this->invoke(ref, builder);
        auto *msg = castToMessage<CMyMessage *>(ref);
        CFdbMsgMetadata md;
        msg->metadata(md);
        printMetadata(this->objId(), md);
        /*
         * 由于是同步调用，msg->invoke()会一直阻塞直到server调用reply()。
         * 接下来就可以处理server reply回来的数据了。
         *
         * 首先要看一下是正常的数据返回还是由于异常或其它原因造成的状态
         * 返回。server可能通过reply()返回正常数据，还可以通过status()返回
         * 失败。如果不是正常数据返回，需要在此处理。
         */
        if (msg->isStatus())
        {
            /*
             * 状态返回统一的格式：错误码+原因。这和linux命令返回是一致的
             */
            int32_t id;
            std::string reason;
            if (!msg->decodeStatus(id, reason))
            {
                FDB_LOG_E("onReply: fail to decode status!\n");
                return;
            }
            if (msg->isError())
            {
                FDB_LOG_I("sync reply: status is received: msg code: %d, id: %d, reason: %s\n", msg->code(), id, reason.c_str());
            }
            return;
        }
        /*
         * OK，现在可以处理调用的返回值了。首先你是知道返回的数据类型是
         * NFdbExample::NowPlayingDetails。这是个protocol buffer格式。
         * msg->deserialize()将收到的串行数据解析成protocol buffer结构，
         * 然后就可以一个一个域处理了。
         * 如果格式不匹配会返回false，就要检查一下是什么原因了。
         */
        NFdbExample::NowPlayingDetails now_playing;
        CFdbProtoMsgParser parser(now_playing);
        if (msg->deserialize(parser))
        {
            const char *artist = now_playing.artist().c_str();
            const char *album = now_playing.album().c_str();
            const char *genre = now_playing.genre().c_str();
            const char *title = now_playing.title().c_str();
            const char *file_name = "";
            if (now_playing.has_file_name())
            {
                file_name = now_playing.file_name().c_str();
            }
            const char *folder_name = "";
            if (now_playing.has_folder_name())
            {
                folder_name = now_playing.folder_name().c_str();
            }
            int32_t elapse_time = now_playing.elapse_time();
            FDB_LOG_I("OBJ %d, artist: %s, album: %s, genre: %s, title: %s, file name: %s, folder name: %s, elapse time: %d\n",
                        this->objId(), artist, album, genre, title, file_name, folder_name, elapse_time);
        }
        else
        {
            FDB_LOG_I("OBJ %d Error! Unable to decode message!!!\n", this->objId());
        }

        std::string raw_buffer("raw buffer test for invoke()!");
        this->invoke(REQ_RAWDATA, raw_buffer.c_str(), raw_buffer.length() + 1);

        /*
         * trigger update manually; onBroadcast() will be called followed by
         * onStatus().
         */
        CFdbMsgTriggerList update_list;
        this->addManualTrigger(update_list, NTF_MANUAL_UPDATE);
        this->update(update_list);
    }

protected:
    void onOnline(FdbSessionId_t sid, bool is_first)
    {
        FDB_LOG_I("OBJ %d client session online: %d\n", this->objId(), sid);
        if (!this->isPrimary())
        {
            this->setDefaultSession(sid);
        }
        if (is_first)
        {
            CFdbMsgSubscribeList subscribe_list;
            this->addNotifyItem(subscribe_list, NTF_ELAPSE_TIME, "my_filter");
            this->addNotifyItem(subscribe_list, NTF_ELAPSE_TIME, "raw_buffer");
            this->addNotifyItem(subscribe_list, NTF_ELAPSE_TIME);
            this->addNotifyItem(subscribe_list, NTF_MEDIAPLAYER_CREATED);
            /*
             * register NTF_MANUAL_UPDATE for manual update: it will not
             * update unless update() is called
             */
	    this->addUpdateItem(subscribe_list, NTF_MANUAL_UPDATE);
            this->subscribe(subscribe_list);

            if (this->isPrimary())
            {
#if 0
                this->invoke(REQ_CREATE_MEDIAPLAYER);
                CMyClient<CFdbBaseObject> *obj = new CMyClient<CFdbBaseObject>("mediaplayer1", &mediaplayer_worker);
                obj->connect(dynamic_cast<CBaseEndpoint *>(this), 1);

                obj = new CMyClient<CFdbBaseObject>("mediaplayer1", &mediaplayer_worker);
                obj->connect(dynamic_cast<CBaseEndpoint *>(this), 2);
#endif
            }
            else
            {
                mTimer->enable();
            }
        }
    }

    void onOffline(FdbSessionId_t sid, bool is_last)
    {
        FDB_LOG_I("OBJ %d client session shutdown: %d\n", this->objId(), sid);
        if (is_last)
        {
            mTimer->disable();
        }
    }

    void onBroadcast(CBaseJob::Ptr &msg_ref)
    {
        auto *msg = castToMessage<CBaseMessage *>(msg_ref);
        FDB_LOG_I("OBJ %d Broadcast is received: %d; filter: %s\n", this->objId(), msg->code(), msg->getFilter());
        //CFdbMsgMetadata md;
        //msg->metadata(md);
        //printMetadata(md);
        /*
         * 接下来就要case by case地处理收到的消息广播。msg->code()表示消息对应的码。
         *
         * 为了让代码结构更加美观及高效，可以使用类CFdbMessageHandle
         * (在CBaseEndpoint.h)中。具体实现可以参考name_server/CNameServer.cpp。
         */
        switch (msg->code())
        {
            case NTF_ELAPSE_TIME:
            {
                /*
                 * 双方约定NTF_ELAPSE_TIME对应的类型是protocol buffer结构NFdbExample::ElapseTime
                 */
                std::string filter(msg->getFilter());
                if (!filter.compare("my_filter"))
                {
                    NFdbExample::ElapseTime et;
                    CFdbProtoMsgParser parser(et);
                    if (msg->deserialize(parser))
                    {
                        FDB_LOG_I("OBJ %d elapse time is received: hour: %d, minute: %d, second: %d\n",
                                    this->objId(), et.hour(), et.minute(), et.second());
                    }
                    else
                    {
                        FDB_LOG_E("Unable to decode NFdbExample::ElapseTime!\n");
                    }
                }
                else if (!filter.compare("raw_buffer"))
                {
                    // const char *buffer = (char *)msg->getPayloadBuffer();
                    int32_t size = msg->getPayloadSize();
                    //std::cout << "OBJ " << this->objId() << " Broadcast of raw buffer is received: size: " << size << ", data: " << buffer << std::endl;
                    FDB_LOG_I("OBJ %d Broadcast of raw buffer is received: size: %d\n", this->objId(), size);
                }
            }
            break;
            case NTF_MEDIAPLAYER_CREATED:
            {
                NFdbExample::FdbMsgObjectInfo obj_info;
                CFdbProtoMsgParser parser(obj_info);
                msg->deserialize(parser);
                auto *obj = new CMyClient<CFdbBaseObject>("mediaplayer", &mediaplayer_worker);
                obj->connect(dynamic_cast<CBaseEndpoint *>(this), obj_info.obj_id());
                my_client_objects.push_back(obj);
            }
            break;
            case NTF_MANUAL_UPDATE:
            {
                FDB_LOG_I("Manual update is received!\n");
            }
            default:
            break;
        }
    }

    void onInvoke(CBaseJob::Ptr &msg_ref)
    {
    }

    void onReply(CBaseJob::Ptr &msg_ref)
    {
        auto *msg = castToMessage<CBaseMessage *>(msg_ref);
        FDB_LOG_I("OBJ %d response is receieved. sn: %d\n", this->objId(), msg->sn());
        CFdbMsgMetadata md;
        msg->metadata(md);
        printMetadata(this->objId(), md);

        switch (msg->code())
        {
            case REQ_METADATA:
            {
                if (msg->isStatus())
                {
                    if (msg->isError())
                    {
                        int32_t error_code;
                        std::string reason;
                        if (!msg->decodeStatus(error_code, reason))
                        {
                            FDB_LOG_E("onReply: fail to decode status!\n");
                            return;
                        }
                        FDB_LOG_I("onReply(): status is received: msg code: %d, error_code: %d, reason: %s\n",
                              msg->code(), error_code, reason.c_str());
                    }
                    return;
                }
                NFdbExample::NowPlayingDetails now_playing;
                CFdbProtoMsgParser parser(now_playing);
                if (msg->deserialize(parser))
                {
                    const char *artist = now_playing.artist().c_str();
                    const char *album = now_playing.album().c_str();
                    const char *genre = now_playing.genre().c_str();
                    const char *title = now_playing.title().c_str();
                    const char *file_name = "";
                    if (now_playing.has_file_name())
                    {
                        file_name = now_playing.file_name().c_str();
                    }
                    const char *folder_name = "";
                    if (now_playing.has_folder_name())
                    {
                        folder_name = now_playing.folder_name().c_str();
                    }
                    int32_t elapse_time = now_playing.elapse_time();
                    FDB_LOG_I("OBJ %d artist: %s, album: %s, genre: %s, title: %s, file name: %s, folder name: %s, elapse time: %d\n",
                                this->objId(), artist, album, genre, title, file_name, folder_name, elapse_time);
                }
                else
                {
                    FDB_LOG_I("OBJ %d Error! Unable to decode message!!!\n", this->objId());
                }
            }
            break;
            case REQ_RAWDATA:
            {
                if (msg->isStatus())
                {
                    int32_t error_code;
                    std::string reason;
                    if (!msg->decodeStatus(error_code, reason))
                    {
                        FDB_LOG_E("onReply: fail to decode status!\n");
                        return;
                    }
                    FDB_LOG_I("onReply(): status is received: msg code: %d, error_code: %d, reason: %s\n",
                          msg->code(), error_code, reason.c_str());
                    return;
                }
            }
            break;
            default:
            break;
        }
    }

    void onStatus(CBaseJob::Ptr &msg_ref
                        , int32_t error_code
                        , const char *description)
    {
        CBaseMessage *msg = castToMessage<CBaseMessage *>(msg_ref);
        if (msg->isSubscribe())
        {
            if (msg->isError())
            {
            }
            else
            {
                FDB_LOG_I("OBJ %d subscribe is ok! sn: %d is received.\n", this->objId(), msg->sn());
            }
        }
        FDB_LOG_I("OBJ %d Reason: %s\n", this->objId(), description);
    }
    void onCreateObject(CBaseEndpoint *endpoint, CFdbMessage *msg)
    {
        FDB_LOG_I("Create object %d because message %d is received.\n", msg->objectId(), msg->code());
        CMyServer<CFdbBaseObject> *obj = new CMyServer<CFdbBaseObject>("mediaplayer", &mediaplayer_worker);
        obj->bind(dynamic_cast<CBaseEndpoint *>(this), msg->objectId());
    }
protected:
    CInvokeTimer<T> *mTimer;
};

class CObjTestTimer : public CBaseLoopTimer
{
public:
    CObjTestTimer()
        : CBaseLoopTimer(1000, true)
        , mCount(0)
    {}
protected:
    void run()
    {
        mCount++;
        if (mCount%2 == 0){
            worker()->name("");
        }else{
            const char* nullp = 0;
            worker()->name(nullp);
        }
        
    #if 0
        for (std::vector<CMyClient<CFdbBaseObject> *>::iterator it = my_client_objects.begin();
                it != my_client_objects.end(); ++it)
        {
            CMyClient<CFdbBaseObject> *obj = *it;
            obj->disconnect();
        }
        for (std::vector<CMyServer<CFdbBaseObject> *>::iterator it = my_server_objects.begin();
                it != my_server_objects.end(); ++it)
        {
            CMyServer<CFdbBaseObject> *obj = *it;
            obj->unbind();
        }
    #endif
    }
    int mCount;
};

static void printUsage(void)
{
    std::cout << "ERROR:" << std::endl;
    std::cout << "argv 1:  1 - server; 0 - client" << std::endl;
    std::cout << "argv 2:  service name" << std::endl;
    std::cout << "Should run this test with arguments like these:" << std::endl;
    std::cout << "if you run server : ./fdbobjtest 1 testserver" << std::endl;
    std::cout << "if you run client : ./fdbobjtest 0 testserver" << std::endl;
}


int main(int argc, char **argv)
{
    //shared_ptr_test();
#ifdef __WIN32__
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

/* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        /* Tell the user that we could not find a usable */
        /* Winsock DLL.                                  */
        printf("WSAStartup failed with error: %d\n", err);
        return 1;
    }
#endif
     FDB_CONTEXT->start();
     main_worker.start();
    mediaplayer_worker.start();

     auto *worker_ptr = &main_worker;
    //CBaseWorker *worker_ptr = 0;

    /*
     * 参数1：1 - server； 0 - client
     * 参数2：service的名字
     *
     * 本例子使用name server做地址解析，url格式为svc://server_name。支持的协议包括：
     * - svc:
     * svc://server_name - 指定要连接或创建的服务名；前提是name server必须已经启动
     * - tcp：
     * tcp://ip_address:port_number - 指定tcp连接的ip地址和端口号；不需要name server
     * - ipc:
     * ipc://path - 指定unix域socket的路径名；不需要name server
     */
    if(argc < 3)
    {
        printUsage();
        return -1;
    }
    int32_t is_server = atoi(argv[1]);
    CObjTestTimer obj_test_timer;
    obj_test_timer.attach(&main_worker, true);

    if (is_server)
    {
        // 创建并注册server
        for (int i = 2; i < argc; ++i)
        {
            std::string server_name = argv[i];
            std::string url(FDB_URL_SVC);
            url += server_name;
            server_name += "_server";
            auto *server = new CMyServer<CBaseServer>(server_name.c_str(), worker_ptr);

#ifdef OBJ_FROM_SERVER_TO_CLIENT
            for (int j = 0; j < 5; ++j)
            {
                char obj_id[64];
                sprintf(obj_id, "obj%u", j);
                std::string obj_name = server_name + obj_id;
                auto *obj = new CMyClient<CFdbBaseObject>(obj_name.c_str(), &mediaplayer_worker);
                obj->connect(server, 1000);
            }
#endif

            server->bind(url.c_str());
        }
        CBaseWorker background_worker;
        background_worker.start(FDB_WORKER_EXE_IN_PLACE);
    }
    else
    {
        // 创建client并连接Server
        CMyClient<CBaseClient> *client = 0;
        for (int i = 2; i < argc; ++i)
        {
            std::string server_name = argv[i];
            std::string url(FDB_URL_SVC);
            url += server_name;
            server_name += "_client";
            client = new CMyClient<CBaseClient>(server_name.c_str(), worker_ptr);
            
#ifndef OBJ_FROM_SERVER_TO_CLIENT
            for (int j = 0; j < 5; ++j)
            {
                char obj_id[64];
                sprintf(obj_id, "obj%u", j);
                std::string obj_name = server_name + obj_id;
                auto *obj = new CMyClient<CFdbBaseObject>(obj_name.c_str(), &mediaplayer_worker);
                obj->connect(client, 1000);
            }
#endif
            
            client->enableReconnect(true);
            client->connect(url.c_str());
        }
#if 0
        sysdep_sleep(1000);
        client->disconnect();
        main_worker.flush();
        main_worker.exit();
        main_worker.join();
        FDB_CONTEXT->destroy();
        return 0;
#endif
        CBaseWorker background_worker;
        background_worker.start(FDB_WORKER_EXE_IN_PLACE);
    }

    return 0;
}

