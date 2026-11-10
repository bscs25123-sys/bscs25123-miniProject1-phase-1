#pragma once

#include <string>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
using namespace std;

const int CLIENT_TIMEOUT_SEC = 30;



struct FuncEntry
{
    string functionLabel;
    int64_t resolveBinPosition;
};
struct PendingPatch
{
    int64_t patchFieldPosition;
    string destinationFuncLabel;
};




inline uint32_t crc32Update(uint32_t runningCrc, const char *data, size_t length)
{
    runningCrc = ~runningCrc;
    for (size_t idx = 0; idx < length; idx++)
    {
        runningCrc ^= (unsigned char)data[idx];
        for (int bitPos = 0; bitPos < 8; bitPos++)
        {
            if (runningCrc & 1)
                runningCrc = (runningCrc >> 1) ^ 0xEDB88320u;
            else
                runningCrc = runningCrc >> 1;
        }
    }
    return ~runningCrc;
}



inline void crcToBytes(uint32_t value, char *dest)
{
    dest[0] = (char)((value >> 24) & 0xFF);
    dest[1] = (char)((value >> 16) & 0xFF);
    dest[2] = (char)((value >> 8) & 0xFF);
    dest[3] = (char)(value & 0xFF);
}

inline void setRecvTimeout(int socketFd, int waitSeconds)
{
    timeval limit;
    limit.tv_sec = waitSeconds;
    limit.tv_usec = 0;
    setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
}



class TrailerChecker
{
    unsigned char lastFour[4];
    int lastFourCount;
    uint32_t checksum;
    long long releasedTotal;




public:
    TrailerChecker()
    {
        lastFourCount = 0;
        checksum = 0;
        releasedTotal = 0;
    }

    size_t feed(const char *incoming, size_t inCount, char *outgoing)
    {
        if (inCount == 0)
            return 0;
        size_t combined = lastFourCount + inCount;
        for (int idx = 0; idx < lastFourCount; idx++)
            outgoing[idx] = (char)lastFour[idx];
        memcpy(outgoing + lastFourCount, incoming, inCount);
        if (combined <= 4)
        {
            for (size_t idx = 0; idx < combined; idx++)
                lastFour[idx] = (unsigned char)outgoing[idx];
            lastFourCount = (int)combined;
            return 0;
        }
        size_t passThrough = combined - 4;
        checksum = crc32Update(checksum, outgoing, passThrough);
        for (int idx = 0; idx < 4; idx++)
            lastFour[idx] = (unsigned char)outgoing[passThrough + idx];
        lastFourCount = 4;
        releasedTotal += passThrough;
        return passThrough;
    }


    
    const char *trailer()
    {
        return (const char *)lastFour;
    }




    bool verify()
    {
        if (lastFourCount != 4 || releasedTotal < 1)
            return false;
        uint32_t received = ((uint32_t)lastFour[0] << 24) | ((uint32_t)lastFour[1] << 16) |
                            ((uint32_t)lastFour[2] << 8) | (uint32_t)lastFour[3];
        return received == checksum;
    }
};