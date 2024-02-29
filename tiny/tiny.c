/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) {
	int listenfd, connfd;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;

	/* Check command line args */
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	listenfd = Open_listenfd(argv[1]);

	int idx = 0;
	while (1) {
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept

		idx++;
		printf("================================ REQUEST No.%3d ================================\n", idx); /////

		Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		printf("Accepted connection from (%s, %s)\n", hostname, port);
		doit(connfd);   // line:netp:tiny:doit
		Close(connfd);  // line:netp:tiny:close
	}
}

void doit(int fd) {
	int is_static;
	struct stat sbuf;
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char filename[MAXLINE], cgiargs[MAXLINE];
	rio_t rio;

	Rio_readinitb(&rio, fd);
	Rio_readlineb(&rio, buf, MAXLINE);
	printf("Request headers:\n");
	printf("%s", buf);

	sscanf(buf, "%s %s %s", method, uri, version);

	if (!strcasecmp(method, "GET")) {
		// GET method
		read_requesthdrs(&rio); // 요청 헤더 읽어서 출력

		is_static = parse_uri(uri, filename, cgiargs);
		if (stat(filename, &sbuf) < 0) {
			// 요청한 filename이 존재하지 않음
			clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
			return;
		}

		if (is_static) {
			// serve static content
			if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
				return;
			}
			serve_static(fd, filename, sbuf.st_size);
		} else {
			// serve dynamic content
			if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
				clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
				return;
			}
			serve_dynamic(fd, filename, cgiargs);
		}
	} else if (!strcasecmp(method, "HEAD")) {
		// HEAD method
		read_requesthdrs(&rio); // 요청 헤더 읽어서 출력

		is_static = parse_uri(uri, filename, cgiargs);
		if (stat(filename, &sbuf) < 0) {
			// 요청한 filename이 존재하지 않음
			clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
			return;
		}

		if (is_static) {
			// serve static content
			if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
				return;
			}
			serve_static_head(fd, filename, sbuf.st_size);
		} else {
			// serve dynamic content
			if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
				clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
				return;
			}
			serve_dynamic_head(fd, filename, cgiargs);
		}
	} else {
		// other methods
		clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
		return;
	}
}

void read_requesthdrs(rio_t *rp) {
	char buf[MAXLINE];

	Rio_readlineb(rp, buf, MAXLINE);
	while (strcmp(buf, "\r\n")) {
		printf("%s", buf);
		Rio_readlineb(rp, buf, MAXLINE);
	}
	printf("\n");
	return;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {
	char *ptr;

	if (!strstr(uri, "cgi-bin")) {
		// cgi-bin를 포함하지 않으면 static
		strcpy(cgiargs, "");
		strcpy(filename, ".");
		strcat(filename, uri);
		if (uri[strlen(uri) -1] == '/') {
			// '/'이면 home.html로
			strcat(filename, "home.html");
		}
		return 1;
	} else {
		// 포함하면 dynamic
		ptr = index(uri, '?');
		if (ptr) {
			// ?를 포함하므로 인자로 전달
			strcpy(cgiargs, ptr+1);
			*ptr = '\0';
		} else {
			strcpy(cgiargs, "");
		}
		strcpy(filename, ".");
		strcat(filename, uri);
		return 0;
	}
}

// static 요청 처리
void serve_static(int fd, char *filename, int filesize) {
	int srcfd;
	char *srcp, filetype[MAXLINE], buf[MAXBUF];

	get_filetype(filename, filetype);
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
	sprintf(buf, "%sConnection: close\r\n", buf);
	sprintf(buf, "%sContent-Length: %d\r\n", buf, filesize);
	sprintf(buf, "%sContent-Type: %s\r\n\r\n", buf, filetype);

	Rio_writen(fd, buf, strlen(buf));
	printf("Response headers:\n");
	printf("%s", buf);

	srcfd = Open(filename, O_RDONLY, 0); // filename으로 지정된 파일 열기

	// default mmap version
	// srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 메모리 주소 할당
	// Close(srcfd);
	// Rio_writen(fd, srcp, filesize);
	// Munmap(srcp, filesize);

	// practice malloc version
	filesize = lseek(srcfd, 0, SEEK_END);
	lseek(srcfd, 0, SEEK_SET);
	srcp = malloc(filesize);
	Rio_readn(srcfd, srcp, filesize); // 할당한 주소로 복사
	Close(srcfd);
	Rio_writen(fd, srcp, filesize);
}

// dynamic 요청 처리
void serve_dynamic(int fd, char *filename, char *cgiargs) {
	char buf[MAXLINE], *emptylist[] = {NULL};

	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Server: Tiny Web Server\r\n");
	Rio_writen(fd, buf, strlen(buf));

	if (Fork() == 0) {
		// 자식 프로세스 생성 후 프로그램 실행
		setenv("QUERY_STRING", cgiargs, 1);
		Dup2(fd, STDOUT_FILENO);
		Execve(filename, emptylist, environ);
	}
	Wait(NULL);
}

// serve_static에서 헤더만 전송
void serve_static_head(int fd, char *filename, int filesize) {
	int srcfd;
	char *srcp, filetype[MAXLINE], buf[MAXBUF];

	get_filetype(filename, filetype);
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
	sprintf(buf, "%sConnection: close\r\n", buf);
	sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
	sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);

	Rio_writen(fd, buf, strlen(buf));
	printf("Response headers:\n");
	printf("%s", buf);
}

// serve_dynamic에서 헤더만 전송
void serve_dynamic_head(int fd, char *filename, char *cgiargs) {
	char buf[MAXLINE], *emptylist[] = {NULL};

	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
	sprintf(buf, "%sConnection: close\r\n", buf);
	sprintf(buf, "%sContent-length: %d\r\n", buf, 127); // 실제로는 인자에 따라 길이가 달라질 수 있음
	sprintf(buf, "%sContent-type: text/html\r\n\r\n", buf);
	Rio_writen(fd, buf, strlen(buf));
}

// fd로 오류 전송
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
	char buf[MAXLINE], body[MAXBUF];

	/* Build the HTTP response body */
	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

	/* Print the HTTP response */
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));
}

// filename의 파일 형식을 filetype에 저장
void get_filetype(char *filename, char *filetype) {
	if (strstr(filename, ".html")) {
		strcpy(filetype, "text/html");
	} else if (strstr(filename, ".gif")) {
		strcpy(filetype, "image/gif");
	} else if (strstr(filename, ".png")) {
		strcpy(filetype, "image/png");
	} else if (strstr(filename, ".jpg")) {
		strcpy(filetype, "image/jpeg");
	} else if (strstr(filename, ".mp4")) {
		strcpy(filetype, "video/mp4");
	} else {
		strcpy(filetype, "text/plain");
	}
}