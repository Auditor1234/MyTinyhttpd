#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#define __USE_MISC
#include <sys/stat.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>

#define ISspace(x) isspace((int)(x))
#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

#define STDIN 0
#define STDOUT 1
#define STDERR 2

void error_die(const char *sc) {
    perror(sc);
    exit(1);
}

int startup(unsigned int *port) {
    int httpd = 0;
    int on = 1;
    struct sockaddr_in name;

    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if(httpd == -1)
        error_die("socket");
    
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)
        error_die("setsockopt failed");
    
    if(bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    
    if(*port == 0) {
        socklen_t namelen = sizeof(name);
        if(getsockname(httpd, (struct sockaddr*)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }
    if(listen(httpd, 5) < 0)
        error_die("listen");

    return httpd;
}

int get_content(int sock, char *buf, int size) {
    int i = 0;
    char c = '\0';
    int n = 0;
    printf("begin read.\n");
    while(recv(sock, &c, 1, 0) != 0) {
        if(c == '\r') {
            buf[i++] = '\\';
            buf[i++] = 'r';
        }else if(c == '\n') {
            buf[i++] = '\\';
            buf[i++] = 'n';
            buf[i++] = '\n';
            n++;
        }else {
            buf[i++] = c;
            n = 0;
        }
        if(n == 2) {
            break;
        }
    }
    printf("end read.\n");
    buf[i] = '\0';
    return i;
}

int get_line(int sock, char *buf, int size) {
    int i = 0;
    char c = '\0';
    int n;

    while((i < size - 1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0);
        if(n > 0) {
            if(c == '\r') {
                // 在套接字缓冲区中保留\n
                n = recv(sock, &c, 1, MSG_PEEK);
                if((n > 0) && (c == '\n')) {
                    int ret = recv(sock, &c, 1, 0);
                }else {
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        }else {
            c = '\n';
        }
    }
    buf[i] = '\0';
    return i;
}

void headers(int client, const char *filename) {
    char buf[1024];
    (void)filename;
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    // strcpy(buf, SERVER_STRING);
    // send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

void cat(int client, FILE *resource) {
    char buf[1024];

    while(!feof(resource)) {
        fgets(buf, sizeof(buf), resource);
        send(client, buf, strlen(buf), 0);
    }
}

void serve_file(int client, const char *filename) {
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];
    
    buf[0] = 'A'; buf[1] = '\0';
    while((numchars > 0) && strcmp("\n", buf))
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if(resource == NULL)
        error_die(client);
    else {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

void execute_cgi(int client, const char *path,
                const char *method, const char *query_string) {
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;
    
    buf[0] = 'A'; buf[1] = '\0';
    if(strcasecmp(method, "GET") == 0)
        while((numchars > 0) && strcmp("\n", buf))
            numchars = get_line(client, buf, sizeof(buf));
    else if(strcasecmp(method, "POST") == 0) {
        numchars = get_line(client, buf, sizeof(buf));
        while((numchars > 0) && strcmp("\n", buf)) {
            buf[15] = '\0';
            if(strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        if(content_length == -1) {
            printf("content length error");
            return;
        }
    }
    printf("content length = %d\n", content_length);
    if(pipe(cgi_output) < 0) {
        error_die("pipe output");
    }
    if(pipe(cgi_input) < 0) {
        error_die("pipe input");
    }

    if((pid = fork()) < 0) {
        error_die("fork");
    }
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    if(pid == 0) {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], STDOUT);
        dup2(cgi_input[0], STDIN);
        close(cgi_output[0]);
        close(cgi_input[1]);
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if(strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }else {
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        execl(path, NULL);
        exit(0);
    }else {
        close(cgi_output[1]);
        close(cgi_input[0]);
        if(strcasecmp(method, "POST") == 0) {
            for(i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        }
        while(read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);
        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);
    }
}

void accept_request(void *arg) {
    int client = *(int *)arg;
    char buf[1024];
    size_t numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;
    char *query_string = NULL;
    // int num = get_content(client, buf, sizeof(buf));
    // printf("num = %d\nclient content = %s\n", num, buf);
    numchars = get_line(client, buf, sizeof(buf));
    printf("1 numchars = %d\n", numchars);
    printf("buf = %s\n", buf);
    i = 0; j = 0;
    while(!ISspace(buf[i]) && (i < sizeof(method) - 1)) {
        method[i] = buf[i];
        i++;
    }
    j = i;
    method[i] = '\0';
    
    if(strcasecmp(method, "GET") && strcasecmp(method, "POST"))
        error_die("method unimplemented");
    if(strcasecmp(method, "POST") == 0)
        cgi = 1;

    i = 0;
    while((ISspace(buf[j]) && (j < numchars)))
        j++;
    while(!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars)) {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

    if(strcasecmp(method, "GET") == 0) {
        query_string = url;
        while((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if(*query_string == '?') {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    sprintf(path, "htdocs%s", url);
    if(path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    
    if(stat(path, &st) == -1) {
        printf("path stat = -1");
        while((numchars > 0) && strcmp("\n", buf))
            numchars = get_line(client, buf, sizeof(buf));
        printf("not found");
    }else {
        if((st.st_mode & __S_IFMT) == __S_IFDIR)
            strcat(path, "/index.html");
        printf("path = %s\n", path);
        if((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH)) {
            cgi = 1;
        }
        if(!cgi) {
            printf("serve file\n");
            serve_file(client, path);
        }else {
            printf("execute cgi\n");
            execute_cgi(client, path, method, query_string);
        }
    }
    close(client);
}


int main(void) {
    int server_sock = -1;
    unsigned short port = 4000;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);

    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);
    printf("server_sock= %d\n", server_sock);

    while(1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_name, &client_name_len);
        printf("client_sock = %d\n", client_sock);
        if(client_sock == -1)
            error_die("accept");
        accept_request(&client_sock);
    }

    close(server_sock);

    return 0;
}