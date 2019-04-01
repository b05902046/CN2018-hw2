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
segment pack_seg(int fin, int ackNumber);
void send_packet(int index, int fin);
int check_ack(segment packet);
void flush(segment buf[32], int buf_cnt);
//variables
char IP[2][50];
int port[2];
int mysocket;
struct sockaddr_in receiver, agent, tmp_addr;
socklen_t receiver_size, agent_size, tmp_size;
FILE *file;
int ack_cnt = 1;
int buf_cnt = 0;
int total_seg;
//signal

int main(int argc, char *argv[]){
    //set arguments
    if(argc != 5){
        fprintf(stderr,"用法: %s <agent IP> <agent port> <receiver port> <file path>\n", argv[0]);
        fprintf(stderr, "例如: ./receiver local 8888 8889 out.txt\n");
        exit(1);
    }
    else{
        setIP(IP[0], argv[1]);
        setIP(IP[1], "local");
        sscanf(argv[2], "%d", &port[0]);
        sscanf(argv[3], "%d", &port[1]);
        file = fopen(argv[4], "w");
        assert(file != NULL);
    }
    
    //Set up UDP connection
    mysocket = socket(PF_INET, SOCK_DGRAM, 0);
    //configure receiver
    receiver.sin_family = AF_INET;
    receiver.sin_port = htons(port[1]);
    receiver.sin_addr.s_addr = inet_addr(IP[1]);
    memset(receiver.sin_zero, '\0', sizeof(receiver.sin_zero));
    //configure agent
    agent.sin_family = AF_INET;
    agent.sin_port = htons(port[0]);
    agent.sin_addr.s_addr = inet_addr(IP[0]);
    memset(agent.sin_zero, '\0', sizeof(agent.sin_zero));
    //bind the socket
    bind(mysocket,(struct sockaddr *)&receiver,sizeof(receiver));
    receiver_size = sizeof(receiver);
    agent_size = sizeof(agent);
    tmp_size = sizeof(tmp_addr);
    //send packet and recv ACK
    segment tmp_seg;
    segment buf[32];
    printf("agent IP:%s receiver IP:%s agent port:%d receiver port:%d\n", IP[0], IP[1], port[0], port[1]);
    
    int buf_cnt = 0;
    
    while(1){
        int recv_size = recvfrom(mysocket, &tmp_seg, sizeof(tmp_seg), 0, (struct sockaddr *)&agent, &agent_size);
        if(tmp_seg.head.fin == 1){//send finack
            flush(buf, buf_cnt);
            send_packet(ack_cnt, 1);
            printf("send    finack\n");
            fclose(file);
            return 0;
        }
        else{
            int correct = check_ack(tmp_seg);
            if(correct == 0){
                printf("drop    data    #%2d\n", tmp_seg.head.seqNumber);
                send_packet(ack_cnt - 1, 0);
                printf("send    ack     #%2d\n", ack_cnt);
                continue;
            }
            else if(buf_cnt == 32){
                printf("flush\n");
                flush(buf, buf_cnt);
                buf_cnt = 0;
                printf("drop    data    #%2d\n", ack_cnt);
            }
            else{
                buf[buf_cnt] = tmp_seg;
                buf_cnt++;
                printf("recv    data    #%2d\n", tmp_seg.head.seqNumber);
                send_packet(ack_cnt, 0);
                printf("send    ack     #%2d\n", ack_cnt);
                ack_cnt++;
            }
        }
        
    }
    return 0;
}
segment pack_seg(int fin, int ackNumber){
    segment tmp;
    //set the flag
    tmp.head.length = 0;
    tmp.head.ack = 1;
    tmp.head.syn = 0;
    tmp.head.ackNumber = ackNumber;
    tmp.head.fin = fin;
    tmp.head.seqNumber = 0;
    return tmp;
}
void send_packet(int index, int fin){
    segment s_tmp = pack_seg(fin, index);
    sendto(mysocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
    return;
}
int check_ack(segment packet){
    if(packet.head.seqNumber == ack_cnt){//correct ACK
        return 1;
    }
    return 0;
}
void flush(segment buf[32], int buf_cnt){
    for(int cnt = 0; cnt < buf_cnt; cnt++){
        fwrite(buf[cnt].data, 1, buf[cnt].head.length, file);
    }
    return;
}

