// ======================= TIME-TRAVEL DEBUGGER - CLIENT TEMPLATE =======================

// The client never talks to the server directly. It only talks to the load balancer.
// The load balancer wakes the server up if it is down, so the client does not care.
// The first "start" may just take a bit longer.
//
// The network is used ONLY by "start". After the .tdbg file is downloaded,
// next / prv / jmp read that file on the local disk. They never contact the load balancer.
//
// How the pieces fit together:
//   main loop -> readCommand -> runCommand
//   start -> cmdStart()  -> requestDebugFile() (Part A) -> openTdbg() (Part B) -> shows step 1
//   next, prv, jmp -> moveBy() (Part D) -> readSnapshotAt() (Part B) -> printSnapshot() (Part E)

#include <iostream>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <vector>
#include <sys/socket.h>
#include <sstream>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include "helper.h"
using namespace std;

// ---- Constants ----
const char *PROGRAM_NAME = "ttdb";
const char *PROMPT = "ttdb> ";
const int IO_BUFFER_SIZE = 64 * 1024;     // fixed buffer for streaming files to and from the socket
const char *TDBG_FILE = "ttdb_last.tdbg"; // where the downloaded .tdbg is saved
const int MAX_JUMP = 1000000000;          // biggest n accepted by jmp
string LB_HOST = "127.0.0.0";
int LB_PORT = 5000;

// These two must be the same as in Phase 1

const int MAX_VARS_PER_FRAME = 16;
const int MAX_STACK_DEPTH = 64;


// ---- Structs (same as Phase 1, so the file can be read back exactly as it was written) ----
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

// ---- Structs only the client needs ----
struct TdbgReader
{
    ssize_t file_index;
    TTDBHeader header;
    vector<int64_t> index;
};
struct DebugSession
{
    int active_flag;     // false until a "start" has worked
    TdbgReader reader;   // the .tdbg file stays open for the whole session
    int64_t currentStep; 
};

// =============================================================================================
// PART A: TALKING TO THE LOAD BALANCER
// =============================================================================================

bool recvAll(int sock, char *buf, size_t n)
{
    // recv() can return fewer bytes than asked for, so keep calling it until exactly n bytes are read.
    // Return false if the connection closes or fails before n bytes arrive.
    size_t gotSoFar = 0;
    while (gotSoFar < n)
    {
        ssize_t piece = recv(sock, buf + gotSoFar, n - gotSoFar, 0);
        if (piece < 0 && errno == EINTR)
            continue;
        if (piece <= 0)
            return false;
        gotSoFar += (size_t)piece;
    }
    return true;
}
bool sendAll(int sock, const char *buf, size_t n)
{
    // send() can send fewer bytes than asked for, so keep calling it until all n bytes are sent.
    // Use the flag MSG_NOSIGNAL. Return false if the connection fails.
    size_t pushed = 0;
    while (pushed < n)
    {
        ssize_t piece = send(sock, buf + pushed, n - pushed, MSG_NOSIGNAL);
        if (piece < 0 && errno == EINTR)
            continue;
        if (piece <= 0)
            return false;
        pushed += (size_t)piece;
    }
    return true;
}

int connectToHost(const char *host, int port)
{
    // Create a TCP socket and connect it to host:port.
    // Return the socket, or -1 if the connection fails.
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    string portText = to_string(port);
    struct addrinfo *found = nullptr;
    if (getaddrinfo(host, portText.c_str(), &hints, &found) != 0 || found == nullptr)
        return -1;

    int channel = -1;
    for (struct addrinfo *cand = found; cand != nullptr; cand = cand->ai_next)
    {
        channel = socket(cand->ai_family, cand->ai_socktype, cand->ai_protocol);
        if (channel < 0)
            continue;
        if (connect(channel, cand->ai_addr, cand->ai_addrlen) == 0)
            break;
        close(channel);
        channel = -1;
    }
    freeaddrinfo(found);
    return channel;
}
bool uploadSource(int sock, const char *sourcePath)
{
    // have to do checksum and then send
    // When the file is finished, call shutdown(sock, SHUT_WR). This is how the server knows the string is complete.
    // Return false if the file cannot be opened or any send fails.
    FILE *srcFile = fopen(sourcePath, "rb");
    if (srcFile == nullptr)
        return false;

    vector<char> chunk(IO_BUFFER_SIZE);
    uint32_t runningCrc = 0;
    bool allGood = true;

    while (true)
    {
        size_t got = fread(chunk.data(), 1, chunk.size(), srcFile);
        if (got > 0)
        {
            runningCrc = crc32Update(runningCrc, chunk.data(), got);
            if (!sendAll(sock, chunk.data(), got))
            {
                allGood = false;
                break;
            }
        }
        if (got < chunk.size())
        {
            if (ferror(srcFile))
                allGood = false;
            break;
        }
    }
    fclose(srcFile);
    if (!allGood)
        return false;

    char crcTail[4];
    crcToBytes(runningCrc, crcTail);
    if (!sendAll(sock, crcTail, 4))
        return false;

    shutdown(sock, SHUT_WR);
    return true;
}
bool receiveResponse(int sock, const char *outTdbgPath, string &outError)
{
    // Reply on the wire: [status][payload][CRC-32]. The CRC covers the status byte too.
    // Feed every received byte (status included) to a TrailerChecker.
    // Status 1: collect the payload into outError. Status 0: write the payload to the file.
    // At the end call verify(). If it fails, delete the file, set outError to
    // "checksum mismatch, response corrupted" and return false.
    TrailerChecker guard;
    vector<char> incoming(IO_BUFFER_SIZE);
    vector<char> released(IO_BUFFER_SIZE + 4);

    FILE *landing = nullptr;
    bool statusSeen = false;
    int statusByte = -1;
    outError.clear();

    auto bail = [&](const string &why) -> bool {
        if (landing != nullptr)
        {
            fclose(landing);
            landing = nullptr;
        }
        remove(outTdbgPath);
        outError = why;
        return false;
    };

    while (true)
    {
        ssize_t gotNow = recv(sock, incoming.data(), incoming.size(), 0);
        if (gotNow < 0 && errno == EINTR)
            continue;
        if (gotNow < 0)
            return bail("connection lost or timed out while receiving the response");
        if (gotNow == 0)
            break;

        size_t ready = guard.feed(incoming.data(), (size_t)gotNow, released.data());
        size_t from = 0;
        if (ready > 0 && !statusSeen)
        {
            statusByte = (unsigned char)released[0];
            statusSeen = true;
            from = 1;
            if (statusByte == 0)
            {
                landing = fopen(outTdbgPath, "wb");
                if (landing == nullptr)
                    return bail("cannot create the local .tdbg file");
            }
            else if (statusByte != 1)
            {
                return bail("unknown status from server");
            }
        }
        if (ready > from)
        {
            size_t amount = ready - from;
            if (statusByte == 0)
            {
                if (fwrite(released.data() + from, 1, amount, landing) != amount)
                    return bail("cannot write the local .tdbg file");
            }
            else
            {
                outError.append(released.data() + from, amount);
            }
        }
    }

    if (!statusSeen || !guard.verify())
        return bail("checksum mismatch, response corrupted");

    if (statusByte == 1)
    {
        if (outError.empty())
            outError = "server reported an error";
        return false;
    }

    if (fclose(landing) != 0)
    {
        landing = nullptr;
        return bail("cannot finish writing the local .tdbg file");
    }
    landing = nullptr;
    return true;
}

bool requestDebugFile(const char *host, int port, const char *sourcePath,
                      const char *outTdbgPath, string &outError)
{
    int channel = connectToHost(host, port);
    if (channel < 0)
    {
        outError = "cannot connect to the load balancer";
        return false;
    }
    setRecvTimeout(channel, CLIENT_TIMEOUT_SEC);

    if (!uploadSource(channel, sourcePath))
    {
        close(channel);
        outError = "cannot read the source file or send it to the load balancer";
        return false;
    }
    bool okay = receiveResponse(channel, outTdbgPath, outError);
    close(channel);
    return okay;
}

// =============================================================================================
// PART B: READING THE .tdbg FILE
// File layout (written by Phase 1):
//   header | snapshot 0 | snapshot 1 | ... | index (one 8 byte offset per snapshot)
// The index lets us jump straight to any step without reading the steps before it.
// =============================================================================================

static bool readFully(int fd, void *dest, size_t amount, int64_t where)
{
    char *cursor = (char *)dest;
    size_t done = 0;
    while (done < amount)
    {
        ssize_t piece = pread(fd, cursor + done, amount - done, (off_t)(where + (int64_t)done));
        if (piece < 0 && errno == EINTR)
            continue;
        if (piece <= 0)
            return false;
        done += (size_t)piece;
    }
    return true;
}
static bool takeInt(int fd, int64_t &where, int32_t &out)
{
    if (!readFully(fd, &out, sizeof(out), where))
        return false;
    where += (int64_t)sizeof(out);
    return true;
}
static bool takeText(int fd, int64_t &where, string &out)
{
    int32_t length = 0;
    if (!takeInt(fd, where, length) || length < 0 || length > 4096)
        return false;
    out.assign((size_t)length, '\0');
    if (length > 0 && !readFully(fd, &out[0], (size_t)length, where))
        return false;
    where += length;
    return true;
}
static bool takeVariable(int fd, int64_t &where, Variable &out)
{
    return takeText(fd, where, out.name) && takeInt(fd, where, out.value);
}

bool openTdbg(const char *path, TdbgReader &r)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;

    TTDBHeader head;
    if (!readFully(fd, &head, sizeof(head), 0) || memcmp(head.magic, "TTDB", 4) != 0 ||
        head.stepCount < 1 || head.indexOffset < (int64_t)sizeof(head))
    {
        close(fd);
        return false;
    }

    vector<int64_t> table((size_t)head.stepCount);
    if (!readFully(fd, table.data(), table.size() * sizeof(int64_t), head.indexOffset))
    {
        close(fd);
        return false;
    }
    for (size_t i = 0; i < table.size(); i++)
    {
        if (table[i] < (int64_t)sizeof(head) || table[i] >= head.indexOffset)
        {
            close(fd);
            return false;
        }
    }

    r.file_index = fd;
    r.header = head;
    r.index = table;
    return true;
}
void closeTdbg(TdbgReader &r)
{
    // Free r.index and close the file.
    vector<int64_t>().swap(r.index);
    if (r.file_index >= 0)
        close((int)r.file_index);
    r.file_index = -1;
}
Snapshot *readSnapshotAt(TdbgReader &r, int32_t step)
{
    if (r.file_index < 0 || step < 0 || (size_t)step >= r.index.size())
        return nullptr;

    int fd = (int)r.file_index;
    int64_t where = r.index[(size_t)step];
    Snapshot *shot = new Snapshot();

    bool good = takeInt(fd, where, shot->stackDepth) &&
                shot->stackDepth >= 0 && shot->stackDepth <= MAX_STACK_DEPTH;

    for (int32_t i = 0; good && i < shot->stackDepth; i++)
    {
        Frame &fr = shot->callStack[i];
        good = takeText(fd, where, fr.func_name) && takeInt(fd, where, fr.argc) &&
               fr.argc >= 0 && fr.argc <= MAX_VARS_PER_FRAME;
        for (int32_t a = 0; good && a < fr.argc; a++)
            good = takeVariable(fd, where, fr.argv[a]);
        good = good && takeInt(fd, where, fr.returnLine) && takeInt(fd, where, fr.localCount) &&
               fr.localCount >= 0 && fr.localCount <= MAX_VARS_PER_FRAME;
        for (int32_t l = 0; good && l < fr.localCount; l++)
            good = takeVariable(fd, where, fr.locals[l]);
    }

    if (!good)
    {
        delete shot;
        return nullptr;
    }
    return shot;
}

// =============================================================================================
// PART C: THE SESSION IN MEMORY
// There is one DebugSession variable, created in main() and passed to every command by reference.
// =============================================================================================

void initSession(DebugSession &s)
{
    s.active_flag = 0;
    s.reader.file_index = -1;
    memset(&s.reader.header, 0, sizeof(s.reader.header));
    s.reader.index.clear();
    s.currentStep = 0;
}
void closeSession(DebugSession &s)
{
    // If active, call closeTdbg(s.reader). Then set active = false.
    // Called when the user types quit and when the input ends.
    if (s.active_flag)
        closeTdbg(s.reader);
    s.active_flag = 0;
}

// =============================================================================================
// PART D: THE COMMANDS
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s); // defined in Part E

bool showCurrent(DebugSession &s)
{
    // Read the snapshot at s.currentStep, call printSnapshot, delete the snapshot.
    // If readSnapshotAt returns nullptr, print "damaged .tdbg file" and return false.
    Snapshot *shot = readSnapshotAt(s.reader, (int32_t)s.currentStep);
    if (shot == nullptr)
    {
        cout << "damaged .tdbg file" << endl;
        return false;
    }
    printSnapshot((int32_t)s.currentStep + 1, s.reader.header.stepCount, *shot);
    delete shot;
    return true;
}
bool moveBy(DebugSession &s, int32_t delta)
{
    if (!s.active_flag)
    {
        cout << "no active session, use: start <source file>" << endl;
        return false;
    }
    int64_t aim = s.currentStep + (int64_t)delta;
    if (aim < 0 || aim >= (int64_t)s.reader.header.stepCount)
    {
        cout << "step out of range (valid steps: 1 to " << s.reader.header.stepCount
             << ", now at " << s.currentStep + 1 << ")" << endl;
        return false;
    }
    int64_t before = s.currentStep;
    s.currentStep = aim;
    if (!showCurrent(s))
    {
        s.currentStep = before;
        return false;
    }
    return true;
}
bool cmdStart(DebugSession &s, const string &sourcePath)
{
    // 1. requestDebugFile with host and port, saving to TDBG_FILE.
    // 2. If it fails, print the error text and return false. Keep the old session as it was.
    // 3. Open the new file with openTdbg into a NEW local TdbgReader.
    //    If that fails, print "server sent a damaged .tdbg file", keep the old session, return false.
    // 4. Only now close the old reader (if s.active), then put the new reader into s,
    //    set s.currentStep = 0 and s.active = true.
    // 5. Show step 1 with showCurrent(s).

    string tempName = string(TDBG_FILE) + ".part";

    string failText;
    if (!requestDebugFile(LB_HOST.c_str(), LB_PORT, sourcePath.c_str(), tempName.c_str(), failText))
    {
        cout << failText << endl;
        return false;
    }

    TdbgReader fresh;
    fresh.file_index = -1;
    if (!openTdbg(tempName.c_str(), fresh))
    {
        remove(tempName.c_str());
        cout << "server sent a damaged .tdbg file" << endl;
        return false;
    }

    if (rename(tempName.c_str(), TDBG_FILE) != 0)
        remove(tempName.c_str());

    if (s.active_flag)
        closeTdbg(s.reader);
    s.reader = fresh;
    s.currentStep = 0;
    s.active_flag = 1;

    return showCurrent(s);
}
bool cmdNext(DebugSession &s) { return moveBy(s, 1); }
bool cmdPrv(DebugSession &s) { return moveBy(s, -1); }
bool cmdJmpNext(DebugSession &s, int32_t n) { return moveBy(s, n); }
bool cmdJmpPrv(DebugSession &s, int32_t n) { return moveBy(s, -n); }

// =============================================================================================
// PART E: PRINTING
// =============================================================================================

static void printVariableList(const char *label, const Variable *list, int32_t howMany)
{
    cout << "    " << label << ": ";
    if (howMany == 0)
        cout << "(none)";
    for (int32_t i = 0; i < howMany; i++)
    {
        if (i > 0)
            cout << ", ";
        cout << list[i].name << " = " << list[i].value;
    }
    cout << endl;
}

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s)
{
    cout << "=== Step " << step << " of " << totalSteps << " ===" << endl;
    if (s.stackDepth == 0)
        cout << "  (empty call stack)" << endl;
    for (int32_t i = s.stackDepth - 1; i >= 0; i--)
    {
        const Frame &fr = s.callStack[i];
        cout << "  #" << (s.stackDepth - 1 - i) << " " << fr.func_name
             << " (returns to line " << fr.returnLine << ")" << endl;
        printVariableList("args  ", fr.argv, fr.argc);
        printVariableList("locals", fr.locals, fr.localCount);
    }
}
void printHelp()
{
    // Print the commands
    cout << "Commands:" << endl;
    cout << "  start <source file>    send the source, download the trace, show step 1" << endl;
    cout << "  next                   go one step forward" << endl;
    cout << "  prv                    go one step back" << endl;
    cout << "  jmp next <n>           go n steps forward" << endl;
    cout << "  jmp prv <n>            go n steps back" << endl;
    cout << "  help                   show this text" << endl;
    cout << "  quit | exit            leave the debugger" << endl;
}

// =============================================================================================
// PART F: THE INFINITE LOOP
// =============================================================================================

bool runCommand(DebugSession &s, const string &line, bool &quitRequested)
{
    // 1. Split the line into words.
    // 2. If the first word is "ttdb", drop it.
    // 3. Match the first word:
    //      start      -> needs a path after it, then cmdStart
    //      next       -> no more words, then cmdNext
    //      prv        -> no more words, then cmdPrv
    //      jmp        -> second word "next" or "prv", third word a positive number ,
    //                    then cmdJmpNext or cmdJmpPrv
    //      help       -> printHelp
    //      quit, exit -> set quitRequested = true
    // 4. For anything else, or a wrong number of words, print "unknown command" and the help text, and return false.
    vector<string> tokens;
    {
        istringstream splitter(line);
        string piece;
        while (splitter >> piece)
            tokens.push_back(piece);
    }
    if (!tokens.empty() && tokens[0] == PROGRAM_NAME)
        tokens.erase(tokens.begin());
    if (tokens.empty())
    {
        if (line.find_first_not_of(" \t\r\n") == string::npos)
            return true;
        printHelp();
        return true;
    }

    const string &verb = tokens[0];
    if (verb == "start" && tokens.size() == 2)
        return cmdStart(s, tokens[1]);
    if (verb == "next" && tokens.size() == 1)
        return cmdNext(s);
    if (verb == "prv" && tokens.size() == 1)
        return cmdPrv(s);
    if (verb == "jmp" && tokens.size() == 3 && (tokens[1] == "next" || tokens[1] == "prv"))
    {
        char *tailPtr = nullptr;
        errno = 0;
        long amount = strtol(tokens[2].c_str(), &tailPtr, 10);
        if (errno == 0 && *tailPtr == '\0' && tokens[2][0] != '\0' && amount >= 1 && amount <= MAX_JUMP)
        {
            if (tokens[1] == "next")
                return cmdJmpNext(s, (int32_t)amount);
            return cmdJmpPrv(s, (int32_t)amount);
        }
    }
    if (verb == "help" && tokens.size() == 1)
    {
        printHelp();
        return true;
    }
    if ((verb == "quit" || verb == "exit") && tokens.size() == 1)
    {
        quitRequested = true;
        return true;
    }

    cout << "unknown command" << endl;
    printHelp();
    return false;
}
int main(int argc, char *argv[])
{
    // you get string LB_HOST and int LB_PORT = 0 in arguments of this main function. 
    if (argc >= 2)
        LB_HOST = argv[1];
    if (argc >= 3)
        LB_PORT = atoi(argv[2]);

    signal(SIGPIPE, SIG_IGN);

    DebugSession session;
    initSession(session);
    cout << "Welcome to the time-travel debugger (" << PROGRAM_NAME << ")" << endl;
    printHelp();

    bool quitRequested = false;
    while (true)
    {
        if (quitRequested)
            break;
        cout << PROMPT << flush;
        string typed;
        if (!getline(cin, typed))
            break;
        runCommand(session, typed, quitRequested);
    }

    closeSession(session);
    return 0;
}