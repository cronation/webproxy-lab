#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000 // 사용하지 않음
#define MAX_OBJECT_SIZE 102400

#define CACHE_ITEM_NUM 10 // cache_list배열에 들어갈 cache_item의 개수

#define DEF_END_PORT "80" // default end server port

typedef struct {
	int read_cnt; // 현재 cache에서 수행중인 읽기 명령 수
	sem_t r_cnt_sem; // read_cnt 수정 충돌을 방지하는 semaphore
	sem_t write_sem; // 쓰기 충돌을 방지하는 semaphore
	char request[MAXLINE]; // 저장할 HTTP 요청 (e.g. "GET localhost:51312/home.html HTTP/1.1")
	int last_req_idx; // 마지막으로 읽기를 수행한 요청 번호 (LRU policy)
	int obj_len; // object 크기
	char obj[MAX_OBJECT_SIZE]; // 저장된 cache object
} cache_item;

typedef struct {
	int connfd;
	int req_idx;
} thread_args;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

static cache_item cache_list[CACHE_ITEM_NUM]; // 10개의 cache_item을 저장하는 배열

void doit(int fd, int req_idx);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *hostname, char *port, char *path);
int send_requesthdrs(rio_t *cl_rp, rio_t *end_rp, char *hostname);
int relay_response(rio_t *cl_rp, rio_t *end_rp, char *cache_buf);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
cache_item *get_cache_victim();
void serve_from_cache(int fd, cache_item *cache_p, int req_idx);

void *thread(void *vargp);

int main(int argc, char **argv) {
	int listenfd;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;

	/* Check command line args */
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	printf("################################ My Proxy Server ###############################\n");
	printf("Initializing...\n");

	listenfd = Open_listenfd(argv[1]);

	for (int i = 0; i < CACHE_ITEM_NUM; i++) {
		// cache_item 초기화
		cache_list[i].read_cnt = 0;
		Sem_init(&(cache_list[i].r_cnt_sem), 0, 1);
		Sem_init(&(cache_list[i].write_sem), 0, 1);
		strcpy(cache_list[i].request, "");
		cache_list[i].last_req_idx = -1;
		cache_list[i].obj_len = 0;
		strcpy(cache_list[i].obj, "");
	}

	printf("Ready!\n");

	int req_idx = 0;
	thread_args *t_arg_p; // thread에 전달할 구조체
	pthread_t tid;
	while (1) {
		req_idx++;
		t_arg_p = Malloc(sizeof(thread_args));

		clientlen = sizeof(clientaddr);
		t_arg_p->connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept
		t_arg_p->req_idx = req_idx;
		
		printf("================================ REQUEST No.%3d ================================\n", req_idx);
		Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		printf("Accepted connection from (%s, %s)\n", hostname, port);

		Pthread_create(&tid, NULL, thread, t_arg_p); // thread 생성해 connfd로 들어오는 요청 처리
	}
}

// vargp로 client fd의 주소를 받아 요청 처리하는 thread
void *thread(void *vargp) {
	int connfd = ( (thread_args*) vargp )->connfd;
	int req_idx = ( (thread_args*) vargp )->req_idx;
	Pthread_detach(pthread_self());
	Free(vargp); // main 함수에서 할당한 메모리 반환

	doit(connfd, req_idx);   // line:netp:tiny:doit
	Close(connfd);  // line:netp:tiny:close

	return NULL;
}

// cl_fd로 들어오는 요청 처리
void doit(int cl_fd, int req_idx) {
	int is_static;
	struct stat sbuf;
	char buf[MAXLINE], request[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char end_hostname[MAXLINE], end_port[MAXLINE], end_uri[MAXLINE];
	rio_t cl_rio, end_rio;

	Rio_readinitb(&cl_rio, cl_fd); // rio init

	printf("\n");
	printf("Request from client:\n");
	int n = Rio_readlineb(&cl_rio, request, MAXLINE); // 요청 읽기

	if (n >= MAXLINE-1) {
		// 요청이 버퍼를 초과함
		read_requesthdrs(&cl_rio); // 요청 헤더 읽어서 출력
		clienterror(cl_fd, method, "414", "URI Too Long", "Request should be less than 8192 bytes");
		return;
	}

	printf("%s", request);
	
	for (int i = 0; i < CACHE_ITEM_NUM; i++) {
		// cache_list의 cache_item을 확인
		if (strcmp(request, cache_list[i].request) == 0) {
			// cache_list에 저장된 request와 일치하는 요청
			read_requesthdrs(&cl_rio); // 요청 헤더 읽어서 출력
			printf("REQ#%d.doit(): incoming request matches cached request, will be served from cache\n", req_idx);
			serve_from_cache(cl_fd, &(cache_list[i]), req_idx);
			return;
		}
	}

	// cache_list에 저장되지 않은 요청: end server에 접속해 전달

	sscanf(request, "%s %s %s", method, uri, version); // 요청 분할

	// end server에 보낼 request로 변환
	parse_uri(uri, end_hostname, end_port, end_uri);

	if (strcmp(method, "CONNECT") == 0) {
		// 브라우저의 CONNECT 요청 무시
		read_requesthdrs(&cl_rio); // 요청 헤더 읽어서 출력
		clienterror(cl_fd, method, "501", "Not Implemented", "CONNECT method is not supported");
		return;
	}

	// end server와 연결 시도
	int end_fd = open_clientfd(end_hostname, end_port); // end server와 연결할 client socket 생성
	Rio_readinitb(&end_rio, end_fd); // rio init

	if (end_fd == -1) {
		// 에러
		read_requesthdrs(&cl_rio); // 요청 헤더 읽어서 출력
		char errmsg[MAXLINE];
		sprintf(errmsg, "Connection to end server (%s:%s) failed", end_hostname, end_port);
		clienterror(cl_fd, method, "502", "Bad Gateway", errmsg);
		return;
	}

	printf("\n");
	
	// end server에 요청 전송
	printf("Request to end server:\n");
	sprintf(buf, "%s %s %s\r\n", method, end_uri, "HTTP/1.0");
	printf("%s", buf);
	Rio_writen(end_fd, buf, strlen(buf));
	send_requesthdrs(&cl_rio, &end_rio, end_hostname);

	// end server의 응답을 client에게 전달
	char cache_buf[MAX_OBJECT_SIZE]; // cache 버퍼
	printf("Response from end server (relay to client):\n");
	int cache_obj_len = relay_response(&cl_rio, &end_rio, cache_buf);

	// 응답 캐싱
	if (strcmp(cache_buf, "") != 0) {
		printf("REQ#%d.doit(): cache is small enough, will cache for later use\n", req_idx);
		printf("[CACHE REQUEST]:\n%s\n", request);

		// cache object가 MAX_OBJECT_SIZE보다 작음: cache_list에 추가
		cache_item *item_p = get_cache_victim(); // LRU policy로 덮어씌울 item 선택

		printf("victim request is:\n");
		printf("[CACHE REQUEST]: %s (last used by REQ#%d)\n", item_p->request, item_p->last_req_idx);

		P(&(item_p->write_sem)); // 쓰기 충돌 방지
		strcpy(item_p->request, request);
		item_p->last_req_idx = req_idx;
		item_p->obj_len = cache_obj_len;
		memcpy(item_p->obj, cache_buf, cache_obj_len);

		V(&(item_p->write_sem));

		printf("caching successful!\n");
	}

	close(end_fd);
	return;
}

// --------------------------------------- [REQ FUNC] ---------------------------------------

// client의 요청 헤더를 읽고 무시
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

// client로부터 받은 proxy uri를 hostname, port, path로 분할
int parse_uri(char *uri, char *hostname, char *port, char *path) {
	char *cln_ptr, *slsh_ptr;

	if (strstr(uri, "https://")) {
		// uri 앞에 https:// 삭제
		uri += 8;
	} else if (strstr(uri, "http://")) {
		// uri 앞에 http:// 삭제
		uri += 7;
	}

	slsh_ptr = strchr(uri, '/');
	cln_ptr = strchr(uri, ':');

	if (slsh_ptr != NULL) {
		// / 가 존재: path 추출
		strcpy(path, slsh_ptr);
		*slsh_ptr = '\0';
	} else {
		// 기본 path
		strcpy(path, "/");
	}

	if (cln_ptr != NULL) {
		// : 가 존재: 포트 번호 추출
		strcpy(port, cln_ptr+1);
		*cln_ptr = '\0';
	} else {
		// default 포트 번호 사용
		strcpy(port, DEF_END_PORT);
	}

	strcpy(hostname, uri);

	return 0;
}

// client에서 받은 헤더를 읽고 end server로 요청 전송
int send_requesthdrs(rio_t *cl_rp, rio_t *end_rp, char *hostname) {
	int sent_host_hdr = 0; // client가 전송한 헤더에 Host 헤더가 있었는지 여부
	int sent_usr_agent_hdr = 0;
	char hdr[MAXLINE] = "", buf[MAXLINE];

	// client 요청 헤더 읽어서 hdr에 저장
	Rio_readlineb(cl_rp, buf, MAXLINE);
	while (strcmp(buf, "\r\n")) {
		if (strcasestr(buf, "Host: ")) {
			// client가 전송한 Host 헤더
			sprintf(hdr, "%s%s", hdr, buf);
			sent_host_hdr = 1;
		} else if (strcasestr(buf, "User-Agent: ")) {
			sprintf(hdr, "%s%s", hdr, buf);
			sent_usr_agent_hdr = 1;
		} else if (strcasestr(buf, "Connection: ")) {
			// client가 전송한 Connection/Proxy-Connection 헤더는 무시
			printf("(NOT SENT TO END SERVER) %s", buf);
		} else {
			sprintf(hdr, "%s%s", hdr, buf);
		}
		Rio_readlineb(cl_rp, buf, MAXLINE);
	}

	// proxy에서 추가하는 헤더
	if (!sent_host_hdr) {
		sprintf(hdr, "%sHost: %s\r\n", hdr, hostname); // Host 헤더
	}
	if (!sent_usr_agent_hdr) {
		sprintf(hdr, "%s%s", hdr, user_agent_hdr); // User-Agent 헤더
	}
	sprintf(hdr, "%sConnection: close\r\n", hdr); // Connection 헤더
	sprintf(hdr, "%sProxy-Connection: close\r\n\r\n", hdr); // Proxy-Conneciton 헤더

	printf("%s", hdr);
	Rio_writen(end_rp->rio_fd, hdr, strlen(hdr)); // 요청 헤더 전송
}

// end server의 응답을 client에게 전달
int relay_response(rio_t *cl_rp, rio_t *end_rp, char *cache_buf) {
	char buf[MAXLINE];
	char *ptr;
	int content_len = 0; // 헤더를 제외한 크기 (Content-Length 헤더로부터 읽음)
	
	char *cache_ptr = cache_buf;
	int cache_obj_len = 0; // 헤더를 포함한 크기
	int n;

	strcpy(cache_buf, ""); // cache 버퍼 초기화

	// 헤더 전달
	n = Rio_readlineb(end_rp, buf, MAXLINE);
	cache_obj_len += n;
	memcpy(cache_ptr, buf, n);
	cache_ptr += n;
	while (strcmp(buf, "\r\n")) {
		if ( (ptr = strcasestr(buf, "Content-Length: ")) != NULL ) {
			// content 전달을 위해 Content-length 값을 저장
			content_len = atoi(buf + 16);
		}
		printf("%s", buf);
		Rio_writen(cl_rp->rio_fd, buf, strlen(buf));

		n = Rio_readlineb(end_rp, buf, MAXLINE);
		cache_obj_len += n;
		if (cache_obj_len < MAX_OBJECT_SIZE) {
			memcpy(cache_ptr, buf, n);
			cache_ptr += n;
		}
	}
	// 헤더 끝
	printf("\r\n"); 
	Rio_writen(cl_rp->rio_fd, "\r\n", 2);

	int bytes_left = content_len;

	cache_obj_len += content_len;
	while (bytes_left > 0) {
		n = MAXLINE < bytes_left ? MAXLINE : bytes_left; // min(bytes_left, MAXLINE)
		Rio_readnb(end_rp, buf, n);
		Rio_writen(cl_rp->rio_fd, buf, n);

		bytes_left -= n;

		if (cache_obj_len < MAX_OBJECT_SIZE) {
			memcpy(cache_ptr, buf, n);
			cache_ptr += n;
		}
	}

	if (cache_obj_len >= MAX_OBJECT_SIZE) {
		// object의 크기가 너무 큰 경우 버퍼를 빈 문자열로
		strcpy(cache_buf, "");
	}
	
	return cache_obj_len;
}

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

// --------------------------------------- [CACHE FUNC] ---------------------------------------

// cache_list에서 삭제하고 덮어씌울 cache_item의 주소를 반환
cache_item *get_cache_victim() {
	int ret_idx = -1;
	int min_req_idx = __INT_MAX__;

	for (int i = 0; i < CACHE_ITEM_NUM; i++) {
		// 마지막 호출로부터 가장 오래 지난 item을 선택
		if (cache_list[i].last_req_idx < min_req_idx) {
			ret_idx = i;
			min_req_idx = cache_list[i].last_req_idx;
		}
	}

	return &(cache_list[ret_idx]);
}

// fd로 cache_idx의 cache를 전달
void serve_from_cache(int fd, cache_item *cache_p, int req_idx) {
	// read_cnt, last_req_idx 업데이트
	P(&(cache_p->r_cnt_sem));
	cache_p->read_cnt++;
	if (cache_p->read_cnt == 1) {
		// 첫 읽기 시작
		P(&(cache_p->write_sem)); // 쓰기 방지
	}
	cache_p->last_req_idx = req_idx; // 마지막 읽기 요청의 req_idx
	V(&(cache_p->r_cnt_sem));
	
	// fd로 전달
	Rio_writen(fd, cache_p->obj, cache_p->obj_len);
	
	// read_cnt 업데이트
	P(&(cache_p->r_cnt_sem));
	cache_p->read_cnt--;
	if (cache_p->read_cnt == 0) {
		// 마지막 읽기 끝
		V(&(cache_p->write_sem)); // 쓰기 허용
	}
	V(&(cache_p->r_cnt_sem));
}