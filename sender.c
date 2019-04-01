#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
typedef struct {
    int length;
    int seqNumber;
    int ackNumber;
    int fin;
    int syn;
    int ack;
} header;

typedef struct{
    header head;
    char data[1000];
} segment;

void setIP(char *dst, char *src) {
    if(strcmp(src, "0.0.0.0") == 0 || strcmp(src, "local") == 0
       || strcmp(src, "localhost") == 0) {//最後多補一個0
        sscanf("127.0.0.1", "%s", dst);
    } else {
        sscanf(src, "%s", dst);
    }
}
int get_size(FILE *fp){
    fseek(fp, 0, SEEK_END);
    int tmp = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    return tmp;
}
//functions
segment pack_seg(int fin, int seqNumber, int length, segment tmp);
void send_packet(int index, int fin);
static void handle(int signum);
void check_ack(segment packet);
//variables
char IP[2][50];
int port[2];
int mysocket;
struct sockaddr_in sender, agent, tmp_addr;
socklen_t sender_size, agent_size, tmp_size;
FILE *file;
int seg_sent = 0;
int window = 3, threshold = 16;
int win_head = 1, win_tail = 0;
int ack_cnt = 1;
int timeout = 0;
int max_seg = 1;
int total_seg;


int main(int argc, char *argv[]){
    //set signal
    struct sigaction sa_usr;
    sa_usr.sa_handler = handle;
    sigaction(SIGALRM, &sa_usr, NULL);
    
    if(argc != 5){
        fprintf(stderr,"用法: %s <agent IP> <agent port> <sender port> <file path>\n", argv[0]);
        fprintf(stderr, "例如: ./sender local 8888 8887 input.txt\n");
        exit(1);
    }
    else{
        setIP(IP[0], argv[1]);//agent IP
        setIP(IP[1], "local");
        sscanf(argv[2], "%d", &port[0]);//agent port
        sscanf(argv[3], "%d", &port[1]);
        file = fopen(argv[4], "r");
        assert(file != NULL);
    }
    //Set up UDP connection
    mysocket = socket(PF_INET, SOCK_DGRAM, 0);
    //configure sender
    sender.sin_family = AF_INET;
    sender.sin_port = htons(port[1]);
    sender.sin_addr.s_addr = inet_addr(IP[1]);
    memset(sender.sin_zero, '\0', sizeof(sender.sin_zero));
    //configure agent
    agent.sin_family = AF_INET;
    agent.sin_port = htons(port[0]);
    agent.sin_addr.s_addr = inet_addr(IP[0]);
    memset(agent.sin_zero, '\0', sizeof(agent.sin_zero));
    //bind the socket
    bind(mysocket,(struct sockaddr *)&sender,sizeof(sender));
    sender_size = sizeof(sender);
    agent_size = sizeof(agent);
    tmp_size = sizeof(tmp_addr);
    //send packet and recv ACK
    int file_size = get_size(file);
    total_seg = (file_size % 1000 == 0)? file_size / 1000 : file_size / 1000 + 1;
    segment tmp_seg;
    printf("file size:%d total seg:%d\n", file_size, total_seg);
    printf("agent IP:%s sender IP:%s agent port:%d sender port:%d\n", IP[0], IP[1], port[0], port[1]);
    
    while(1){
        //send segments
        if(total_seg != seg_sent){
            win_tail = (win_head + window - 1 > total_seg)? total_seg : win_head + window - 1; //set tail of the window
            fseek(file, (win_head-1) * 1000, SEEK_SET);//set the tape
            for(int cnt = win_head;cnt <= win_tail; cnt++){
                if(cnt > total_seg){//last packet is sent
                    win_tail = cnt;
                    break;
                }
                send_packet(cnt, 0);
                if(cnt == max_seg){//first time sending
                    printf("send    data    #%2d,    winSize = %d\n", cnt, window);
                    max_seg++;
                }
                else if(cnt < max_seg){//resend
                    printf("resnd   data    #%2d,    winSize = %d\n", cnt, window);
                }
            }
        }
        else{//send fin
            win_tail = seg_sent + 1;
            send_packet(seg_sent + 1, 1);
            printf("send    fin\n");
        }
    //recv ACKs
        alarm(1);
        while(ack_cnt <= win_tail){
            int recv_size = recvfrom(mysocket, &tmp_seg, sizeof(tmp_seg), 0, (struct sockaddr *)&agent, &agent_size);
            if(recv_size > 0){
                if(tmp_seg.head.fin == 1){//finished
                    printf("recv    finack\n");
                    fclose(file);
                    return 0;
                }
                printf("recv    ack     #%2d\n", tmp_seg.head.ackNumber);
                check_ack(tmp_seg);
            }
        }
        alarm(0);
        
        window = (window < threshold)? 2 * window : window + 1;
        win_head = ack_cnt;
    }
    return 0;
}
segment pack_seg(int fin, int seqNumber, int length, segment tmp){
    tmp.head.length = length;
    tmp.head.ack = tmp.head.syn = tmp.head.ackNumber = 0;
    tmp.head.fin = fin;
    tmp.head.seqNumber = seqNumber;
    return tmp;
}
void send_packet(int index, int fin){
    segment s_tmp;
    if(fin == 0){
        int num_read = fread(s_tmp.data, 1, 1000, file);
        s_tmp = pack_seg(0, index, num_read, s_tmp);
        sendto(mysocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
    }
    else{
        s_tmp = pack_seg(1, index, 0, s_tmp);
        sendto(mysocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
    }
    return;
}
void check_ack(segment packet){
    if(packet.head.ackNumber == ack_cnt){//correct ACK
        ack_cnt++;
        seg_sent++;
    }
    return;
}
static void handle(int signum){
    threshold = ((window / 2) > 1)? (window / 2) : 1;
    printf("time    out,           threshold = %d\n", threshold);
    window = 1;
    win_head = ack_cnt;
    win_tail = (win_head + window - 1 > total_seg)? ack_cnt : win_head + window - 1;
    //resend
    fseek(file, (win_head - 1) * 1000, SEEK_SET);
    for(int cnt = win_head;cnt <= win_tail; cnt++){
        if(cnt > total_seg)
            break;
        send_packet(cnt, 0);
        printf("resnd   data    #%2d,    winSize = %d\n", cnt, window);
    }
    alarm(1);
    return;
}
