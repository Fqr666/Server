#include <stdio.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <process.h>
#include <stdint.h>
#include <sqlite3.h> // SQLite��ͷ�ļ�
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "sqlite3.lib") // ����ʵ���������SQLite������
#define INI_FILE "server.ini"

void handle_client_request(SOCKET client_sock);

#pragma warning(disable:4996)
#define PORT 12345

// Base64�����
const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// ��������
unsigned __stdcall client_handler(void* socket_desc);
void base64_decode(const char* input, int input_len, uint8_t* output);
int insert_data_into_db(const char* data);

int main() {
    WSADATA wsa;
    SOCKET server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    int c;
    int addr_len = sizeof(struct sockaddr_in);

    // ��ʼ��Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Failed to initialize Winsock\n");
        return 1;
    }

    // ����socket
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        return 1;
    }

    // ���÷�������ַ
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // �󶨷�������ַ
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed\n");
        closesocket(server_sock);
        return 1;
    }

    // ����
    listen(server_sock, 3);

    printf("Server listening on port %d...\n", PORT);



    // ���ܺʹ�������
    c = sizeof(struct sockaddr_in);
    while ((client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &c)) != INVALID_SOCKET) {
        printf("Connection accepted\n");

        // ����ÿ�����ӵĿͻ����߳�
        HANDLE client_thread = (HANDLE)_beginthreadex(NULL, 0, &client_handler, (void*)&client_sock, 0, NULL);
        if (client_thread == NULL) {
            printf("Failed to create client thread\n");
            closesocket(client_sock);
            continue;
        }



        printf("Client connected: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Handle client request
        handle_client_request(client_sock);



        CloseHandle(client_thread); // �ر��߳̾��
    }

    if (client_sock == INVALID_SOCKET) {
        printf("Accept failed\n");
        return 1;
    }


    closesocket(server_sock);
    WSACleanup();

    return 0;
}

// �ͻ��˴����̺߳���
unsigned __stdcall client_handler(void* socket_desc) {
    SOCKET client_sock = *((SOCKET*)socket_desc);
    char client_message[2048] = { 0 };
    int recv_size;

    while ((recv_size = recv(client_sock, client_message, sizeof(client_message), 0)) > 0) {
        client_message[recv_size] = '\0';
        printf("Received from client: %s\n", client_message);

        // Base64����
        uint8_t decoded_data[2048] = { 0 };
        base64_decode(client_message, strlen(client_message), decoded_data);




        // �������������ǰ����һ���ֽڱ�ʶ���ļ������ֶ�
        char data_type = decoded_data[0];
        unsigned char* actual_data = decoded_data + 1; // ʵ�����ݲ���

        if (data_type == 'F') {
            // ������ļ�����
            FILE* file = fopen(actual_data, "wb"); // ���ļ���д�����������
            if (file) {
                fwrite(actual_data, 1, strlen((const char*)actual_data) - 4, file); // д���ļ�
                fclose(file);
                printf("�ļ��ѱ���\n");
            }
            else {
                printf("�޷����ļ�\n");
            }
        }
        else {
            // �洢���ļ�
            FILE* file = fopen("received_data.txt", "ab"); // ׷��д��ģʽ�����Ը����������
            if (file) {
                fwrite(decoded_data, 1, strlen((char*)decoded_data), file);
                fclose(file);
            }
            else {
                printf("Failed to open file for writing\n");
            }

            // �洢��SQLite���ݿ�
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






// Base64���뺯��
void base64_decode(const char* input, int input_len, uint8_t* output) {
    int i = 0, j = 0;
    uint32_t buf = 0;
    int buf_len = 0;

    while (i < input_len) {
        char c = input[i++];
        if (c == '=') break;

        // Ѱ���ַ���base64���е�����
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

// �������ݵ�SQLite���ݿ�
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
