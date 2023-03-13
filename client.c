#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h> //bools are nice :)
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netdb.h>

// =====================================

#define RTO 500000       /* timeout in microseconds */
#define HDR_SIZE 12      /* header size*/
#define PKT_SIZE 524     /* total packet size */
#define PAYLOAD_SIZE 512 /* PKT_SIZE - HDR_SIZE */
#define WND_SIZE 10      /* window size*/
#define MAX_SEQN 25601   /* number of sequence numbers [0-25600] */
#define FIN_WAIT 2       /* seconds to wait after receiving FIN*/

// Packet Structure: Described in Section 2.1.1 of the spec. DO NOT CHANGE!
struct packet
{
    unsigned short seqnum;
    unsigned short acknum;
    char syn;
    char fin;
    char ack;
    char dupack;
    unsigned int length;
    char payload[PAYLOAD_SIZE];
};

// Printing Functions: Call them on receiving/sending/packet timeout according
// Section 2.6 of the spec. The content is already conformant with the spec,
// no need to change. Only call them at correct times.
void printRecv(struct packet *pkt)
{
    printf("RECV %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN" : "", pkt->fin ? " FIN" : "", (pkt->ack || pkt->dupack) ? " ACK" : "");
}

void printSend(struct packet *pkt, int resend)
{
    if (resend)
        printf("RESEND %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN" : "", pkt->fin ? " FIN" : "", pkt->ack ? " ACK" : "");
    else
        printf("SEND %d %d%s%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN" : "", pkt->fin ? " FIN" : "", pkt->ack ? " ACK" : "", pkt->dupack ? " DUP-ACK" : "");
}

void printTimeout(struct packet *pkt)
{
    printf("TIMEOUT %d\n", pkt->seqnum);
}

// Building a packet by filling the header and contents.
// This function is provided to you and you can use it directly
void buildPkt(struct packet *pkt, unsigned short seqnum, unsigned short acknum, char syn, char fin, char ack, char dupack, unsigned int length, const char *payload)
{
    pkt->seqnum = seqnum;
    pkt->acknum = acknum;
    pkt->syn = syn;
    pkt->fin = fin;
    pkt->ack = ack;
    pkt->dupack = dupack;
    pkt->length = length;
    memcpy(pkt->payload, payload, length);
}

// =====================================

double setTimer()
{
    struct timeval e;
    gettimeofday(&e, NULL);
    return (double)e.tv_sec + (double)e.tv_usec / 1000000 + (double)RTO / 1000000;
}

double setFinTimer()
{
    struct timeval e;
    gettimeofday(&e, NULL);
    return (double)e.tv_sec + (double)e.tv_usec / 1000000 + (double)FIN_WAIT;
}

int isTimeout(double end)
{
    struct timeval s;
    gettimeofday(&s, NULL);
    double start = (double)s.tv_sec + (double)s.tv_usec / 1000000;
    return ((end - start) < 0.0);
}

// ===================================== Matan utils start

// this function returns the current numbers in transmission as indicated by s and e.
// even though that number is {0,...,WND_SIZE} this Range(calc_cur_windowsize) = {1,...,WNDSIZE} where we know if
// calc_cur_windowsize should return zero by checking variable `zero_packets_in_transmission`
int calc_cur_windowsize(int s, int e)
{
    if (s < e)
    {
        return e - s;
    }
    else
    {
        return WND_SIZE - s + e;
    }
}
int mod(int x, int N)
{
    return (x % N + N) % N;
}
// ===================================== Matan utils end

int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        perror("ERROR: incorrect number of arguments\n "
               "Please use \"./client <HOSTNAME-OR-IP> <PORT> <ISN> <FILENAME>\"\n");
        exit(1);
    }

    struct in_addr servIP;
    if (inet_aton(argv[1], &servIP) == 0)
    {
        struct hostent *host_entry;
        host_entry = gethostbyname(argv[1]);
        if (host_entry == NULL)
        {
            perror("ERROR: IP address not in standard dot notation\n");
            exit(1);
        }
        servIP = *((struct in_addr *)host_entry->h_addr_list[0]);
    }

    unsigned int servPort = atoi(argv[2]);
    unsigned short initialSeqNum = atoi(argv[3]);

    FILE *fp = fopen(argv[4], "r");
    if (fp == NULL)
    {
        perror("ERROR: File not found\n");
        exit(1);
    }

    // =====================================
    // Socket Setup

    int sockfd;
    struct sockaddr_in servaddr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr = servIP;
    servaddr.sin_port = htons(servPort);
    memset(servaddr.sin_zero, '\0', sizeof(servaddr.sin_zero));

    int servaddrlen = sizeof(servaddr);

    // NOTE: We set the socket as non-blocking so that we can poll it until
    //       timeout instead of getting stuck. This way is not particularly
    //       efficient in real programs but considered acceptable in this
    //       project.
    //       Optionally, you could also consider adding a timeout to the socket
    //       using setsockopt with SO_RCVTIMEO instead.
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    // =====================================
    // Establish Connection: This procedure is provided to you directly and is
    // already working.
    // Note: The third step (ACK) in three way handshake is sent along with the
    // first piece of along file data thus is further below

    struct packet synpkt, synackpkt;
    unsigned short seqNum = initialSeqNum;

    buildPkt(&synpkt, seqNum, 0, 1, 0, 0, 0, 0, NULL);

    printSend(&synpkt, 0);
    sendto(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr *)&servaddr, servaddrlen);
    double timer = setTimer();
    int n;

    while (1)
    {
        while (1)
        {
            n = recvfrom(sockfd, &synackpkt, PKT_SIZE, 0, (struct sockaddr *)&servaddr, (socklen_t *)&servaddrlen);

            if (n > 0)
                break;
            else if (isTimeout(timer))
            {
                printTimeout(&synpkt);
                printSend(&synpkt, 1);
                sendto(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr *)&servaddr, servaddrlen);
                timer = setTimer();
            }
        }

        printRecv(&synackpkt);
        if ((synackpkt.ack || synackpkt.dupack) && synackpkt.syn && synackpkt.acknum == (seqNum + 1) % MAX_SEQN)
        {
            seqNum = synackpkt.acknum;
            break;
        }
    }

    // =====================================
    // FILE READING VARIABLES

    char buf[PAYLOAD_SIZE];
    size_t m; // total number bytes we read from file

    // =====================================
    // CIRCULAR BUFFER VARIABLES

    struct packet ackpkt;         // the ACKing packet we recieve from the server
    struct packet pkts[WND_SIZE]; // packets for our circuilar window //! possibly init to some unreachable value
    int s = 0;                    // start of circular buffer
    int e = 0;                    // end of circular buffer
    // int full = 0;                 //! I don't think we will use this, rather use below to indicate empty
    bool zero_packets_in_transmission = true; // to differantite between s==e because no packets are in transmission and s==e
                                              //  when WND_SIZE packets are in transmission
    bool sent_entire_file = false;            // tells us when to stop sending
    bool first = true;                        // at first, any ACK we recieve allows us to advance the window
    // =====================================
    // Send First Packet (ACK containing payload) -- this packet is special, still part of handshake

    m = fread(buf, 1, PAYLOAD_SIZE, fp);
    if (ferror(fp))
    {
        perror("fread");
        exit(1);
    }

    buildPkt(&pkts[0], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, m, buf); // ack == 1 becaous eof the protocol
    printSend(&pkts[0], 0);
    sendto(sockfd, &pkts[0], PKT_SIZE, 0, (struct sockaddr *)&servaddr, servaddrlen);
    timer = setTimer();
    buildPkt(&pkts[0], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, m, buf); //? changed from dupack to ack

    seqNum += m; // update seqNum after send is convention
    e = 1;
    zero_packets_in_transmission = false;
    while (1) // must wait to see that the server got the hanshake.
    {
        n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *)&servaddr, (socklen_t *)&servaddrlen);
        if (n > 0) // if we recieve something -- it must be the correct ack
        {
            printRecv(&ackpkt);
            s = 1;
            zero_packets_in_transmission = true;
            break;
        }
        else if (isTimeout(timer))
        {
            assert(!zero_packets_in_transmission); // we are waiting on nothing

            printTimeout(&pkts[s]);

            // send all packets in our window
            for (int i = 0; i < calc_cur_windowsize(s, e); i++)
            {
                int cur_pkt_idx = (i + s) % WND_SIZE;
                printSend(&pkts[cur_pkt_idx], 1);
                sendto(sockfd, &pkts[cur_pkt_idx], PKT_SIZE, 0, (struct sockaddr *)&servaddr, servaddrlen);
            }

            // reset timer after timeout
            timer = setTimer();
        }
    }

    // =====================================
    // *** TODO: Implement the rest of reliable transfer in the client ***
    // Implement GBN for basic requirement or Selective Repeat to receive bonus

    // Note: the following code is not the complete logic. It only sends a
    //       single data packet, and then tears down the connection without
    //       handling data loss.
    //       Only for demo purpose. DO NOT USE IT in your final submission
    while (!(sent_entire_file && zero_packets_in_transmission)) // while we have more to send/ potentialy waiting for ACs.
    {
        // handle messages from server
        // printf("timer: %f, timeout: %i, sent_entire_file: %i,  zero_packets_in_transmission: %i\n", timer, isTimeout(timer), sent_entire_file, zero_packets_in_transmission);
        n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *)&servaddr, (socklen_t *)&servaddrlen); //! no double while... I think that's okay
        if (n > 0)
        {
            printRecv(&ackpkt); //! do we print duplicate ACKs?
            // advance window size if ack was for the first package in window, ignore if it was not (we only resend based on timeout).
            //! # this will change for SR
            // this is okay to do only becuae network does not re-order and it is not possible to recieve lower ACK number than expcted
            //! this statement does not catch duplicate ACKs, since have advanced the thing out
            // fprintf(stderr, "info print. Acknum: %i, what I think is dup: %i}\n", ackpkt.acknum, mod(pkts[s].seqnum - PAYLOAD_SIZE, MAX_SEQN));
            if (!first && (ackpkt.acknum == mod(pkts[s].seqnum - PAYLOAD_SIZE, MAX_SEQN))) // if ACK for previous
            {
                // fprintf(stderr, "recieved and discareded {out-of-order ACK: %i, I think I should recieve: %i, (s,e): (%i,%i) }\n", ackpkt.acknum, mod(pkts[s].seqnum - PAYLOAD_SIZE, MAX_SEQN), s, e);
                //fprintf(stderr, "recieved and discareded {out-of-order ACK: %i, I think I should recieve: %i, (s,e): (%i,%i) }\n", ackpkt.acknum, mod(pkts[s].seqnum - PAYLOAD_SIZE, MAX_SEQN), s, e);
            }
            else // the packet we recieve ACKs some future packet
            {

                // we have a problem if the server sends us double ACKS: we increment via the first, its no longer in the sequence, and we try to get it with the second.
                first = false;
                int num_ran = 0;
                while (ackpkt.acknum != (pkts[(s + num_ran) % WND_SIZE].seqnum + pkts[(s + num_ran) % WND_SIZE].length) % MAX_SEQN && num_ran <= WND_SIZE)
                {
                    //printf("acknum: %i, wanted: %i\n", ackpkt.acknum, (pkts[(s + num_ran) % WND_SIZE].seqnum + pkts[(s + num_ran) % WND_SIZE].length) % MAX_SEQN);
                    // assert(num_ran < WND_SIZE + 2);
                    num_ran++;
                    //! assrt that we neever advance past e
                    // s = (s + 1) % WND_SIZE;
                }
                if (num_ran < WND_SIZE)
                {
                    s = (s + num_ran + 1) % WND_SIZE;
                    timer = setTimer(); // I think this is how you restart timer
                    if (s == e)         // this is true because we just incremented s. If we just incremented e, we could have WND_SIZE packets in transmission
                    {
                        zero_packets_in_transmission = true;
                    }
                }
            }
        }

        // resend based on timeout
        if (isTimeout(timer))
        {
            assert(!zero_packets_in_transmission); // we are waiting on nothing

            printTimeout(&pkts[s]);

            // send all packets in our window
            for (int i = 0; i < calc_cur_windowsize(s, e); i++)
            {
                int cur_pkt_idx = (i + s) % WND_SIZE;
                printSend(&pkts[cur_pkt_idx], 1);
                sendto(sockfd, &pkts[cur_pkt_idx], PKT_SIZE, 0, (struct sockaddr *)&servaddr, servaddrlen);
            }

            // reset timer after timeout
            timer = setTimer();
        }
        // send new packet if window size permits
        if ((zero_packets_in_transmission || calc_cur_windowsize(s, e) < WND_SIZE) && !sent_entire_file)
        {
            // read from file and check for error
            m = fread(buf, 1, PAYLOAD_SIZE, fp);
            if (ferror(fp))
            {
                perror("fread");
                exit(1);
            }

            // if read anything, send it
            else if (m > 0) // send packet if there's something to read
            {
                buildPkt(&pkts[e], seqNum, 0 % MAX_SEQN, 0, 0, 0, 0, m, buf); // correct because: ack number is 0, seqnum updated after send
                printSend(&pkts[e], 0);
                sendto(sockfd, &pkts[e], PKT_SIZE, 0, (struct sockaddr *)&servaddr, servaddrlen);
                if (zero_packets_in_transmission) // previously 0, but we send again
                {
                    timer = setTimer();
                }

                // remember to build dupack packet
                //! this can be removed unless any flags need to be set. Right now, its not hurting
                buildPkt(&pkts[e], seqNum, 0 % MAX_SEQN, 0, 0, 0, 0, m, buf); //! I don't think DUPACK is required, but I am not sure

                // update vars for next send
                assert(s == e ? zero_packets_in_transmission : true); // if s==e we want to increment iff there are zero packets in transmission
                e = (e + 1) % WND_SIZE;
                seqNum = (seqNum + m) % MAX_SEQN; // update seqNum to the number of bytes we send
                zero_packets_in_transmission = false;
            }
            else // if didn't read anything and no error,  we have reached EOF (and have sent all data, although it may have no been recieved).
            {
                sent_entire_file = true;
            }
        }
    }

    // *** End of your client implementation ***
    fclose(fp);

    // =====================================
    // Connection Teardown: This procedure is provided to you directly and is
    // already working.

    struct packet finpkt, recvpkt;
    buildPkt(&finpkt, ackpkt.acknum, 0, 0, 1, 0, 0, 0, NULL);
    buildPkt(&ackpkt, (ackpkt.acknum + 1) % MAX_SEQN, (ackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, 0, NULL);

    printSend(&finpkt, 0);
    sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr *)&servaddr, servaddrlen);
    timer = setTimer();
    int timerOn = 1;

    double finTimer;
    int finTimerOn = 0;

    while (1)
    {
        while (1)
        {
            n = recvfrom(sockfd, &recvpkt, PKT_SIZE, 0, (struct sockaddr *)&servaddr, (socklen_t *)&servaddrlen);

            if (n > 0)
                break;
            if (timerOn && isTimeout(timer))
            {
                printTimeout(&finpkt);
                printSend(&finpkt, 1);
                if (finTimerOn)
                    timerOn = 0;
                else
                    sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr *)&servaddr, servaddrlen);
                timer = setTimer();
            }
            if (finTimerOn && isTimeout(finTimer))
            {
                close(sockfd);
                if (!timerOn)
                    exit(0);
            }
        }
        printRecv(&recvpkt);
        if ((recvpkt.ack || recvpkt.dupack) && recvpkt.acknum == (finpkt.seqnum + 1) % MAX_SEQN)
        {
            timerOn = 0;
        }
        else if (recvpkt.fin && (recvpkt.seqnum + 1) % MAX_SEQN == ackpkt.acknum)
        {
            printSend(&ackpkt, 0);
            sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *)&servaddr, servaddrlen);
            finTimer = setFinTimer();
            finTimerOn = 1;
            buildPkt(&ackpkt, ackpkt.seqnum, ackpkt.acknum, 0, 0, 0, 1, 0, NULL);
        }
    }
}
