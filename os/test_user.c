#include <syscall.h>

int main(int argc, char **argv, char **envp)
{
	const char* msg = "Hello, World from C!\r\n";
	int msg_len = 0;
	while (msg[msg_len]) msg_len++;
	__syscall3(SYSCALL_WRITE, 1, (long)msg, msg_len);
}
