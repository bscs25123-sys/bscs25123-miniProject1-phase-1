#include <iostream>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "helper.h"
using namespace std;
const char *SERVER_PATH = "./server";
const int IO_BUFFER_SIZE = 64 * 1024;
const int LISTEN_BACKLOG = 16;
const int MAX_PENDING_CLIENTS = 8;
const int STARTUP_MAX_ATTEMPTS = 50;
const int STARTUP_POLL_INTERVAL_MS = 100;
int lbPort = 5000;
string serverHost = "127.0.0.1";
int serverPort = 5001;
template <typename T>
class Queue
{
    struct Node
    {
        T data;
        Node *next;
    };
    Node *front, *rear;
    int count;
public:
    Queue()
    {
        front = nullptr;
        rear = nullptr;
        count = 0;
    }
    ~Queue()
    {
        while (front != nullptr)
        {
            Node *oldnode = front;
            front = front->next;
            delete oldnode;
        }
        rear = nullptr;
        count = 0;
    }
    void enqueue(const T &val)
    {
        Node *newnode = new Node;
        newnode->data = val;
        newnode->next = nullptr;
        if (rear == nullptr)
        {
            front = newnode;
            rear = newnode;
        }
        else
        {
            rear->next = newnode;
            rear = newnode;
        }
        count++;
    }
    T dequeue()
    {
        if (front == nullptr)
            return T();
        Node *oldnode = front;
        T item = oldnode->data;
        front = front->next;
        if (front == nullptr)
            rear = nullptr;
        delete oldnode;
        count--;
        return item;
    }
    bool isEmpty()
    {
        return count == 0;
    }
    int size()
    {
        return count;
    }
};
struct ClientInfo
{
    int sock;
    string address;
    int port;
    time_t arrivedAt;
};
Queue<ClientInfo> pendingClients;
mutex queueMutex;
volatile sig_atomic_t flag = 1;
bool sendAll(int sock, const char *buf, size_t n)
{
    size_t sentlen = 0;
    while (sentlen < n)
    {
        ssize_t sentnow = send(sock, buf + sentlen, n - sentlen, MSG_NOSIGNAL);
        if (sentnow < 0 && errno == EINTR)
            continue;
        if (sentnow <= 0)
            return false;
        sentlen += sentnow;
    }
    return true;
}
bool sendError(int sock, const string &message)
{
    char code = 1;
    uint32_t sum = crc32Update(0, &code, 1);
    sum = crc32Update(sum, message.c_str(), message.size());
    char sumbytes[4];
    crcToBytes(sum, sumbytes);
    if (!sendAll(sock, &code, 1))
        return false;
    if (!sendAll(sock, message.c_str(), message.size()))
        return false;
    return sendAll(sock, sumbytes, 4);
}
int createListenSocket(int port)
{
    int lsock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (lsock < 0)
    {
        perror("socket");
        exit(1);
    }
    int reuse = 1;
    setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in bindaddr;
    memset(&bindaddr, 0, sizeof(bindaddr));
    bindaddr.sin_family = AF_INET;
    bindaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bindaddr.sin_port = htons(port);
    if (bind(lsock, (sockaddr *)&bindaddr, sizeof(bindaddr)) < 0)
    {
        perror("bind");
        exit(1);
    }
    if (listen(lsock, LISTEN_BACKLOG) < 0)
    {
        perror("listen");
        exit(1);
    }
    return lsock;
}
int connectToServer(const char *host, int port)
{
    int ssock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (ssock < 0)
        return -1;
    sockaddr_in srvaddr;
    memset(&srvaddr, 0, sizeof(srvaddr));
    srvaddr.sin_family = AF_INET;
    srvaddr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &srvaddr.sin_addr) != 1)
    {
        close(ssock);
        return -1;
    }
    if (connect(ssock, (sockaddr *)&srvaddr, sizeof(srvaddr)) < 0)
    {
        close(ssock);
        return -1;
    }
    return ssock;
}
bool startServer(int port)
{
    string cmd = string(SERVER_PATH) + " " + to_string(port) + " &";
    return system(cmd.c_str()) == 0;
}
int connectToServerWithWakeup()
{
    int conn = connectToServer(serverHost.c_str(), serverPort);
    if (conn >= 0)
        return conn;
    if (!startServer(serverPort))
        return -1;
    for (int tries = 0; tries < STARTUP_MAX_ATTEMPTS; tries++)
    {
        usleep(STARTUP_POLL_INTERVAL_MS * 1000);
        conn = connectToServer(serverHost.c_str(), serverPort);
        if (conn >= 0)
            return conn;
    }
    return -1;
}
void relayOneClient(ClientInfo &c)
{
    int serverSock = connectToServerWithWakeup();
    if (serverSock < 0)
    {
        sendError(c.sock, "backend server failed to start");
        close(c.sock);
        return;
    }
    setRecvTimeout(c.sock, CLIENT_TIMEOUT_SEC);
    setRecvTimeout(serverSock, CLIENT_TIMEOUT_SEC);
    char inbuf[IO_BUFFER_SIZE];
    char outbuf[IO_BUFFER_SIZE + 4];
    TrailerChecker upchk;
    bool upok = true;
    while (true)
    {
        ssize_t recvlen = recv(c.sock, inbuf, IO_BUFFER_SIZE, 0);
        if (recvlen < 0 && errno == EINTR)
            continue;
        if (recvlen == 0)
            break;
        if (recvlen < 0)
        {
            upok = false;
            break;
        }
        size_t fwdlen = upchk.feed(inbuf, recvlen, outbuf);
        if (fwdlen > 0 && !sendAll(serverSock, outbuf, fwdlen))
        {
            upok = false;
            break;
        }
    }
    if (!upok)
    {
        close(serverSock);
        sendError(c.sock, "upload failed");
        close(c.sock);
        return;
    }
    if (!upchk.verify())
    {
        close(serverSock);
        sendError(c.sock, "upload checksum mismatch");
        close(c.sock);
        return;
    }
    sendAll(serverSock, upchk.trailer(), 4);
    shutdown(serverSock, SHUT_WR);
    TrailerChecker replychk;
    bool replydone = false;
    while (true)
    {
        ssize_t recvlen = recv(serverSock, inbuf, IO_BUFFER_SIZE, 0);
        if (recvlen < 0 && errno == EINTR)
            continue;
        if (recvlen == 0)
        {
            replydone = true;
            break;
        }
        if (recvlen < 0)
            break;
        size_t fwdlen = replychk.feed(inbuf, recvlen, outbuf);
        if (fwdlen > 0 && !sendAll(c.sock, outbuf, fwdlen))
            break;
    }
    if (replydone && replychk.verify())
        sendAll(c.sock, replychk.trailer(), 4);
    close(serverSock);
    close(c.sock);
}
void acceptLoop(int listenSock)
{
    while (flag)
    {
        sockaddr_in cliaddr;
        socklen_t addrlen = sizeof(cliaddr);
        int clisock = accept4(listenSock, (sockaddr *)&cliaddr, &addrlen, SOCK_CLOEXEC);
        if (clisock < 0)
        {
            if (flag && errno != EINTR)
            {
                perror("accept");
                usleep(10000);
            }
            continue;
        }
        ClientInfo newcli;
        newcli.sock = clisock;
        char iptext[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cliaddr.sin_addr, iptext, sizeof(iptext));
        newcli.address = iptext;
        newcli.port = ntohs(cliaddr.sin_port);
        newcli.arrivedAt = time(nullptr);
        queueMutex.lock();
        if (pendingClients.size() >= MAX_PENDING_CLIENTS)
        {
            queueMutex.unlock();
            sendError(clisock, "server busy, try again later");
            close(clisock);
        }
        else
        {
            pendingClients.enqueue(newcli);
            queueMutex.unlock();
        }
    }
}
void dispatcherLoop()
{
    while (flag)
    {
        bool hasclient = false;
        ClientInfo curclient;
        queueMutex.lock();
        if (!pendingClients.isEmpty())
        {
            curclient = pendingClients.dequeue();
            hasclient = true;
        }
        queueMutex.unlock();
        if (hasclient)
        {
            if (difftime(time(nullptr), curclient.arrivedAt) > CLIENT_TIMEOUT_SEC)
                close(curclient.sock);
            else
                relayOneClient(curclient);
        }
        else
        {
            usleep(20000);
        }
    }
}
void handler(int sg)
{
    (void)sg;
    flag = 0;
}
int main(int argc, char *argv[])
{
    signal(SIGINT, handler);
    if (argc >= 2)
        serverHost = argv[1];
    if (argc >= 3)
        serverPort = atoi(argv[2]);
    int listenSock = createListenSocket(lbPort);
    thread acceptthread(acceptLoop, listenSock);
    acceptthread.detach();
    dispatcherLoop();
    queueMutex.lock();
    while (!pendingClients.isEmpty())
    {
        ClientInfo leftclient = pendingClients.dequeue();
        close(leftclient.sock);
    }
    queueMutex.unlock();
    close(listenSock);
    return 0;
}