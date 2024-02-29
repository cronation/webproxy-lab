/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "csapp.h"

int main(void) {
	char *buf, *p, *p2;
	char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
	int n1 = 0, n2 = 0;

	if ( (buf = getenv("QUERY_STRING")) != NULL ) {
		// QUERY_STRING에 저장된 변수에서 정수 두 개를 추출
		// adder_2?a={n1}&b={n2} 형태
		p = strchr(buf, '=');
		p2 = strchr(buf, '&');
		*p = '\0';
		
		strcpy(arg1, p+1);
		strcpy(arg2, p2+3);
		n1 = atoi(arg1);
		n2 = atoi(arg2);
	}


	sprintf(content, "QUERY_STRING=%s", buf);
	sprintf(content, "Welcome to add.com: ");
	sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
	sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
	sprintf(content, "%sThanks for visiting!\r\n", content);

	printf("Connection: close\r\n");
	printf("Content-length: %d\r\n", (int)strlen(content));
	printf("Content-type: text/html\r\n\r\n");
	printf("%s", content);
	fflush(stdout);

	exit(0);
}
/* $end adder */
