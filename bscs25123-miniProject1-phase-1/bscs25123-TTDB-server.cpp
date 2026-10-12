
// ======================= TIME-TRAVEL DEBUGGER - SERVER TEMPLATE =======================

// Pipeline this file implements, top to bottom:
//   0. Receive  -- stream the client's .trace bytes straight to source.bin on disk
//   1. Pass 0X0   -- validity check (FUNC/FUNC_END matching)
//   2. Pass 0X1   -- resolve(): copy EVERY source line into resolve.bin as [offset][size][string], then patch CALL targets.
//   3. Pass 0X2   -- execute resolve.bin: tokenize ONE line at a time, update the call stack, take a snapshot -> Timeline
//   4. Pass 0X3   -- serialize Timeline -> session.tdbg(header + snapshot records + dense index)

#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <string>
#include <cstdint>
#include <fstream>
#include <cstdio>
#include<vector>
using namespace std;

// ---- Constants ----
const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
const int32_t MAX_FUNCS = 128;
const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2; // kW + func_name + upto 16 params/args
const int32_t MAX_PATCHES = MAX_FUNCS * 4;
const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024; // sanity cap on the declared file length
const int32_t IO_BUFFER_SIZE = 64 * 1024;                  // fixed buffer for streaming to/from disk
const int32_t SOCKET_TIMEOUT_SEC = 5;                      // TODO: apply as SO_RCVTIMEO so a deadclient can't hang the server forever

// ---- Custom data structures

// Stack: back the live Call Stack during execution
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
    // Implement these functions:
    Stack()
    { 
        top = nullptr;
        count = 0;
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


// Timeline : doubly linked list of Snapshots
struct Snapshot; // fwd declaration;
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
    // Implement these functions
    Timeline()
    {
        head = nullptr;
        tail = nullptr;
        stepCount = 0;
    }
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

// Core structs
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
struct TTDBHeader
{
    char magic[4]; // "TTDB"
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
// resolve.bin - bookkeeping
struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};



// PASS 0x0: READING source.bin + VALIDITY CHECK
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

// PASS 0x1: RESOLVE() -> resolve.bin
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
    // writes one [offset(8B)][size(4B)][string] record at the current file position
    // returns this record's own starting byte position
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
    // reads one record at the current position and advances past it, returns the offset field - the raw line text comes back untouched in outText.
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
            funcArray[funcCount].funcName = name;
            funcArray[funcCount].byteOffsetInResolveBin = offset;
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
                if (funcArray[j].funcName == name)
                    tgt = funcArray[j].byteOffsetInResolveBin;
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
        if (funcArray[j].funcName == "main")
            return funcArray[j].byteOffsetInResolveBin;
    }
    return -1;
}
    // Every source line becomes one record holding the raw line, as-is.
    // resolve() only PEEKS at the leading word(s) -- enough to spot FUNC
    // (remember its position) and CALL (remember which function it needs
    // and where its offset field sits).
    // Once the whole file is written, every CALL's offset field is patched
    // with its target's position. Patching happens after the full write
    // Returns the byte offset of main's FUNC header record.
    // if there is no main return the error 


// PASS 0x2: EXECUTION (tokenization happens here)
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
    // first word is always a instruction keyword
    // instruction set = [func, func_end, call, set, add, sub, mul and div]
    // next word is identifier like name of a function, variable name
    // after identifier all are the params/arg, space separated
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
    // build the snapshot based on the callStack given
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
    // initialize the call stack
    // make the main frame
    // push main frame on the call stack

    // implementation:
    // execute line by line, and according to the keyword perform action

// PASS 0x3: SERIALIZE TIMELINE
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
    // placeholder for header
    // index array of the size of stepcount from the timeline
    // placing each snapshot in the file while maintaining the index(starting point of each nth snapshot)
    // after timeline add the index array i the file
    // update the header
}
// main section
int32_t main()
{

    if (!validateProgram("source.bin"))
    {
        cout << "Errror";
        return 1;
    }

    int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");
    if (mainOffset < 0)
    {
        cout << "Errror";        return 1;
    }
    Timeline timeline;
    executeProgram("resolve.bin", mainOffset, timeline);

    writeTdbg(timeline, "session.tdbg");

    return 0;
}
