#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>

#define PORT 8080
#define BUFFER_SIZE 4096

void url_decode(const char *src, char *dest) {
    while (*src) {
        if ((*src == '%') && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], '\0' };
            *dest++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dest++ = ' ';
            src++;
        } else {
            *dest++ = *src++;
        }
    }
    *dest = '\0';
}

const char* get_mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    
    if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".c") == 0) return "text/plain";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";
    if (strcmp(ext, ".wav") == 0) return "audio/wav";
    if (strcmp(ext, ".mp4") == 0) return "video/mp4";
    if (strcmp(ext, ".pdf") == 0) return "application/pdf";
    
    return "application/octet-stream";
}

void send_404(int client_fd) {
    const char* response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<h1>404 Not Found</h1>";
    send(client_fd, response, strlen(response), 0);
}

void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    
    int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }

    char method[16] = {0}, path[1024] = {0}, protocol[16] = {0};
    
    if (sscanf(buffer, "%15s %1023s %15s", method, path, protocol) != 3) {
        close(client_fd);
        return;
    }

    if (strcmp(method, "GET") != 0) {
        close(client_fd);
        return;
    }

    char decoded_path[1024];
    url_decode(path, decoded_path);

    char real_path[2048];
    if (strcmp(decoded_path, "/") == 0) {
        strcpy(real_path, "."); 
    } else {
        snprintf(real_path, sizeof(real_path), ".%s", decoded_path);
    }

    struct stat path_stat;
    if (stat(real_path, &path_stat) != 0) {
        send_404(client_fd);
        close(client_fd);
        return;
    }

    if (S_ISDIR(path_stat.st_mode)) {
        DIR* dir = opendir(real_path);
        if (!dir) {
            send_404(client_fd);
            close(client_fd);
            return;
        }

        char response_header[1024];
        snprintf(response_header, sizeof(response_header), 
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/html; charset=utf-8\r\n"
                 "Connection: close\r\n\r\n");
        send(client_fd, response_header, strlen(response_header), 0);

        char html_body[BUFFER_SIZE];
        snprintf(html_body, sizeof(html_body), "<html><head><title>Directory %s</title></head><body><h1>Danh sach cac file %s</h1><hr><ul>", decoded_path, decoded_path);
        send(client_fd, html_body, strlen(html_body), 0);

        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0) continue;

            char sub_path[4096];
            snprintf(sub_path, sizeof(sub_path), "%s/%s", real_path, entry->d_name);
            struct stat sub_stat;
            stat(sub_path, &sub_stat);

            char link_path[2048];
            if (strcmp(decoded_path, "/") == 0) {
                snprintf(link_path, sizeof(link_path), "/%s", entry->d_name);
            } else {
                snprintf(link_path, sizeof(link_path), "%s/%s", decoded_path, entry->d_name);
            }

            char item_html[4096];
            if (S_ISDIR(sub_stat.st_mode)) {
                snprintf(item_html, sizeof(item_html), "<li><b><a href=\"%s\">%s/</a></b></li>", link_path, entry->d_name);
            } else {
                snprintf(item_html, sizeof(item_html), "<li><i><a href=\"%s\">%s</a></i></li>", link_path, entry->d_name);
            }
            send(client_fd, item_html, strlen(item_html), 0);
        }
        closedir(dir);

        const char* html_footer = "</ul><hr></body></html>";
        send(client_fd, html_footer, strlen(html_footer), 0);

    } 
    else if (S_ISREG(path_stat.st_mode)) {
        int fd = open(real_path, O_RDONLY);
        if (fd < 0) {
            send_404(client_fd);
            close(client_fd);
            return;
        }

        const char* mime_type = get_mime_type(real_path);
        char response_header[1024];
        snprintf(response_header, sizeof(response_header), 
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %ld\r\n"
                 "Connection: close\r\n\r\n", mime_type, path_stat.st_size);
        send(client_fd, response_header, strlen(response_header), 0);

        int bytes_read_file;
        char file_buffer[BUFFER_SIZE];
        while ((bytes_read_file = read(fd, file_buffer, sizeof(file_buffer))) > 0) {
            send(client_fd, file_buffer, bytes_read_file, 0);
        }
        close(fd);
    }

    close(client_fd);
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("HTTP File Server is running on http://localhost:%d\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd >= 0) {
            handle_client(client_fd);
        }
    }

    close(server_fd);
    return 0;
}