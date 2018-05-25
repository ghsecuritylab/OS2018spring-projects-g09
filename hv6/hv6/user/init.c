#include "user.h"

extern char* _uprogs_hello_start[];

void line(char *buf, size_t n) {
	char ch;
	size_t off = 0;

	while(off < n) {
		ch = sys_debug_getchar();
		if (ch == '\r') continue;
		if (ch == '\n') break;
		buf[off++] = ch;
	}
	buf[off] = '\0';
	return;
}

int main() {
	pid_t pid = getpid();
	cprintf("Hello World pid = %d\n", pid);

////	cprintf("Input Line:");
////	line(buf, sizeof(buf));
////	cprintf("Your Input:%s\n", buf);
//
	pid = fork();
	cprintf("fork pid = %d\n", pid);
	if (pid == 0) {
		exit();
	}

	yield();
	cprintf("after yield getpid() = %d\n", getpid());

	pid = fork();
	if (pid == 0) {
		cprintf("Info: running hello uprog\n");
		exec_in_mem(_uprogs_hello_start);
		cprintf("Error: this code should not reach\n");
	}
	yield();

	cprintf("Info: init process exit.\n");
    while(1);
}
