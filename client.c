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

// new struct for packet window which keeps track of the pkt, its timer and its ACK status
struct WindowPacket
{
    struct packet pkt;
    double timer;
    bool isACKed;
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
// I (Lam) used this
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

// didn't use this one
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
    struct WindowPacket pkts[WND_SIZE]; // packets for our circuilar window 

    // we memset all the packets to have 65535 (max unsigned short value) cause we know seqnum can't be that
    memset(pkts, 65535, sizeof(pkts));

    // sendbase index (start) of our window 
    // sendBaseSegnum didn't really use  (just in case)
    int sendBase = 0;
    int sendBaseSegNum;

    // this index represents the next available packet to send in our window (pretty much end)
    // nextosendsegnum represents the segnum of the next packet we will send
    int nextToSend = 0;
    int nextToSendSegNum;

    // range from sendBase to nextToSend - 1 represents the range of packets in transmission 
        // if sendBase == nextToSend, have to do additional check to see if either zero or all in transmission
    // range from nextToSend to sendBase - 1 represents the range of packets we can send 
    
    bool zero_packets_in_transmission = true; // to differantite between sendBase==nextToSend because no packets are in transmission and sendBase==nextToSend
                                              //  when WND_SIZE packets are in transmission


    bool sent_entire_file = false;            // tells us when to stop sending
    
    // Lam didn't use this
    //bool first = true;                        // at first, any ACK we recieve allows us to advance the window
    // =====================================
    // Send First Packet (ACK containing payload) -- this packet is special, still part of handshake

    m = fread(buf, 1, PAYLOAD_SIZE, fp);
    if (ferror(fp))
    {
        perror("fread");
        exit(1);
    }

    // when we build packet now, we also set the timer and its ACK
    buildPkt(&pkts[0].pkt, seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, m, buf); // ack == 1 becaous eof the protocol
    pkts[0].timer = setTimer();
    pkts[0].isACKed = false;

    printSend(&pkts[0].pkt, 0);
    sendto(sockfd, &pkts[0].pkt, PKT_SIZE, 0, (struct sockaddr *)&servaddr, servaddrlen);

    //buildWinPkt(&pkts[0], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 0, 1, m, buf); // this is the packet we can use on duplicate, if the previous packet drops

    sendBaseSegNum = seqNum;

    // move nexttosend pointer by one cause it will be the next index in our window that will send stuff
    nextToSend++;
    nextToSendSegNum = (seqNum + m) % MAX_SEQN;

    zero_packets_in_transmission = false;

    // =====================================
    // *** TODO: Implement the rest of reliable transfer in the client ***
    // Implement GBN for basic requirement or Selective Repeat to receive bonus

    // Note: the following code is not the complete logic. It only sends a
    //       single data packet, and then tears down the connection without
    //       handling data loss.
    //       Only for demo purpose. DO NOT USE IT in your final submission
    while (!(sent_entire_file && zero_packets_in_transmission)) // while we have more to send/ potentialy waiting for ACs.
    {
        // this checks whether we still have packets in transmissions
        // if there is still an unacked packet, then that means there is still one in transmission
        // this part may be useless, but just for extra protection
        zero_packets_in_transmission = true;
        for (int i = 0; i < 10; i++) {
            if (pkts[i].isACKed == false) {
                zero_packets_in_transmission = false;
            }
        }

        
        // handle messages from server
        n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *)&servaddr, (socklen_t *)&servaddrlen); //! no double while... I think that's okay
        if (n > 0)
        {

            printRecv(&ackpkt); //! do we print duplicate ACKs? (Lam: I think so)

            

            // for (int j = 0, i = sendBase; j < iter; i = (i + 1) % WND_SIZE, j++) {
            //     int expectedACKnum = pkts[i].pkt.seqnum + pkts[i].pkt.length;
            //     if (ackpkt.acknum == expectedACKnum) {
            //         pkts[i].isACKed = true;
            //         pkts[i].timer = 777; //random num to indicate it is finished
            //         break;
            //     }
            // }

            // we will check if the received ACK acks any of the packets in our window
            // should be fine to check entire window cause we know we can't every have duplicate seqnums in window
                // cause window size n and seq num is >2n (as shown in lecture)
            // since we memset (initialize) the entire window to have impossible seqnum, we can be sure that first ACKs won't ACK any
            for (int i = 0; i < 10; i++) {
                int expectedACKnum = (pkts[i].pkt.seqnum + pkts[i].pkt.length) % MAX_SEQN;
                if (ackpkt.acknum == expectedACKnum) {
                    //!printf("ACKed at this index: %d\n", i);
                    pkts[i].isACKed = true;
                    pkts[i].timer = 777; //random num to indicate it is finished
                    break;
                }
            }

            // we can move sendbase consecutively as long as we have consecutive ACKed packets
            // we have i < 10 cause there may be a case where entire window is ACKED
            int i = 0;
            while (pkts[sendBase].isACKed && i < 10) {
                sendBase = (sendBase + 1) % WND_SIZE;
                sendBaseSegNum = (sendBaseSegNum + pkts[sendBase].pkt.length) % MAX_SEQN;
                i++;
            }
            //!printf("SendBase: %d\n", sendBase);
            //!printf("NextToSend: %d\n", nextToSend);
        }

        // we then go through our window to check for any packets that may have timers that ran out
        // we then send that packet again and reset timer

        int iter = calc_cur_windowsize(sendBase, nextToSend);


        for (int j = 0, i = sendBase; j < iter; i = (i + 1) % WND_SIZE, j++) {
            if (pkts[i].isACKed == false && isTimeout(pkts[i].timer)) {
                printTimeout(&pkts[i].pkt);
                printSend(&pkts[i].pkt, 1);
                sendto(sockfd, &pkts[i].pkt, PKT_SIZE, 0, (struct sockaddr *)&servaddr, servaddrlen);
                pkts[i].timer = setTimer();
            }
        }

        // we then check our window if we can send any more packets
        // we can if there is some gap between next to send and sendbase
        // at the end of this; next to send should always be equal to sendbase

        iter = calc_cur_windowsize(nextToSend, sendBase);
        if (iter == 10 && zero_packets_in_transmission == false) {
            iter = 0;
        }
        
        for (int j = 0; j < iter; nextToSend = (nextToSend + 1) % WND_SIZE, j++) {
            m = fread(buf, 1, PAYLOAD_SIZE, fp);
            if (ferror(fp))
            {
                perror("fread");
                exit(1);
            }
            else if (m > 0) // send packet if there's something to read
            {
                // build packet and send
                buildPkt(&pkts[nextToSend].pkt, nextToSendSegNum, 0 % MAX_SEQN, 0, 0, 0, 0, m, buf); // correct because: ack number is 0, seqnum updated after send
                pkts[nextToSend].timer = setTimer();
                pkts[nextToSend].isACKed = false;
                printSend(&pkts[nextToSend].pkt, 0);
                sendto(sockfd, &pkts[nextToSend].pkt, PKT_SIZE, 0, (struct sockaddr *)&servaddr, servaddrlen);

                // we update our nexttosend segnum
                nextToSendSegNum = (nextToSendSegNum + m) % MAX_SEQN;
                zero_packets_in_transmission = false;
            }
            else {
                sent_entire_file = true;
                break;
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
