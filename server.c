#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <assert.h>
// =====================================

#define RTO 500000       /* timeout in microseconds */
#define HDR_SIZE 12      /* header size*/
#define PKT_SIZE 524     /* total packet size */
#define PAYLOAD_SIZE 512 /* PKT_SIZE - HDR_SIZE */
#define WND_SIZE 10      /* window size*/
#define MAX_SEQN 25601   /* number of sequence numbers [0-25600] */

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

int isTimeout(double end)
{
    struct timeval s;
    gettimeofday(&s, NULL);
    double start = (double)s.tv_sec + (double)s.tv_usec / 1000000;
    return ((end - start) < 0.0);
}

// =====================================

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        perror("ERROR: incorrect number of arguments\n"
               "Please use command \"./server <PORT> <ISN>\"\n");
        exit(1);
    }

    unsigned int servPort = atoi(argv[1]);
    unsigned short initialSeqNum = atoi(argv[2]);

    // =====================================
    // Socket Setup

    int sockfd;
    struct sockaddr_in servaddr, cliaddr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(servPort);
    memset(servaddr.sin_zero, '\0', sizeof(servaddr.sin_zero));

    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
    {
        perror("bind() error");
        exit(1);
    }

    int cliaddrlen = sizeof(cliaddr);

    // NOTE: We set the socket as non-blocking so that we can poll it until
    //       timeout instead of getting stuck. This way is not particularly
    //       efficient in real programs but considered acceptable in this
    //       project.
    //       Optionally, you could also consider adding a timeout to the socket
    //       using setsockopt with SOq_RCVTIMEO instead.
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    // =====================================

    for (int i = 1;; i++)
    {
        // =====================================
        // Establish Connection: This procedure is provided to you directly and
        // is already working.

        // For testing, we reset the sequence number to the initial sequence number (ISN) for all connections.
        unsigned short seqNum = initialSeqNum;

        int n;

        FILE *fp;

        struct packet synpkt, synackpkt, ackpkt;

        while (1)
        {
            n = recvfrom(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr *)&cliaddr, (socklen_t *)&cliaddrlen);
            if (n > 0)
            {
                printRecv(&synpkt);
                if (synpkt.syn)
                    break;
            }
        }

        unsigned short cliSeqNum = (synpkt.seqnum + 1) % MAX_SEQN; // next message from client should have this sequence number

        buildPkt(&synackpkt, seqNum, cliSeqNum, 1, 0, 1, 0, 0, NULL);

        while (1)
        {
            printSend(&synackpkt, 0);
            sendto(sockfd, &synackpkt, PKT_SIZE, 0, (struct sockaddr *)&cliaddr, cliaddrlen);

            while (1)
            {
                n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *)&cliaddr, (socklen_t *)&cliaddrlen);
                if (n > 0)
                {
                    printRecv(&ackpkt);
                    if (ackpkt.seqnum == cliSeqNum && ackpkt.ack && ackpkt.acknum == (synackpkt.seqnum + 1) % MAX_SEQN)
                    {

                        int length = snprintf(NULL, 0, "%d", i) + 6; // I did not think of this pattern for the sequencial path writing -- pretty cool!
                        char *filename = malloc(length);
                        snprintf(filename, length, "%d.file", i);

                        fp = fopen(filename, "w");
                        free(filename);
                        if (fp == NULL)
                        {
                            perror("ERROR: File could not be created\n");
                            exit(1);
                        }

                        fwrite(ackpkt.payload, 1, ackpkt.length, fp);

                        seqNum = ackpkt.acknum;
                        cliSeqNum = (ackpkt.seqnum + ackpkt.length) % MAX_SEQN;

                        buildPkt(&ackpkt, seqNum, cliSeqNum, 0, 0, 1, 0, 0, NULL);
                        printSend(&ackpkt, 0);
                        sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *)&cliaddr, cliaddrlen);

                        break;
                    }
                    else if (ackpkt.syn) // ! what does this corrospond to?
                    {
                        buildPkt(&synackpkt, seqNum, (synpkt.seqnum + 1) % MAX_SEQN, 1, 0, 0, 1, 0, NULL);
                        break;
                    }
                }
            }

            if (!ackpkt.syn)
                break;
        }

        // *** TODO: Implement the rest of reliable transfer in the server ***
        // Implement GBN for basic requirement or Selective Repeat to receive bonus

        // Note: the following code is not the complete logic. It only expects
        //       a single data packet, and then tears down the connection
        //       without handling data loss.
        //       Only for demo purpose. DO NOT USE IT in your final submission
        struct packet recvpkt;

        while (1)
        { // the exit condition is recieving a finished which breaks the loop

            memset(&recvpkt, 0, sizeof(recvpkt));
            n = recvfrom(sockfd, &recvpkt, PKT_SIZE, 0, (struct sockaddr *)&cliaddr, (socklen_t *)&cliaddrlen);
            if (n > 0)
            {
                // printf("in the n>0\n");
                printRecv(&recvpkt);

                if (recvpkt.fin)
                {
                    //  printf("in the fin\n");

                    cliSeqNum = (cliSeqNum + 1) % MAX_SEQN;

                    buildPkt(&ackpkt, seqNum, cliSeqNum, 0, 0, 1, 0, 0, NULL);
                    printSend(&ackpkt, 0);
                    sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *)&cliaddr, cliaddrlen);

                    break;                            // break on finish is the exit condition
                }                                     // the first byte the client sends == the first bit that we expect.
                else if (recvpkt.seqnum == cliSeqNum) // I am pretty sure this condition is correct
                {
                    // printf("in the correct\n");

                    // write payload to file and check errors
                    // fprintf(stderr, "writing %i bytes to file %i\n", recvpkt.length, i);
                    fwrite(recvpkt.payload, 1, recvpkt.length, fp); // write contents to file. This will append over what we previously got
                    if (ferror(fp))
                    {
                        perror("fwrite");
                        exit(1);
                    }
                    assert(recvpkt.length > 0);
                    assert((cliSeqNum + recvpkt.length) % MAX_SEQN != cliSeqNum);

                    // update sequence num (byte we expect next based on what we have recieved)
                    cliSeqNum = (cliSeqNum + recvpkt.length) % MAX_SEQN; // update cliSeqNum after we recieve and write to file

                    // send packet and print
                    buildPkt(&ackpkt, seqNum, cliSeqNum, 0, 0, 1, 0, 0, NULL);
                    printSend(&ackpkt, 0);
                    sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *)&cliaddr, cliaddrlen);
                }
                else
                {
                    // printf("in the else\n");

                    // server resends the ACK he already has on innapropriate packege. I forgot to implement this at commit 21e5a
                    buildPkt(&ackpkt, seqNum, cliSeqNum, 0, 0, 0, 1, 0, NULL);
                    printSend(&ackpkt, 0);
                    sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *)&cliaddr, cliaddrlen);
                    // fprintf(stderr, "recieved and discareded duplicate pckt of {starting byte: %i, len: %i, cliSeqNum: %i}\n", recvpkt.seqnum, recvpkt.length, cliSeqNum);
                }
            }
        }

        // *** End of your server implementation ***

        fclose(fp);
        // =====================================
        // Connection Teardown: This procedure is provided to you directly and
        // is already working.

        struct packet finpkt, lastackpkt;
        buildPkt(&finpkt, seqNum, 0, 0, 1, 0, 0, 0, NULL);
        buildPkt(&ackpkt, seqNum, cliSeqNum, 0, 0, 0, 1, 0, NULL);

        printSend(&finpkt, 0);
        sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr *)&cliaddr, cliaddrlen);
        double timer = setTimer();

        while (1)
        {
            while (1)
            {
                n = recvfrom(sockfd, &lastackpkt, PKT_SIZE, 0, (struct sockaddr *)&cliaddr, (socklen_t *)&cliaddrlen);
                if (n > 0)
                    break;

                if (isTimeout(timer))
                {
                    printTimeout(&finpkt);
                    printSend(&finpkt, 1);
                    sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr *)&cliaddr, cliaddrlen);
                    timer = setTimer();
                }
            }

            printRecv(&lastackpkt);
            if (lastackpkt.fin)
            {

                printSend(&ackpkt, 0);
                sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *)&cliaddr, cliaddrlen);

                printSend(&finpkt, 1);
                sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr *)&cliaddr, cliaddrlen);
                timer = setTimer();

                continue;
            }
            if ((lastackpkt.ack || lastackpkt.dupack) && lastackpkt.acknum == (finpkt.seqnum + 1) % MAX_SEQN)
                break;
        }

        seqNum = lastackpkt.acknum;
    }
}
