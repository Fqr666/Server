#include <stdio.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <process.h>
#include <stdint.h>
#include <sqlite3.h> // SQLite库头文件
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "sqlite3.lib") // 根据实际情况调整SQLite库名称
#define INI_FILE "server.ini"

void handle_client_request(SOCKET client_sock);

#pragma warning(disable:4996)
#define PORT 12345

// Base64解码表
const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 函数声明
unsigned __stdcall client_handler(void* socket_desc);
void base64_decode(const char* input, int input_len, uint8_t* output);
int insert_data_into_db(const char* data);

int main() {
    WSADATA wsa;
    SOCKET server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    int c;
    int addr_len = sizeof(struct sockaddr_in);

    // 初始化Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Failed to initialize Winsock\n");
        return 1;
    }

    // 创建socket
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        return 1;
    }

    // 设置服务器地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // 绑定服务器地址
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed\n");
        closesocket(server_sock);
        return 1;
    }

    // 监听
    listen(server_sock, 3);

    printf("Server listening on port %d...\n", PORT);



    // 接受和处理连接
    c = sizeof(struct sockaddr_in);
    while ((client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &c)) != INVALID_SOCKET) {
        printf("Connection accepted\n");

        // 处理每个连接的客户端线程
        HANDLE client_thread = (HANDLE)_beginthreadex(NULL, 0, &client_handler, (void*)&client_sock, 0, NULL);
        if (client_thread == NULL) {
            printf("Failed to create client thread\n");
            closesocket(client_sock);
            continue;
        }



        printf("Client connected: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Handle client request
        handle_client_request(client_sock);



        CloseHandle(client_thread); // 关闭线程句柄
    }

    if (client_sock == INVALID_SOCKET) {
        printf("Accept failed\n");
        return 1;
    }


    closesocket(server_sock);
    WSACleanup();

    return 0;
}

// 客户端处理线程函数
unsigned __stdcall client_handler(void* socket_desc) {
    SOCKET client_sock = *((SOCKET*)socket_desc);
    char client_message[2048] = { 0 };
    int recv_size;

    while ((recv_size = recv(client_sock, client_message, sizeof(client_message), 0)) > 0) {
        client_message[recv_size] = '\0';
        printf("Received from client: %s\n", client_message);

        // Base64解码
        uint8_t decoded_data[2048] = { 0 };
        base64_decode(client_message, strlen(client_message), decoded_data);




        // 假设解码后的数据前面有一个字节标识是文件还是字段
        char data_type = decoded_data[0];
        unsigned char* actual_data = decoded_data + 1; // 实际数据部分

        if (data_type == 'F') {
            // 如果是文件数据
            FILE* file = fopen(actual_data, "wb"); // 打开文件以写入二进制数据
            if (file) {
                fwrite(actual_data, 1, strlen((const char*)actual_data) - 4, file); // 写入文件
                fclose(file);
                printf("文件已保存\n");
            }
            else {
                printf("无法打开文件\n");
            }
        }
        else {
            // 存储到文件
            FILE* file = fopen("received_data.txt", "ab"); // 追加写入模式，可以根据需求调整
            if (file) {
                fwrite(decoded_data, 1, strlen((char*)decoded_data), file);
                fclose(file);
            }
            else {
                printf("Failed to open file for writing\n");
            }

            // 存储到SQLite数据库
            if (insert_data_into_db((char*)decoded_data) != 0) {
                printf("Failed to insert data into SQLite database\n");
            }
        }
    }
    if (recv_size == 0) {
        printf("Need to reboot\n");
    }
    else if (recv_size == SOCKET_ERROR) {
        printf("Receive failed\n");
    }

    closesocket(client_sock);
    //return 0;

}






// Base64解码函数
void base64_decode(const char* input, int input_len, uint8_t* output) {
    int i = 0, j = 0;
    uint32_t buf = 0;
    int buf_len = 0;

    while (i < input_len) {
        char c = input[i++];
        if (c == '=') break;

        // 寻找字符在base64表中的索引
        const char* pos = strchr(base64_table, c);
        if (pos == NULL) continue;

        buf = (buf << 6) | (pos - base64_table);
        buf_len += 6;

        if (buf_len >= 8) {
            output[j++] = (buf >> (buf_len - 8)) & 0xFF;
            buf_len -= 8;
        }
    }
}

// 插入数据到SQLite数据库
int insert_data_into_db(const char* data) {
    sqlite3* db;
    char* err_msg = 0;
    int rc;

    rc = sqlite3_open("data.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    char sql_stmt[2048];
    sprintf(sql_stmt, "INSERT INTO data_table (data_column) VALUES ('%s');", data);

    rc = sqlite3_exec(db, sql_stmt, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 1;
    }

    sqlite3_close(db);
    return 0;
}

// Handle client request
void handle_client_request(SOCKET client_sock) {
    char buffer[1024] = { 0 };
    FILE* fp;
    char version[50];

    // Receive client request (assuming a simple request)
    if (recv(client_sock, buffer, sizeof(buffer), 0) == SOCKET_ERROR) {
        printf("Receive failed\n");
        return;
    }

    // Assume the request is a simple string "get_version"
    if (strcmp(buffer, "get_version") != 0) {
        printf("Invalid request\n");
        return;
    }

    // Read version from INI file
    fp = fopen(INI_FILE, "r");
    if (fp == NULL) {
        printf("Failed to open INI file\n");
        return;
    }

    if (fgets(version, sizeof(version), fp) == NULL) {
        printf("Failed to read version from INI file\n");
        fclose(fp);
        return;
    }

    fclose(fp);

    // Send version to client
    if (send(client_sock, version, strlen(version), 0) == SOCKET_ERROR) {
        printf("Send failed\n");
        return;
    }

    printf("Sent version to client: %s\n", version);
}
