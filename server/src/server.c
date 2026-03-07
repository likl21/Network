#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <memory.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include "server.h"
#define MAX_SIZE 8192

/* fix: 实现 get_pwd —— 原 server.h 声明但 server.c 中缺失实现，导致链接报错 */
void get_pwd(int socket, char *info) {
    char path[1000];
    getcwd(path, sizeof(path));
    sprintf(info, "257 \"%s\" is current directory.", path);
}

/* fix: 实现 get_list_server —— 原 server.h 声明但 server.c 中缺失实现，导致链接报错 */
void get_list_server(const int sock) {
    char buffer[MAX_SIZE];
    FILE *fp = popen("ls -l", "r");
    if (fp == NULL) {
        const char *err = "ls failed\r\n";
        send(sock, err, strlen(err), 0);
        return;
    }
    int n;
    while ((n = fread(buffer, 1, MAX_SIZE, fp)) > 0) {
        send(sock, buffer, n, 0);
    }
    pclose(fp);
}

int ftp_server_response(const char* sentence) {
    char buffer[MAX_SIZE];
    char tmp[100];
    char filename[100];
    char user_name[100];

    /* 获取用户输入命令 */
    get_command_arg(sentence, client_cmd, client_arg);
    printf("%s %s \n", client_cmd, client_arg);

    /* 登录 */
    if(strcmp(client_cmd, "USER") == 0){
        strcpy(user_name, client_arg);
        send_infomation(client_sock, "331 Guest login ok");
        client_status = USER;
    }
    else if(strcmp(client_cmd, "PASS") == 0){
        if(check_user(user_name, client_arg) == 1){
            send_infomation(client_sock, "230 Guest login ok, access restrictions apply.");
            client_status = PASS;
        }
        else{
            send_infomation(client_sock, "501 Please check your password.");
        }
    }
    /* 系统以及传输数据类型 */
    else if(strcmp(client_cmd, "SYST") == 0){
        send_infomation(client_sock, "215 UNIX Type: L8");
        client_status = PASS;
    }
    else if(strcmp(client_cmd, "TYPE") == 0){
        send_infomation(client_sock, "200 Type set to I.");
        client_status = PASS;
    }
    /* 主动模式 */
    else if(strcmp(client_cmd, "PORT") == 0){
        get_address(file_addr, client_arg);
        file_port = get_port(client_arg);
        client_status = PORT;
        send_infomation(client_sock, "200 PORT command successful.");
    }
    /* 被动模式 */
    else if(strcmp(client_cmd, "PASV") == 0){
        char send_add[100];
        file_port = 20000 + rand() % 45536;

        /* 获取被动模式下本机 IP 地址 */
        struct hostent *ht = NULL;
        char host[128];
        gethostname(host, sizeof(host));
        ht = gethostbyname(host);
        for(int i=0;;i++){
            if(ht->h_addr_list[i] != NULL){
                sprintf(file_addr, "%s", inet_ntoa(*(struct in_addr*)(ht->h_addr_list[i])));
            } else{
                break;
            }
        }
        printf("file_addr IP:%s",file_addr);

        /* 将 IP 中的点换成逗号 */
        strcpy(send_add, file_addr);
        int len_fileIP = strlen(send_add);
        for(int i=0;i<len_fileIP;i++){
            if(send_add[i] == '.')
                send_add[i] = ',';
        }

        /* 被动模式下服务端新建文件传输 socket */
        struct sockaddr_in file_addr_in;
        if((file_listen_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){
            perror("socket");
            exit(EXIT_FAILURE);
        }
        memset(&file_addr_in, 0, sizeof(struct sockaddr_in));
        file_addr_in.sin_family = AF_INET;
        file_addr_in.sin_port = htons(file_port);
        file_addr_in.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(file_listen_sock, (struct sockaddr*)&file_addr_in,
            sizeof(file_addr_in)) == -1) {
            printf("Error bind(): %s(%d)\n", strerror(errno), errno);
            exit(EXIT_FAILURE);
        }
        if (listen(file_listen_sock, 10) == -1) {
            printf("Error listen(): %s(%d)\n", strerror(errno), errno);
            exit(EXIT_FAILURE);
        }

        /* 发送 227 响应到 client */
        sprintf(buffer, "227 Entering Passive Mode (%s,", send_add);
        sprintf(tmp, "%d", file_port / 256);
        strcat(buffer, tmp);
        strcat(buffer, ",");
        sprintf(tmp, "%d", file_port % 256);
        strcat(buffer, tmp);
        strcat(buffer, ")");
        send_infomation(client_sock, buffer);
        client_status = PASV;
    }
    /* RETR 下载文件 */
    else if(strcmp(client_cmd, "RETR") == 0){
        if (client_status == PORT) {
            char info[1000];
            send_infomation(client_sock, "150 Opening BINARY mode data connection.");
            connect_server(&file_connect_sock, file_addr, file_port);
            strcpy(filename, cur_path);
            strcat(filename, client_arg);
            int file_size = get_file_size(filename);
            if(file_size < 0){
                send_infomation(client_sock, "502 File not found");
            }
            else{
                send_file(file_connect_sock, filename);
                close(file_connect_sock);
                sprintf(info, "226 Transfer complete (%d bytes).", file_size);
                transfer_size += file_size;
                send_infomation(client_sock, info);
            }
        }
        else if(client_status == PASV) {
            char info[1000];
            send_infomation(client_sock, "150 Opening BINARY mode data connection.");
            if ((file_connect_sock = accept(file_listen_sock, NULL, NULL)) == -1) {
                printf("Error accept(): %s(%d)\n", strerror(errno), errno);
            }
            strcpy(filename, cur_path);
            strcat(filename, client_arg);
            int file_size = get_file_size(filename);
            if(file_size < 0){
                send_infomation(client_sock, "502 File not found");
            }
            else{
                send_file(file_connect_sock, filename);
                close(file_connect_sock);
                close(file_listen_sock);
                sprintf(info, "226 Transfer complete (%d bytes).", file_size);
                transfer_size += file_size;
                send_infomation(client_sock, info);
            }
        }
        else{
            send_infomation(client_sock, "425 No TCP connection was established.");
        }
        client_status = RETR;
    }
    /* STOR 上传文件 */
    else if(strcmp(client_cmd, "STOR") == 0){
        if (client_status == PORT) {
            char info[1000];
            connect_server(&file_connect_sock, file_addr, file_port);
            send_infomation(client_sock, "150 Opening BINARY mode data connection.");
            strcpy(filename, cur_path);
            strcat(filename, client_arg);
            recv_file(file_connect_sock, filename);
            int file_size = get_file_size(filename);
            close(file_connect_sock);
            sprintf(info, "226 Transfer complete (%d bytes).", file_size);
            transfer_size += file_size;
            send_infomation(client_sock, info);
        }
        else if (client_status == PASV) {
            char info[1000];
            if ((file_connect_sock = accept(file_listen_sock, NULL, NULL)) == -1) {
                printf("Error accept(): %s(%d)\n", strerror(errno), errno);
            }
            send_infomation(client_sock, "150 Opening BINARY mode data connection.");
            strcpy(filename, cur_path);
            strcat(filename, client_arg);
            recv_file(file_connect_sock, filename);
            int file_size = get_file_size(filename);
            sprintf(info, "226 Transfer complete (%d bytes).", file_size);
            close(file_connect_sock);
            close(file_listen_sock);
            transfer_size += file_size;
            send_infomation(client_sock, info);
        }
        else{
            send_infomation(client_sock, "425 No TCP connection was established.");
        }
        client_status = STOR;
    }
    /* MKD 创建目录 */
    else if(strcmp(client_cmd, "MKD") == 0){
        char path[100];
        char info[100];
        char control[200];
        memset(path, 0, 100);
        strcpy(path, cur_path);
        strcat(path, client_arg);
        sprintf(control, "mkdir %s", path);
        int r = system(control);
        if(r == 0){
            sprintf(info, "257 \"%s\" dictionary created.", path);
            send_infomation(client_sock, info);
        }
        else{
            sprintf(info, "550 \"%s\": file or dictionary already exists.", path);
            send_infomation(client_sock, info);
        }
    }
    /* CWD 改变当前目录 */
    else if(strcmp(client_cmd, "CWD") == 0){
        char info[200];
        memset(info, 0, 200);
        if(chdir(client_arg) >= 0){
            getcwd(cur_path,1000);
            sprintf(info, "257 CWD command successful \"%s\" is current directory.", cur_path);
            strcat(cur_path, "/");
            send_infomation(client_sock, info);
        }
        else{
            sprintf(info, "550 \"%s\": No such file or directory.", client_arg);
            send_infomation(client_sock, info);
        }
    }
    /* fix: CDUP 返回上级目录 —— 原代码缺少此命令处理 */
    else if(strcmp(client_cmd, "CDUP") == 0){
        char info[200];
        memset(info, 0, 200);
        if(chdir("..") >= 0){
            getcwd(cur_path, 1000);
            sprintf(info, "257 CDUP command successful \"%s\" is current directory.", cur_path);
            strcat(cur_path, "/");
            send_infomation(client_sock, info);
        }
        else{
            send_infomation(client_sock, "550 CDUP failed.");
        }
    }
    /* PWD 输出当前目录 */
    else if(strcmp(client_cmd, "PWD") == 0){
        char info[1000];
        memset(info, 0, sizeof(info));
        get_pwd(client_sock, info);
        send_infomation(client_sock, info);
        client_status = PWD;
    }
    /* LIST 列出当前目录文件 */
    else if(strcmp(client_cmd, "LIST") == 0){
        if (client_status == PORT) {
            send_infomation(client_sock, "150 Opening BINARY mode data connection.");
            connect_server(&file_connect_sock, file_addr, file_port);
            get_list_server(file_connect_sock);
            close(file_connect_sock);
            send_infomation(client_sock, "226 LIST all the files.");
        }
        else if(client_status == PASV) {
            send_infomation(client_sock, "150 Opening BINARY mode data connection.");
            if ((file_connect_sock = accept(file_listen_sock, NULL, NULL)) == -1) {
                printf("Error accept(): %s(%d)\n", strerror(errno), errno);
            }
            get_list_server(file_connect_sock);
            close(file_connect_sock);
            close(file_listen_sock);
            send_infomation(client_sock, "226 LIST all the files.");
        }
        else{
            send_infomation(client_sock, "425 No TCP connection was established.");
        }
    }
    /* fix: DELE 删除文件 —— 原代码缺少此命令处理（客户端已实现，服务端缺失） */
    else if(strcmp(client_cmd, "DELE") == 0){
        char info[200];
        char path[200];
        strcpy(path, cur_path);
        strcat(path, client_arg);
        if(remove(path) == 0){
            sprintf(info, "250 \"%s\": file deleted.", path);
            send_infomation(client_sock, info);
        }
        else{
            sprintf(info, "550 \"%s\": no such file or permission denied.", path);
            send_infomation(client_sock, info);
        }
    }
    /* RMD 删除目录 */
    else if(strcmp(client_cmd, "RMD") == 0){
        char info[100];
        char path[100];
        char control[200];
        strcpy(path, cur_path);
        strcat(path, client_arg);
        sprintf(control, "rm -r %s", path);
        int r = system(control);
        if(r == 0){
            sprintf(info, "250 \"%s\": dictionary is deleted.", path);
            send_infomation(client_sock, info);
        }
        else{
            sprintf(info, "550 \"%s\": dictionary can't be deleted or not found", path);
            send_infomation(client_sock, info);
        }
    }
    /* RNFR/RNTO 重命名 */
    else if(strcmp(client_cmd, "RNFR") == 0){
        char info[200];
        strcpy(rename_path, cur_path);
        strcat(rename_path, client_arg);
        /* fix: 原代码执行 mv X X（同路径），改为用 access() 检查文件是否存在 */
        if(access(rename_path, F_OK) == 0){
            sprintf(info, "350 \"%s\": File exists, ready for destination name.", client_arg);
            send_infomation(client_sock, info);
        }
        else{
            sprintf(info, "550 \"%s\": no such file or directory.", client_arg);
            send_infomation(client_sock, info);
            memset(rename_path, 0, sizeof(rename_path));
        }
    }
    else if(strcmp(client_cmd, "RNTO") == 0){
        char info[200];
        char control[400];
        char name_path[200];
        strcpy(name_path, cur_path);
        strcat(name_path, client_arg);
        sprintf(control, "mv %s %s", rename_path, name_path);
        int r = system(control);
        if(r == 0){
            sprintf(info, "250 File \"%s\" renamed to \"%s\".", rename_path, name_path);
            send_infomation(client_sock, info);
            memset(rename_path, 0, sizeof(rename_path));
        }
        else{
            send_infomation(client_sock, "502 Can't rename the file or directory.");
        }
    }
    /* 退出并关闭连接 */
    else if((strcmp(client_cmd, "QUIT") == 0) || (strcmp(client_cmd, "ABOR") == 0)){
        char info[1000];
        sprintf(info, "221 Goodbye. All the file you transfer(RETR & STOR) is %d bytes", transfer_size);
        send_infomation(client_sock, info);
        return 0;
    }
    /* 鲁棒性：异常输入处理 */
    else{
        send_infomation(client_sock, "502 Invalid command.");
    }
    return 1;
}

void send_file(const int sock, const char* filename) {
    FILE* f = fopen(filename, "rb");
    if(!f){
        printf("Not the File\n");
        return;
    }
    char buffer[MAX_SIZE];
    int n;
    do {
        n = fread(buffer, 1, 8190, f);
        send(sock, buffer, n, 0);
    } while (n > 0);
    fclose(f);
}

void recv_file(const int sock, const char* filename) {
    FILE* f = fopen(filename, "wb");
    int n;
    char buffer[MAX_SIZE];
    do {
        n = recv(sock, buffer, MAX_SIZE, 0);
        fwrite(buffer, 1, n, f);
    } while (n > 0);
    fclose(f);
}

int get_file_size(char *filename){
    FILE *f = fopen(filename, "r");
    if(!f){
        return -1;
    }
    fseek(f, 0L, SEEK_END);
    int file_size = ftell(f);
    fclose(f);
    return file_size;
}

int connect_server(int* client_sock, const char *server_IP, const int port) {
    struct sockaddr_in server_addr;
    if ((*client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        printf("Error socket(): %s(%d)\n", strerror(errno), errno);
        return 1;
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_IP, &server_addr.sin_addr) <= 0) {
        printf("Error inet_pton(): %s(%d)\n", strerror(errno), errno);
        return 1;
    }
    if (connect(*client_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Error connect(): %s(%d)\n", strerror(errno), errno);
        return 1;
    }
    return 0;
}

void get_address(char* addr, const char* str){
    char* ptr;
    char tmp[100];
    strcpy(tmp, str);
    addr[0] = '\0';
    ptr = strtok(tmp, ",");
    strcat(addr, ptr);
    strcat(addr, ".");
    ptr = strtok(NULL, ",");
    strcat(addr, ptr);
    strcat(addr, ".");
    ptr = strtok(NULL, ",");
    strcat(addr, ptr);
    strcat(addr, ".");
    ptr = strtok(NULL, ",");
    strcat(addr, ptr);
    printf("PORT addr:%s\n",addr);
}

int get_port(const char* str) {
    char* arg = NULL;
    char temp[100];
    int port = 0;
    strcpy(temp, str);
    arg = strtok(temp, ",");
    arg = strtok(NULL, ",");
    arg = strtok(NULL, ",");
    arg = strtok(NULL, ",");
    arg = strtok(NULL, ",");
    port = atoi(arg) * 256;
    arg = strtok(NULL, ",");
    port += atoi(arg);
    printf("PORT port:%d\n",port);
    if(port < 1024)
        port = 2048;
    return port;
}

int check_user(char *user_name, char *user_pass){
    if(strcmp(user_name, "anonymous") == 0){
        return 1;
    }
    return 1;
}

void get_command_arg(const char* buffer, char *cmd, char *arg) {
    char temp[1000];
    memset(temp, 0, 1000);
    strcpy(temp, buffer);
    char *flag = NULL;
    flag = strtok(temp, " ");
    strcpy(cmd, flag);
    flag = strtok(NULL, " ");
    if(flag != NULL){
        strcpy(arg, flag);
    }
}

void ftp_server_argPort(int* port, const int argc, const char **argv) {
    int i;
    *port = 21;
    for (i = 0; i < argc; i ++) {
        if (strcmp(argv[i], "-port") == 0) {
            *port = atoi(argv[i + 1]);
            break;
        }
    }
}

void ftp_server_argRoot(char* root, const int argc, const char **argv) {
    int i;
    strcpy(root, "/tmp/");
    for (i = 0; i < argc; i ++) {
        if (strcmp(argv[i], "-root") == 0) {
            strcpy(root, argv[i + 1]);
            break;
        }
    }
    if (root[strlen(root) - 1] != '/')
        strcat(root, "/");
}

void recv_data(const int sock, char* sentence) {
    char buffer[MAX_SIZE];
    unsigned int len;
    memset(buffer, 0, sizeof(buffer));
    recv(sock, buffer, MAX_SIZE, 0);
    printf("recv: /%s\n", buffer);
    len = strlen(buffer);
    while (len > 0 && (buffer[len - 1] == '\r' || buffer[len - 1] == '\n')) len --;
    buffer[len] = '\0';
    strcpy(sentence, buffer);
}

void send_infomation(const int sock, const char* buffer) {
    char sentence[MAX_SIZE];
    strcpy(sentence, buffer);
    strcat(sentence, "\r\n");
    send(sock, sentence, strlen(sentence), 0);
    printf("sent: /%s\n", buffer);
}

void ftp_server_loop(){
    while (1) {
        memset(sentence, 0, sizeof(sentence));
        memset(client_cmd, 0, 1000);
        memset(client_arg, 0, 1000);
        recv_data(client_sock, sentence);
        int answer = ftp_server_response(sentence);
        if(!answer){
            break;
        }
    }
}

int main(const int argc, const char **argv) {
    pid_t pid;

    ftp_server_argPort(&server_port, argc, argv);
    ftp_server_argRoot(cur_path, argc, argv);

    struct sockaddr_in addr;

    if ((server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        printf("Error socket(): %s(%d)\n", strerror(errno), errno);
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("server IP:%s\nport:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

    if (bind(server_sock, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        printf("Error bind(): %s(%d)\n", strerror(errno), errno);
        return 1;
    }

    if (listen(server_sock, 10) == -1) {
        printf("Error listen(): %s(%d)\n", strerror(errno), errno);
        return 1;
    }

    while (1) {
        if ((client_sock = accept(server_sock, NULL, NULL)) == -1) {
            printf("Error accept(): %s(%d)\n", strerror(errno), errno);
            break;
        }

        pid = fork();
        if (pid < 0) {
            printf("Error forking child process");
        } else if (pid == 0) {
            close(server_sock);
            client_status = CONN;
            send_infomation(client_sock, "220 Anonymous FTP server ready.");
            ftp_server_loop();
            close(client_sock);
            return 0;
        }
        close(client_sock);
    }
    close(server_sock);
    return 0;
}
