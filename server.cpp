#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <string>
#include <cstdint>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include<vector>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "helper.h"
using namespace std;
const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
const int32_t MAX_FUNCS = 128;
const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2;
const int32_t MAX_PATCHES = MAX_FUNCS * 4;
const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024;
const int32_t IO_BUFFER_SIZE = 64 * 1024;
const int32_t SOCKET_TIMEOUT_SEC = 5;
template <typename T>
class Stack
{
    struct Node
    {
        T data;
        Node* next;
    };
    Node* top;
    int32_t count;
public:
    Stack()
    { 
        top = nullptr;
        count = 0;
    }
    ~Stack()
    {
        while (top != nullptr) {
            Node* doomed = top;
            top = top->next;
            delete doomed;
        }
    }
    void push(const T& val)
    {
        Node* n = new Node{ val, top };
        top = n;
        count++;
    }
    T pop()
    {
        if (top == nullptr)
            return T();
        Node *nn = top;
        T val = nn->data;
        top = top->next;
        delete nn;
        count--;
        return val;
    }
    T& peek()
    {
        return top->data;
    }
    bool isEmpty()
    {
        return top == nullptr;
    }
    int32_t depth()
    {
        return count;
    }
    int32_t snapshot_into(T out[], int32_t maxLen)
    {
        Node* u = top;
        int32_t count = 0;
        for (int32_t i = 0; i < maxLen and u != nullptr; i++) {
            out[i] = u->data;
            u = u->next;
            count++;
        }

        return count;
    }
};
struct Snapshot;
struct TimelineNode
{
    Snapshot* data;
    TimelineNode* next;
    TimelineNode* prev;
};
class Timeline
{
    TimelineNode* head, * tail;
    int32_t stepCount;
public:
    Timeline()
    {
        head = nullptr;
        tail = nullptr;
        stepCount = 0;
    }
    ~Timeline();
    void record(Snapshot* s)
    {
        TimelineNode *k=new TimelineNode;
        k->data = s;
        k->next = nullptr;
        k->prev = nullptr;
        if (head==nullptr) {
            head = tail = k;
        }
        else {
            tail->next = k;
             k->prev = tail;
            tail = k;
        }
        stepCount++;
    }
    TimelineNode* begin()
    {
        return head;
    }
    int32_t getStepCount()
    {
        return stepCount;
    }
};
struct Variable
{
    string name;
    int32_t value;
};
struct Frame
{
    string func_name;
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};
struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};
Timeline::~Timeline()
{
    TimelineNode* walker = head;
    while (walker != nullptr) {
        TimelineNode* following = walker->next;
        delete walker->data;
        delete walker;
        walker = following;
    }
}
struct TTDBHeader
{
    char magic[4];
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};
void writeHeader(FILE* f, const TTDBHeader& h)
{
    fwrite(h.magic, 1, 4, f);
    fwrite(&h.version, sizeof(int32_t), 1, f);
    fwrite(&h.stepCount, sizeof(int32_t), 1, f);
    fwrite(&h.indexOffset, sizeof(int64_t), 1, f);
}
bool readSourceLine(ifstream& in, string& out)
{
    char rd;
    while (true) {
        out.clear();
        bool found = false;
        while (in.read(&rd, 1)) {
            found = true;
            if (rd == '\n') {
                break;
            }
            if (rd == '\r') {
                continue;
            }
            out += rd;
        }
        bool blank = true;
        for (char c : out) {
            if (c != ' ' and c != '\t') {
                blank = false;
                break;
            }
        }
        if (!blank)
            return true;
        if (!found)
            return false;
    }
}
string firstWord(const string& line)
{
    string firstword="";
    for (int i = 0; line[i] != ' ' and i<line.size(); i++) {
        firstword += line[i];
  }
    return firstword;
}
string secondWord(const string& line)
{
    int j = 0;
    while (j <line.size() && line[j] != ' ')
        j++;
    while (j < line.size() && line[j] == ' ')
        j++;
    string w = "";
    while (j <line.size() && line[j] != ' ')
        w += line[j++];
    return w;
}
string restAfterSecond(const string& line)
{
    int j = 0;
    while (j <line.size() && line[j] != ' ') 
        j++;  
    while (j < line.size() && line[j] == ' ') 
        j++;   
    while (j < line.size() && line[j] != ' ')
        j++;  
    return line.substr(j);                               
}
bool validateProgram(const char* sourcePath)
{
    ifstream in(sourcePath, ios::binary);
    if (!in) {
        return false;
    }
    bool inside = false;
    string line;
    while (readSourceLine(in, line)) {
        string frstwrd = firstWord(line);
        if (frstwrd == "func") {
            if (inside)
                return false;
            inside = true;
            continue;
        }
        if (frstwrd == "func_end") {
            if(!inside)
            return false;
            inside = false;
            continue;
        }
    }
    if (inside)
        return false;

    return true;
}
int64_t writeResolveRecord(FILE* f, int64_t offsetField, const string& text)
{
    if (!f) {
        return -1;
    }
    int64_t pos= ftell(f);
    int64_t y = text.size();
    fwrite(&offsetField, 8, 1, f);
    fwrite(&y, 4, 1, f);
    fwrite(text.c_str(), 1,y, f);
    return pos;
}
int64_t readResolveRecord(FILE* f, string& outText)
{
    if (!f) {
        return -1;
    }
  
    int64_t g;
   
    if (fread(&g, 8, 1, f) != 1) {
        return -1;
    }
    int32_t o;

    if (fread(&o, 4, 1, f)!=1) {
        return -1;
    }
    outText.resize(o);
    if (fread(&outText[0],  1,o, f) != o) {
        return -1;
    }
    return g;
}
int64_t resolveProgram(const char* sourcePath, const char* resolveBinPath)
{
    FuncEntry funcArray[MAX_FUNCS];
    int32_t funcCount = 0;
    vector<string> l;
    vector<int64_t> lineoff;
    ifstream in(sourcePath, ios::binary);
    if (!in) 
        return -1;
    int64_t offset = 0;
    string line;
    while (readSourceLine(in, line)) {
        string kw = firstWord(line);
        string name = secondWord(line);
        string rest = restAfterSecond(line);
        lineoff.push_back(offset);
        l.push_back(line);
        if (kw == "func") {
            if (funcCount >= MAX_FUNCS) 
                return -1;
            funcArray[funcCount].functionLabel = name;
            funcArray[funcCount].resolveBinPosition = offset;
            funcCount++;
        }
        int64_t size = line.size();
        if (kw == "call")
            size = 5 + 16 + rest.size();     
        offset += 8 + 4 + size;
    }
    in.close();
    FILE* out = fopen(resolveBinPath, "wb");
    if (!out) 
        return -1;
    for (int32_t i = 0; i < (int32_t)l.size(); i++) {
        string kw = firstWord(l[i]);
        string name = secondWord(l[i]);
        string rest = restAfterSecond(l[i]);
        string toWrite = l[i];
        if (kw == "call") {
            int64_t tgt = -1;
            for (int32_t j = 0; j < funcCount; j++) {
                if (funcArray[j].functionLabel == name)
                    tgt = funcArray[j].resolveBinPosition;
            }
            if (tgt < 0) {
                fclose(out);
                return -1;
            }
            string dig = to_string(tgt);
            string padd = string(16 - dig.size(), '0') + dig;
            toWrite = "call " + padd + rest;
        }
        writeResolveRecord(out, lineoff[i], toWrite);
    }
    fclose(out);
    for (int32_t j = 0; j < funcCount; j++) {
        if (funcArray[j].functionLabel == "main")
            return funcArray[j].resolveBinPosition;
    }
    return -1;
}
enum TokenType
{
    KEYWORD,
    IDENTIFIER,
    PARAM
};
struct Token
{
    TokenType type;
    string text;
};
int32_t tokenizeLine(const string& line, Token tokens[], int32_t maxTokens)
{
    int32_t n = 0;
    int i = 0;
    int len = line.size();
    while (i < len && n < maxTokens) {
        while (i < len && line[i] == ' ') 
            i++;
        if (i >= len)
        { break;
        }
        string w = "";
        while (i < len && line[i] != ' ') 
            w += line[i++];
        tokens[n].text = w;
        tokens[n].type = (n == 0) ? KEYWORD : (n == 1) ? IDENTIFIER : PARAM;
        n++;
    }
    return n;
}
Variable* findVar(Frame& f, const string& nm)
{
    for (int i = 0; i < f.argc; i++) {
        if (f.argv[i].name == nm)
            return &f.argv[i];
    }
    for (int i = 0; i < f.localCount; i++) {
        if (f.locals[i].name == nm)
            return &f.locals[i];
    }
    return nullptr;
}

Variable* getVar(Frame& f, const string& nm)
{
    Variable* v = findVar(f, nm);
    if (v != nullptr)
        return v;
    if (f.localCount >= MAX_VARS_PER_FRAME)
        return nullptr;
    int k = f.localCount;
    f.locals[k].name = nm;
    f.locals[k].value = 0;
    f.localCount++;
    return &f.locals[k];
}
bool isNum(const string& s)
{
    if (s.empty())
        return false;
    int start = 0;
    if (s[0] == '-')
        start = 1;
    if (start == s.size())
        return false;
    for (int i = start; i < s.size(); i++) {
        if (s[i] < '0' || s[i] > '9')
            return false;
    }
    return true;
}
int32_t valOf(Frame& f, const string& s)
{
    Variable* v = findVar(f, s);
    if (v != nullptr)
        return v->value;
    if (isNum(s))
        return stoi(s);
    return 0;
}
Snapshot* buildSnapshot(Stack<Frame>& callStack)
{
    Snapshot* s = new Snapshot;
    s->stackDepth = callStack.snapshot_into(s->callStack, MAX_STACK_DEPTH);
    int32_t d = s->stackDepth;
    for (int32_t i = 0; i < d / 2; i++) {
        Frame t = s->callStack[i];
        s->callStack[i] = s->callStack[d - 1 - i];
        s->callStack[d - 1 - i] = t;
    }
    return s;
}
void executeProgram(const char* resolveBinPath, int64_t mainOffset, Timeline& timeline)
{
 FILE* f = fopen(resolveBinPath, "rb");
  if (f == nullptr)
    return;
  if (mainOffset < 0) {
    fclose(f);
    return;
}
 Stack<Frame> st;
  Token tk[MAX_TOKENS];
  Token hd[MAX_TOKENS];
  string s;
  fseek(f, (long)mainOffset, SEEK_SET);
 readResolveRecord(f, s);
Frame m;
m.func_name = "main";
m.argc = 0;
m.returnLine = -1;
m.localCount = 0;
st.push(m);
while (true) {
    int64_t cur = readResolveRecord(f, s);
    if (cur < 0)
        break;
    int n = tokenizeLine(s, tk, MAX_TOKENS);
    if (n == 0)
        continue;
    string kw = tk[0].text;
    Frame& top = st.peek();
    if (kw == "set" && n >= 3) {
        Variable* v = getVar(top, tk[1].text);
        if (v != nullptr)
            v->value = valOf(top, tk[2].text);
    }
    else if (kw == "add" && n >= 3) {
        Variable* v = getVar(top, tk[1].text);
        if (v != nullptr)
            v->value += valOf(top, tk[2].text);
    }
    else if (kw == "sub" && n >= 3) {
        Variable* v = getVar(top, tk[1].text);
        if (v != nullptr)
            v->value -= valOf(top, tk[2].text);
    }
    else if (kw == "mul" && n >= 3) {
        Variable* v = getVar(top, tk[1].text);
        if (v != nullptr)
            v->value *= valOf(top, tk[2].text);
    }
    else if (kw == "div" && n >= 3) {
        Variable* v = getVar(top, tk[1].text);
        int y = valOf(top, tk[2].text);
        if (v != nullptr && y != 0)
            v->value /= y;
    }
    else if (kw == "call" && n >= 2) {
        if (st.depth() >= MAX_STACK_DEPTH)
            break;
        fseek(f, (long)stoll(tk[1].text), SEEK_SET);
        readResolveRecord(f, s);
        int hn = tokenizeLine(s, hd, MAX_TOKENS);
        Frame nf;
        nf.func_name = hd[1].text;
        nf.argc = hn - 2;
        nf.returnLine = (int32_t)cur;
        nf.localCount = 0;
        for (int i = 0; i < nf.argc; i++) {
            nf.argv[i].name = hd[2 + i].text;
            nf.argv[i].value = 0;
            if (2 + i < n)
                nf.argv[i].value = valOf(top, tk[2 + i].text);
        }
        st.push(nf);
    }
    else if (kw == "func_end") {
        if (st.depth() == 1) {
            timeline.record(buildSnapshot(st));
            break;
        }
        Frame done = st.pop();
        Frame& back = st.peek();
        fseek(f, done.returnLine, SEEK_SET);
        readResolveRecord(f, s);
        n = tokenizeLine(s, tk, MAX_TOKENS);
        for (int i = 0; i < done.argc; i++) {
            if (2 + i < n) {
                Variable* v = findVar(back, tk[2 + i].text);
                if (v != nullptr)
                    v->value = done.argv[i].value;
            }
        }
    }
    timeline.record(buildSnapshot(st));
}
fclose(f);
}
void wstr(FILE* f, const string& s)
{
    int32_t n = s.size();
    fwrite(&n, 4, 1, f);
    fwrite(s.c_str(), 1, n, f);
}
void wvar(FILE* f, const Variable& v)
{
    wstr(f, v.name);
    fwrite(&v.value, 4, 1, f);
}
void wframe(FILE* f, const Frame& fr)
{
    wstr(f, fr.func_name);
    fwrite(&fr.argc, 4, 1, f);
    for (int32_t i = 0; i < fr.argc; i++)
        wvar(f, fr.argv[i]);
    fwrite(&fr.returnLine, 4, 1, f);
    fwrite(&fr.localCount, 4, 1, f);
    for (int32_t i = 0; i < fr.localCount; i++)
        wvar(f, fr.locals[i]);
}
void writeTdbg(Timeline& timeline, const char* tdbgPath)
{
    FILE* f = fopen(tdbgPath, "wb");
    if (!f)
        return;
    TTDBHeader h;
    h.magic[0] = 'T';
    h.magic[1] = 'T';
    h.magic[2] = 'D';
    h.magic[3] = 'B';
    h.version = 1;
    h.stepCount = timeline.getStepCount();
    h.indexOffset = 0;
    writeHeader(f, h);
    vector<int64_t> idx;
    for (TimelineNode* k = timeline.begin(); k != nullptr; k = k->next) {
        idx.push_back(ftell(f));
        Snapshot* s = k->data;
        fwrite(&s->stackDepth, 4, 1, f);
        for (int32_t i = 0; i < s->stackDepth; i++)
            wframe(f, s->callStack[i]);
    }
    h.indexOffset = ftell(f);
    for (int32_t i = 0; i < (int32_t)idx.size(); i++)
        fwrite(&idx[i], 8, 1, f);
    fseek(f, 0, SEEK_SET);
    writeHeader(f, h);
    fclose(f);
}
bool sendAll(int sock, const char* data, size_t length)
{
    size_t pushed = 0;
    while (pushed < length) {
        ssize_t piece = send(sock, data + pushed, length - pushed, MSG_NOSIGNAL);
        if (piece < 0 && errno == EINTR)
            continue;
        if (piece <= 0)
            return false;
        pushed += (size_t)piece;
    }
    return true;
}
bool receiveSource(int sock, const char* sourcePath, string& problem)
{
    FILE* target = fopen(sourcePath, "wb");
    if (target == nullptr) {
        problem = "server cannot store the uploaded source";
        return false;
    }
    TrailerChecker guard;
    vector<char> incoming(IO_BUFFER_SIZE);
    vector<char> released(IO_BUFFER_SIZE + 4);
    bool statusSeen = false;
    int statusByte = -1;
    uint64_t bodyBytes = 0;
    bool oversize = false;
    bool writeFailed = false;

    while (true) {
        ssize_t gotNow = recv(sock, incoming.data(), incoming.size(), 0);
        if (gotNow < 0 && errno == EINTR)
            continue;
        if (gotNow < 0) {
            fclose(target);
            problem = "connection lost or timed out during upload";
            return false;
        }
        if (gotNow == 0)
            break;
        size_t ready = guard.feed(incoming.data(), (size_t)gotNow, released.data());
        size_t from = 0;
        if (ready > 0 && !statusSeen) {
            statusByte = (unsigned char)released[0];
            statusSeen = true;
            from = 1;
        }
        if (ready > from) {
            size_t amount = ready - from;
            bodyBytes += amount;
            if (bodyBytes > MAX_SOURCE_BYTES)
                oversize = true;
            else if (fwrite(released.data() + from, 1, amount, target) != amount)
                writeFailed = true;
        }
    }
    fclose(target);
    if (!statusSeen || !guard.verify()) {
        problem = "checksum mismatch, upload corrupted";
        return false;
    }
    if (statusByte != 0) {
        problem = "bad request from client";
        return false;
    }
    if (oversize) {
        problem = "source file too large";
        return false;
    }
    if (writeFailed) {
        problem = "server cannot store the uploaded source";
        return false;
    }
    return true;
}
bool sendTrailer(int sock, uint32_t sum)
{
    char tailBytes[4];
    crcToBytes(sum, tailBytes);
    return sendAll(sock, tailBytes, 4);
}
bool sendErrorReply(int sock, const string& text)
{
    char statusFlag = 1;
    uint32_t sum = crc32Update(0, &statusFlag, 1);
    sum = crc32Update(sum, text.c_str(), text.size());
    return sendAll(sock, &statusFlag, 1) && sendAll(sock, text.c_str(), text.size()) &&
           sendTrailer(sock, sum);
}
bool sendFileReply(int sock, const char* path)
{
    FILE* source = fopen(path, "rb");
    if (source == nullptr)
        return sendErrorReply(sock, "server could not produce the .tdbg file");
    char statusFlag = 0;
    uint32_t sum = crc32Update(0, &statusFlag, 1);
    bool healthy = sendAll(sock, &statusFlag, 1);
    vector<char> chunk(IO_BUFFER_SIZE);
    while (healthy) {
        size_t got = fread(chunk.data(), 1, chunk.size(), source);
        if (got > 0) {
            sum = crc32Update(sum, chunk.data(), got);
            healthy = sendAll(sock, chunk.data(), got);
        }
        if (got < chunk.size()) {
            if (ferror(source))
                healthy = false;
            break;
        }
    }
    fclose(source);
    return healthy && sendTrailer(sock, sum);
}
void serveClient(int sock)
{
    string problem;
    if (!receiveSource(sock, "source.bin", problem)) {
        sendErrorReply(sock, problem);
        return;
    }
    if (!validateProgram("source.bin")) {
        sendErrorReply(sock, "invalid program: FUNC/FUNC_END mismatch or nested function");
        return;
    }
    int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");
    if (mainOffset < 0) {
        sendErrorReply(sock, "cannot resolve program: main missing, call to an undefined function, or too many functions");
        return;
    }
    Timeline timeline;
    executeProgram("resolve.bin", mainOffset, timeline);
    writeTdbg(timeline, "session.tdbg");
    sendFileReply(sock, "session.tdbg");
}
int32_t main(int argc, char* argv[])
{
    if (argc < 2) {
        cerr << "usage: ./server <port>" << endl;
        return 1;
    }
    int listenPort = atoi(argv[1]);
    signal(SIGPIPE, SIG_IGN);

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        cerr << "cannot create socket" << endl;
        return 1;
    }
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddr.sin_port = htons((uint16_t)listenPort);
    if (bind(listener, (sockaddr*)&bindAddr, sizeof(bindAddr)) < 0 || listen(listener, 8) < 0) {
        cerr << "cannot listen on port " << listenPort << endl;
        close(listener);
        return 1;
    }
    while (true) {
        int sock = accept(listener, nullptr, nullptr);
        if (sock < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        setRecvTimeout(sock, SOCKET_TIMEOUT_SEC);
        serveClient(sock);
        shutdown(sock, SHUT_WR);
        close(sock);
    }
    close(listener);
    return 0;
}