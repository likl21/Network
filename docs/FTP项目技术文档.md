# FTP 服务器项目技术文档

> 清华大学软件学院计算机网络课程大作业
> 语言：C，平台：Linux，基于 POSIX socket 接口

---

## 一、项目概述

本项目用 C 语言从零实现了一个符合 RFC 959 规范的 FTP 服务端与客户端。

- **控制连接**：基于 TCP，默认端口 21，传输 FTP 指令与响应码
- **数据连接**：独立 TCP 连接，用于文件内容和目录列表的传输
- **并发模型**：服务端使用 `fork()` 多进程，每个客户端连接独占一个子进程

---

## 二、目录结构

```
Network/
├── client/
│   ├── bin/client          # 已编译的客户端可执行文件
│   └── src/
│       ├── client.c        # 客户端核心逻辑
│       ├── client.h        # 头文件（全局变量 + 函数声明）
│       └── makefile
└── server/
    └── src/
        ├── server.c        # 服务端核心逻辑
        ├── server.h        # 头文件（全局变量 + 函数声明）
        └── makefile
```

---

## 三、编译与运行

### 编译

```bash
# 服务端
cd server/src && make

# 客户端
cd client/src && make
```

### 运行

```bash
# 启动服务端（端口可选，默认 21；根目录可选，默认 /tmp/）
./server -port 2121 -root /home/user/ftp/

# 启动客户端，连接指定 IP 和端口
./client 127.0.0.1 2121
```

---

## 四、FTP 协议核心概念

### 4.1 控制连接 vs 数据连接

| 连接类型 | 描述 | 端口 |
|----------|------|------|
| 控制连接 | 传输命令和响应，持续保持 | 服务端 21 |
| 数据连接 | 仅在传输文件/目录列表时建立，传完即关 | 协商端口 |

### 4.2 主动模式（PORT）

客户端在本地开一个端口并监听，把地址通过 `PORT` 命令告诉服务端，服务端主动来连接。

```
客户端                  服务端
  |-- PORT h1,h2,h3,h4,p1,p2 -->|  （告知数据端口）
  |<-- 200 PORT successful ------|
  |-- LIST / RETR / STOR ------->|
  |<-- 150 Opening connection ----|
  服务端主动 connect 客户端数据端口
  [数据传输]
  |<-- 226 Transfer complete ----|
```

**PORT 参数格式**：`h1,h2,h3,h4,p1,p2`
- IP = `h1.h2.h3.h4`
- 端口 = `p1 * 256 + p2`

### 4.3 被动模式（PASV）

服务端开一个端口并监听，客户端主动来连接。适合客户端在 NAT/防火墙后的场景。

```
客户端                  服务端
  |-- PASV ---------------------->|
  |<-- 227 Entering Passive Mode  |  （含服务端数据端口）
  客户端主动 connect 服务端数据端口
  |-- LIST / RETR / STOR ------->|
  [数据传输]
  |<-- 226 Transfer complete ----|
```

**227 响应格式**：`227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)`

---

## 五、支持的 FTP 命令

| 命令 | 功能 | 状态码 |
|------|------|--------|
| `USER <name>` | 提交用户名 | 331 |
| `PASS <password>` | 提交密码 | 230 / 501 |
| `SYST` | 查询系统类型 | 215 |
| `TYPE I` | 设置二进制传输模式 | 200 |
| `PWD` | 显示当前工作目录 | 257 |
| `CWD <dir>` | 切换工作目录 | 257 / 550 |
| `CDUP` | 返回上级目录 | 257 / 550 |
| `MKD <dir>` | 创建目录 | 257 / 550 |
| `RMD <dir>` | 删除目录（含子项） | 250 / 550 |
| `DELE <file>` | 删除文件 | 250 / 550 |
| `RNFR <old>` | 指定重命名源路径 | 350 / 550 |
| `RNTO <new>` | 执行重命名 | 250 / 502 |
| `PORT h1,...,p2` | 指定主动模式数据端口 | 200 |
| `PASV` | 进入被动模式 | 227 |
| `LIST` | 列出当前目录内容 | 150+226 / 425 |
| `RETR <file>` | 从服务端下载文件 | 150+226 / 502 / 425 |
| `STOR <file>` | 向服务端上传文件 | 150+226 / 425 |
| `QUIT` / `ABOR` | 断开连接（打印总传输量） | 221 |

---

## 六、服务端架构详解

### 6.1 main 函数流程

```
main()
  ├── ftp_server_argPort()     解析 -port 参数，默认 21
  ├── ftp_server_argRoot()     解析 -root 参数，默认 /tmp/
  ├── socket() / bind() / listen()   创建监听套接字
  └── while(1)
        ├── accept()           阻塞等待客户端连接
        ├── fork()             创建子进程
        │    ├── 子进程: close(server_sock)
        │    │           send "220 Anonymous FTP server ready."
        │    │           ftp_server_loop()      处理该客户端所有命令
        │    │           close(client_sock)
        │    │           return 0
        │    └── 父进程: close(client_sock)     继续 accept 下一个
```

**关键设计**：父进程 `close(client_sock)` 确保文件描述符不泄漏；子进程 `close(server_sock)` 避免误接受新连接。

### 6.2 ftp_server_loop

```c
void ftp_server_loop() {
    while (1) {
        recv_data(client_sock, sentence);    // 接收一行命令
        int answer = ftp_server_response(sentence);  // 分发处理
        if (!answer) break;                  // QUIT/ABOR 返回 0 则退出
    }
}
```

### 6.3 ftp_server_response 命令分发

大型 if-else 链，通过 `get_command_arg()` 解析命令和参数后逐一匹配。返回值 1=继续，0=断开连接。

### 6.4 数据连接建立

**主动模式（PORT）**：
```
服务端收到 RETR/STOR/LIST 后调用 connect_server(&file_connect_sock, file_addr, file_port)
主动连接到客户端指定的数据地址和端口
```

**被动模式（PASV）**：
```
服务端收到 PASV 后创建 file_listen_sock 并绑定随机端口（20000~65535），
发送 227 响应通知客户端端口号；
收到 RETR/STOR/LIST 后调用 accept(file_listen_sock) 等待客户端来连接
```

### 6.5 文件传输实现

```c
// 发送文件：每次读 8190 字节（略小于 MAX_SIZE=8192，留余量），直到 fread 返回 0
void send_file(const int sock, const char* filename) {
    FILE* f = fopen(filename, "rb");
    do { n = fread(buffer, 1, 8190, f); send(sock, buffer, n, 0); } while(n > 0);
}

// 接收文件：循环 recv 直到对端关闭连接（n==0）
void recv_file(const int sock, const char* filename) {
    FILE* f = fopen(filename, "wb");
    do { n = recv(sock, buffer, MAX_SIZE, 0); fwrite(buffer, 1, n, f); } while(n > 0);
}
```

**传输完成后关闭数据套接字**，客户端的 `recv_file` 通过 `recv` 返回 0 感知到连接关闭，从而结束接收。

### 6.6 transfer_size 统计

全局变量，每次 RETR 或 STOR 完成后累加文件大小，在 QUIT/ABOR 时输出总量：
```
221 Goodbye. All the file you transfer(RETR & STOR) is XXX bytes
```

---

## 七、客户端架构详解

### 7.1 main 函数

```c
int main(int argc, char **argv) {
    connect_server(&client_sock, argv[1], atoi(argv[2]));  // 连接服务端
    recv_response(reply);      // 接收 220 欢迎消息
    client_status = CONN;
    while (client_loop() == 0);  // 循环处理用户命令
    close(client_sock);
}
```

### 7.2 client_loop 交互循环

1. 打印提示符 `ftp client>`
2. 读取用户输入（`fgets`）
3. 解析命令和参数（`get_command_arg`）
4. 根据命令执行对应逻辑

### 7.3 数据连接时序

**RETR（下载）主动模式**：
```
客户端: create_socket() 监听数据端口
客户端: 发 PORT 命令 → 发 RETR 命令
客户端: accept() 等待服务端数据连接
客户端: recv_response() 接收 150
客户端: recv_file() 接收文件内容
客户端: recv_response() 接收 226
```

**RETR（下载）被动模式**：
```
客户端: 发 PASV → 解析 227 响应中的 IP/端口
客户端: 发 RETR 命令
客户端: connect_server() 主动连服务端数据端口
客户端: recv_response() 接收 150
客户端: recv_file() 接收文件内容
客户端: recv_response() 接收 226
```

### 7.4 client_status 状态机

记录当前会话所处状态，决定 RETR/STOR/LIST 时如何建立数据连接（PORT 还是 PASV）。

| 值 | 含义 |
|----|------|
| CONN(0) | 已连接，未登录 |
| USER(1) | 已发送用户名 |
| PASS(2) | 已完成登录 |
| PORT(3) | 已发送 PORT 命令 |
| PASV(4) | 已发送 PASV 命令 |
| RETR/STOR/... | 上一次操作类型 |

---

## 八、关键工具函数

### 8.1 PORT/PASV 参数解析

```c
// 从 "h1,h2,h3,h4,p1,p2" 解析出 "h1.h2.h3.h4"
void get_address(char* addr, const char* str);

// 从 "h1,h2,h3,h4,p1,p2" 解析出端口号 = p1*256 + p2
int get_port(const char* str);

// 从 PASV 227 响应整行中提取 "(h1,h2,h3,h4,p1,p2)"
void get_address_port_client(const char* buffer, char *arg);
```

### 8.2 send_infomation

所有 FTP 响应/命令均以 `\r\n` 结尾（RFC 959 要求）：
```c
void send_infomation(const int sock, const char* buffer) {
    strcpy(sentence, buffer);
    strcat(sentence, "\r\n");
    send(sock, sentence, strlen(sentence), 0);
}
```

### 8.3 recv_data

接收一条消息并去掉末尾 `\r\n`：
```c
void recv_data(const int sock, char* sentence) {
    recv(sock, buffer, MAX_SIZE, 0);
    // 去掉尾部 \r\n
    while (len > 0 && (buffer[len-1] == '\r' || buffer[len-1] == '\n')) len--;
    buffer[len] = '\0';
    strcpy(sentence, buffer);
}
```

---

## 九、多进程并发模型

```
父进程
  │
  ├── accept() → client_sock_1 → fork() → 子进程1 (处理 client1)
  │                                        close(server_sock)
  │                                        ftp_server_loop()
  │
  ├── accept() → client_sock_2 → fork() → 子进程2 (处理 client2)
  │
  ...
```

**每个子进程独立拥有一套全局变量**（`cur_path`、`client_status`、`rename_path` 等），`fork()` 时完整复制父进程地址空间，保证会话完全隔离。

**父进程不等待子进程退出**（没有调用 `wait()`），会产生僵尸进程（zombie）。这是课程作业中的简化处理。

---

## 十、认证机制

```c
int check_user(char *user_name, char *user_pass) {
    if(strcmp(user_name, "anonymous") == 0) return 1;
    return 1;   // 任何用户名/密码均返回 1（接受）
}
```

实际上无验证，任意用户名密码均可登录，满足课程匿名 FTP 的需求。

---

## 十一、常见面试问答

### Q1: 为什么 FTP 要用两个连接？

**控制连接**负责传命令和状态码，在整个会话期间保持不断开。**数据连接**仅在实际传输时建立，传完立即关闭。这样可以：
- 避免文件传输中断导致会话失效
- 支持传输期间继续发控制命令（如 ABOR）
- 减少空闲时不必要的端口占用

### Q2: 主动模式 vs 被动模式区别？

| | 主动模式 PORT | 被动模式 PASV |
|--|--------------|--------------|
| 数据连接发起方 | 服务端主动连客户端 | 客户端主动连服务端 |
| 客户端需开放端口 | 是（防火墙常阻断） | 否 |
| 服务端需开放端口 | 否 | 是（随机高端口） |
| 适用场景 | 局域网 | NAT/防火墙后的客户端 |

### Q3: 如何处理并发连接？

使用 `fork()` 系统调用，每接受一个客户端就创建子进程专门处理。父进程继续 `accept()` 等待新连接。子进程退出时会话结束，父进程不 `wait()` 导致僵尸进程，生产环境应用 `SIGCHLD` 信号处理。

### Q4: 文件传输如何知道传输结束？

发送方传完文件后 `close()` 数据套接字；接收方 `recv()` 返回 0 表示对端关闭，循环退出。这利用了 TCP 的 FIN 机制。

### Q5: PORT 格式如何解析端口号？

`PORT h1,h2,h3,h4,p1,p2` 中端口 = `p1 * 256 + p2`，这是 RFC 959 规定的编码方式（高字节 * 256 + 低字节）。

### Q6: 被动模式的端口范围？

代码中 `file_port = 20000 + rand() % 45536`，范围约 20000~65535，避开知名端口（<1024）。

### Q7: 全局变量在多进程中安全吗？

安全。`fork()` 后父子进程各有独立的地址空间副本（Copy-on-Write），修改互不影响。这也是多进程相比多线程的优势——不需要加锁。

### Q8: socket() 调用参数含义？

```c
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
// AF_INET: IPv4 地址族
// SOCK_STREAM: 流式套接字（TCP）
// IPPROTO_TCP: 明确使用 TCP 协议
```

### Q9: bind 为什么服务端要用，客户端 create_socket 也要用？

服务端 `bind(21)` 是为了固定控制连接端口。客户端 `create_socket` 中的 `bind(INADDR_ANY, port)` 是在主动模式下绑定数据监听端口，然后通过 `PORT` 命令把该端口告知服务端。

### Q10: htons/htonl 的作用？

将主机字节序转换为网络字节序（大端序）。网络协议规定使用大端序，x86 是小端序，必须转换；`ntohs` 是逆操作。

---

## 十二、已知局限（课程作业简化）

1. `recv_data` 单次 `recv` 可能收到不完整的行（TCP 流式语义），生产环境需要循环读到 `\r\n`
2. 路径拼接使用字符串 `strcat`，无越界保护
3. `check_user` 无真实认证
4. 父进程不 `wait()` 子进程，产生僵尸进程
5. `get_file_size` 打开文件后未关闭（`fclose` 缺失，已在修复版中添加）
6. PASV 端口使用 `rand()` 无种子，每次启动端口序列相同
7. 使用 `system()` 执行 shell 命令（`mkdir`、`rm -r`、`mv`），存在命令注入风险
