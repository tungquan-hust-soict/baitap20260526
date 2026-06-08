#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

void get_param_value(const char *query, const char *param_name, char *output) {
    char search_str[32];
    sprintf(search_str, "%s=", param_name);
    char *pos = strstr(query, search_str);
    
    if (pos) {
        pos += strlen(search_str);
        int i = 0;
        while (*pos != '&' && *pos != ' ' && *pos != '\0' && *pos != '\r' && *pos != '\n') {
            output[i++] = *pos++;
        }
        output[i] = '\0';
    } else {
        output[0] = '\0';
    }
}

const char *html_home = 
    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
    "<html><body>"
    "<h1>May tinh cua toi</h1>"
    "<b>Thu nghiem GET:</b><br><br>"
    "<form action='/calc' method='GET'>"
    "So A: <input type='text' name='a'> <br><br>"
    "Phep tinh: "
    "<select name='op'>"
    "<option value='add'>Cong</option>"
    "<option value='sub'>Tru</option>"
    "<option value='mul'>Nhan</option>"
    "<option value='div'>Chia</option>"
    "</select> <br><br>"
    "So B: <input type='text' name='b'> <br><br>"
    "<input type='submit' value='Tinh ket qua'>"
    "</form>"
    "<br><hr><br>"
    "<b>Thu nghiem POST:</b><br><br>"
    "<form action='/calc' method='POST'>"
    "So A: <input type='text' name='a'> <br><br>"
    "Phep tinh: "
    "<select name='op'>"
    "<option value='add'>Cong</option>"
    "<option value='sub'>Tru</option>"
    "<option value='mul'>Nhan</option>"
    "<option value='div'>Chia</option>"
    "</select> <br><br>"
    "So B: <input type='text' name='b'> <br><br>"
    "<input type='submit' value='Tinh ket qua'>"
    "</form>"
    "</body></html>";

int main() {
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) {
        perror("socket() failed.\n");
        exit(1);
    }
    
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(8080);
    
    if (bind(listener, (struct sockaddr*) &addr, sizeof(addr))) {
        perror("bind() failed.\n");
        exit(1);
    }
    
    if (listen(listener, 10) == -1) {
        perror("listen() failed.\n");
        exit(1);
    }
    
    int num_processes = 8;
    printf("Server dang chay cong 8080...\n");
    
    for (int i = 0; i < num_processes; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork() failed");
            exit(1);
        } else if (pid == 0) {
            char buf[2048];
            while (1) {
                int client_fd = accept(listener, NULL, NULL);
                if (client_fd < 0) continue;
                
                int ret = recv(client_fd, buf, sizeof(buf) - 1, 0);
                if (ret <= 0) {
                    close(client_fd);
                    continue;
                }
                buf[ret] = 0;
                
                char method[16], uri[256];
                sscanf(buf, "%15s %255s", method, uri);
                
                if (strcmp(uri, "/") == 0) {
                    send(client_fd, html_home, strlen(html_home), 0);
                } 
                else if (strncmp(uri, "/calc", 5) == 0) {
                    char param_str[1024] = "";
                    
                    if (strcmp(method, "GET") == 0) {
                        char *query = strchr(uri, '?');
                        if (query) strcpy(param_str, query + 1);
                    } 
                    else if (strcmp(method, "POST") == 0) {
                        char *body = strstr(buf, "\r\n\r\n");
                        if (body) strcpy(param_str, body + 4);
                    }

                    char str_a[32], str_b[32], op[16];
                    get_param_value(param_str, "a", str_a);
                    get_param_value(param_str, "b", str_b);
                    get_param_value(param_str, "op", op);

                    if (strlen(str_a) > 0 && strlen(str_b) > 0 && strlen(op) > 0) {
                        double a = atof(str_a);
                        double b = atof(str_b);
                        double result = 0;
                        char symbol = '?';
                        char error_msg[128] = "";

                        if (strcmp(op, "add") == 0) { result = a + b; symbol = '+'; }
                        else if (strcmp(op, "sub") == 0) { result = a - b; symbol = '-'; }
                        else if (strcmp(op, "mul") == 0) { result = a * b; symbol = '*'; }
                        else if (strcmp(op, "div") == 0) { 
                            if (b != 0) { result = a / b; symbol = '/'; }
                            else { strcpy(error_msg, "Khong the chia cho 0!"); symbol = '/'; }
                        }

                        char response[2048];
                        if (strlen(error_msg) > 0) {
                            sprintf(response, 
                                "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                                "<html><body>"
                                "<h1>Loi: %s</h1><br>"
                                "<a href='/'>Quay lai</a>"
                                "</body></html>", error_msg);
                        } else {
                            sprintf(response, 
                                "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                                "<html><body>"
                                "<h1>Ket qua (%s):</h1>"
                                "<b>%g %c %g = %g</b><br><br>"
                                "<a href='/'>Quay lai trang chu</a>"
                                "</body></html>", 
                                method, a, symbol, b, result);
                        }
                        send(client_fd, response, strlen(response), 0);
                    } else {
                        char *bad_req = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nThieu tham so nha!";
                        send(client_fd, bad_req, strlen(bad_req), 0);
                    }
                } 
                else {
                    char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nTrang khong ton tai!";
                    send(client_fd, not_found, strlen(not_found), 0);
                }
                close(client_fd);
            }
            exit(0);
        }
    }
    
    wait(NULL);
    close(listener);
    return 0;
}