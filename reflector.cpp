
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <iostream>
#include <map>

using namespace std;

#define SECOND_DIFFERENCE_BETWEEN_1900_AND_1970 2208988800uL

struct TWAMP_TS
{
    uint32_t sec;
    uint32_t frac_sec;  // fractional second
};

///////////////////////////////////////////////////////////////////////////////
// convert linux timeval to twamp timestamp
TWAMP_TS tv2ts(const timeval &tv)
{
    TWAMP_TS ts;

    ts.sec = tv.tv_sec;
    ts.sec += SECOND_DIFFERENCE_BETWEEN_1900_AND_1970;
    ts.frac_sec = (uint32_t)((double)tv.tv_usec * ((double)(1uLL << 32) / (double)1e6));

    return ts;
}

///////////////////////////////////////////////////////////////////////////////
// gets current time in twamp timestamp format
TWAMP_TS get_timestamp()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    TWAMP_TS ts = tv2ts(tv);

    // Now, convert to network byte order
    ts.sec = htonl(ts.sec);
    ts.frac_sec = htonl(ts.frac_sec);

    return ts;
}

struct Header
{
    Header()
    {
        memset(this, 0, sizeof(Header));
    }

    uint32_t seq;
    uint32_t ts_sec;
    uint32_t ts_frac_sec;
    uint16_t error;
    uint16_t mbz;
    uint32_t rx_ts_sec;
    uint32_t rx_ts_frac_sec;
    uint32_t tx_seq;
    uint32_t tx_ts_sec;
    uint32_t tx_ts_frac_sec;
    uint16_t tx_error;
    uint16_t tx_mbz;
    uint8_t tx_ttl;
};

#define HEADER_SIZE 41 // TWAMP header size

int write_header(uint8_t *buf, const Header *hdr)
{
    uint32_t *lbuf;

    lbuf = (uint32_t *)buf;
    lbuf[0] = htonl(hdr->seq);
    lbuf[1] = htonl(hdr->ts_sec);
    lbuf[2] = htonl(hdr->ts_frac_sec);
    ((uint16_t *)(lbuf + 3))[0] = htons(hdr->error);
    ((uint16_t *)(lbuf + 3))[1] = htons(hdr->mbz);
    lbuf[4] = htonl(hdr->rx_ts_sec);
    lbuf[5] = htonl(hdr->rx_ts_frac_sec);
    lbuf[6] = htonl(hdr->tx_seq);
    lbuf[7] = htonl(hdr->tx_ts_sec);
    lbuf[8] = htonl(hdr->tx_ts_frac_sec);
    ((uint16_t *)(lbuf + 9))[0] = htons(hdr->tx_error);
    ((uint16_t *)(lbuf + 9))[1] = htons(hdr->tx_mbz);
    ((uint8_t *)(lbuf + 10))[0] = hdr->tx_ttl;
    return 0;
}

int read_header(uint8_t *buf, Header *hdr)
{
    uint32_t *lbuf;

    lbuf = (uint32_t *)buf;
    hdr->seq = ntohl(lbuf[0]);
    hdr->ts_sec = ntohl(lbuf[1]);
    hdr->ts_frac_sec = ntohl(lbuf[2]);
    hdr->error = ntohs(((uint16_t *)(lbuf + 3))[0]);
    hdr->mbz = ntohs(((uint16_t *)(lbuf + 3))[1]);
    hdr->rx_ts_sec = ntohl(lbuf[4]);
    hdr->rx_ts_frac_sec = ntohl(lbuf[5]);
    hdr->tx_seq = ntohl(lbuf[6]);
    hdr->tx_ts_sec = ntohl(lbuf[7]);
    hdr->tx_ts_frac_sec = ntohl(lbuf[8]);
    hdr->tx_error = ntohs(((uint16_t *)(lbuf + 9))[0]);
    hdr->tx_mbz = ntohs(((uint16_t *)(lbuf + 9))[1]);
    hdr->tx_ttl = ((uint8_t *)(lbuf + 10))[0];
    return 0;
}

// comparison function for ordering sockaddr_in
inline bool operator<( const sockaddr_in &left, const sockaddr_in &right )
{
   if( left.sin_port < right.sin_port ) return -1;
   if( left.sin_port > right.sin_port ) return 0;
   if( left.sin_addr.s_addr < right.sin_addr.s_addr ) return -1;
   return 0;
}

///////////////////////////////////////////////////////////////////////////////
int main( int argc, char **argv )
{
    int sock, port, n;
    sockaddr_in local, remote;
    socklen_t remote_len;
    timeval now;
    map<sockaddr_in, int> seqs;
    map<sockaddr_in, int>::iterator iter;
    uint8_t buf[9000];
    Header hdr;
    TWAMP_TS ts;

    if( argc != 2 )
    {
        cerr << "Arguments: <port>\n";
        return -1;
    }
    port = atoi( argv[1] );
    sock = socket( AF_INET, SOCK_DGRAM, 0 );
    memset( &local, 0, sizeof( local ) );
    local.sin_family = AF_INET;
    local.sin_port = htons( port );
    if( bind( sock, ( sockaddr * )&local, sizeof( local ) ) == -1 )
    {
        close( sock );
        cerr << "Bind error: " << strerror( errno ) << endl;
        return -1;
    }
    n = -1;
    if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMP, (void *)&n, sizeof(n)) == -1)
    {
        close( sock );
        cerr << "Cannot set SO_TIMESTAMP: " << strerror( errno ) << endl;
        return -1;
    }
    while( -1 )
    {
        n = recvfrom( sock, buf, 9000, 0, ( sockaddr * )&remote, &remote_len );
        if( n <= 0 )
        {
            cerr << "Read error\n";
            close( sock );
            return 0;
        }
        gettimeofday( &now, 0 );
        iter = seqs.find( remote );
        if( iter == seqs.end() )
        {
            seqs[remote] = 0;
        }
        read_header(buf, &hdr);
        // copy over values
        hdr.tx_seq = hdr.seq;
        hdr.tx_ts_sec = hdr.ts_sec;
        hdr.tx_ts_frac_sec = hdr.ts_frac_sec;
        hdr.tx_error = hdr.error;
        hdr.tx_mbz = hdr.mbz;
        hdr.tx_ttl = 64;  // not implemented
        // fill in new values
        hdr.seq = seqs[remote]++;
        ts = tv2ts(now);
        hdr.rx_ts_sec = ts.sec;
        hdr.rx_ts_frac_sec = ts.frac_sec;
        gettimeofday( &now, 0 );
        ts = tv2ts( now );
        hdr.ts_sec = ts.sec;
        hdr.ts_frac_sec = ts.frac_sec;
        write_header(buf, &hdr);
        // send back
        sendto( sock, buf, n, 0, ( sockaddr * )&remote, sizeof( remote ) );
    }
    return 0;
}
