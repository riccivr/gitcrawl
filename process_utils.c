/* See LICENSE file for copyright and license details. */
#include "process_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if !defined(_WIN32)
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

int
run_cmd_argv(const char *const *argv,
             const void *stdin_data, size_t stdin_len,
             struct strbuf *stdout_buf,
             struct strbuf *stderr_buf,
             const char *const *envp)
{
	if (!argv || !argv[0])
		return -1;

	int pipe_in[2] = {-1, -1};
	int pipe_out[2] = {-1, -1};
	int pipe_err[2] = {-1, -1};

	if (stdin_data && stdin_len > 0) {
		if (pipe(pipe_in) < 0)
			return -1;
	}
	if (stdout_buf) {
		if (pipe(pipe_out) < 0) {
			if (pipe_in[0] >= 0) { close(pipe_in[0]); close(pipe_in[1]); }
			return -1;
		}
	}
	if (stderr_buf) {
		if (pipe(pipe_err) < 0) {
			if (pipe_in[0] >= 0) { close(pipe_in[0]); close(pipe_in[1]); }
			if (pipe_out[0] >= 0) { close(pipe_out[0]); close(pipe_out[1]); }
			return -1;
		}
	}

	pid_t pid = fork();
	if (pid < 0) {
		if (pipe_in[0] >= 0) { close(pipe_in[0]); close(pipe_in[1]); }
		if (pipe_out[0] >= 0) { close(pipe_out[0]); close(pipe_out[1]); }
		if (pipe_err[0] >= 0) { close(pipe_err[0]); close(pipe_err[1]); }
		return -1;
	}

	if (pid == 0) {
		/* Child process */
		if (pipe_in[0] >= 0) {
			dup2(pipe_in[0], STDIN_FILENO);
			close(pipe_in[0]);
			close(pipe_in[1]);
		} else {
			int devnull = open("/dev/null", O_RDONLY);
			if (devnull >= 0) {
				dup2(devnull, STDIN_FILENO);
				close(devnull);
			}
		}

		if (pipe_out[1] >= 0) {
			dup2(pipe_out[1], STDOUT_FILENO);
			close(pipe_out[0]);
			close(pipe_out[1]);
		}
		if (pipe_err[1] >= 0) {
			dup2(pipe_err[1], STDERR_FILENO);
			close(pipe_err[0]);
			close(pipe_err[1]);
		}

		if (envp) {
			for (const char *const *e = envp; *e; e++) {
				const char *eq = strchr(*e, '=');
				if (eq) {
					char k[256];
					size_t klen = eq - *e;
					if (klen < sizeof(k)) {
						memcpy(k, *e, klen);
						k[klen] = '\0';
						setenv(k, eq + 1, 1);
					}
				}
			}
		}

		execvp(argv[0], (char *const *)argv);
		_exit(127);
	}

	/* Parent process */
	if (pipe_in[0] >= 0) close(pipe_in[0]);
	if (pipe_out[1] >= 0) close(pipe_out[1]);
	if (pipe_err[1] >= 0) close(pipe_err[1]);

	/* Write stdin data */
	if (pipe_in[1] >= 0) {
		const char *p = (const char *)stdin_data;
		size_t remaining = stdin_len;
		while (remaining > 0) {
			ssize_t w = write(pipe_in[1], p, remaining);
			if (w <= 0)
				break;
			p += w;
			remaining -= w;
		}
		close(pipe_in[1]);
	}

	/* Read stdout and stderr */
	if (pipe_out[0] >= 0) {
		char buf[4096];
		ssize_t r;
		while ((r = read(pipe_out[0], buf, sizeof(buf))) > 0) {
			strbuf_append_len(stdout_buf, buf, r);
		}
		close(pipe_out[0]);
	}

	if (pipe_err[0] >= 0) {
		char buf[4096];
		ssize_t r;
		while ((r = read(pipe_err[0], buf, sizeof(buf))) > 0) {
			strbuf_append_len(stderr_buf, buf, r);
		}
		close(pipe_err[0]);
	}

	int status = 0;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}

	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return -1;
}

#else
/* Windows implementation using CreateProcessA */
#include <windows.h>
#include <io.h>

int
run_cmd_argv(const char *const *argv,
             const void *stdin_data, size_t stdin_len,
             struct strbuf *stdout_buf,
             struct strbuf *stderr_buf,
             const char *const *envp)
{
	(void)envp;
	if (!argv || !argv[0])
		return -1;

	/* Build Windows command line with proper quoting */
	struct strbuf cmdline;
	strbuf_init(&cmdline, 512);
	for (size_t i = 0; argv[i]; i++) {
		if (i > 0) strbuf_append_char(&cmdline, ' ');
		strbuf_append_char(&cmdline, '"');
		for (const char *p = argv[i]; *p; p++) {
			if (*p == '"') strbuf_append_str(&cmdline, "\\\"");
			else if (*p == '\\') {
				if (*(p+1) == '"' || *(p+1) == '\0')
					strbuf_append_str(&cmdline, "\\\\");
				else
					strbuf_append_char(&cmdline, '\\');
			} else {
				strbuf_append_char(&cmdline, *p);
			}
		}
		strbuf_append_char(&cmdline, '"');
	}

	HANDLE hStdinRead = NULL, hStdinWrite = NULL;
	HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
	HANDLE hStderrRead = NULL, hStderrWrite = NULL;

	SECURITY_ATTRIBUTES sa;
	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	if (stdin_data && stdin_len > 0) {
		CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0);
		SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);
	}
	if (stdout_buf) {
		CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0);
		SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
	}
	if (stderr_buf) {
		CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0);
		SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);
	}

	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags |= STARTF_USESTDHANDLES;
	si.hStdInput = hStdinRead ? hStdinRead : GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = hStdoutWrite ? hStdoutWrite : GetStdHandle(STD_OUTPUT_HANDLE);
	si.hStdError = hStderrWrite ? hStderrWrite : GetStdHandle(STD_ERROR_HANDLE);
	memset(&pi, 0, sizeof(pi));

	BOOL ok = CreateProcessA(NULL, cmdline.buf, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
	strbuf_free(&cmdline);

	if (hStdinRead) CloseHandle(hStdinRead);
	if (hStdoutWrite) CloseHandle(hStdoutWrite);
	if (hStderrWrite) CloseHandle(hStderrWrite);

	if (!ok) {
		if (hStdinWrite) CloseHandle(hStdinWrite);
		if (hStdoutRead) CloseHandle(hStdoutRead);
		if (hStderrRead) CloseHandle(hStderrRead);
		return -1;
	}

	if (hStdinWrite) {
		DWORD written;
		WriteFile(hStdinWrite, stdin_data, (DWORD)stdin_len, &written, NULL);
		CloseHandle(hStdinWrite);
	}

	if (hStdoutRead) {
		char buf[4096];
		DWORD bytesRead;
		while (ReadFile(hStdoutRead, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {
			strbuf_append_len(stdout_buf, buf, bytesRead);
		}
		CloseHandle(hStdoutRead);
	}

	if (hStderrRead) {
		char buf[4096];
		DWORD bytesRead;
		while (ReadFile(hStderrRead, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {
			strbuf_append_len(stderr_buf, buf, bytesRead);
		}
		CloseHandle(hStderrRead);
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD exitCode = 0;
	GetExitCodeProcess(pi.hProcess, &exitCode);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return (int)exitCode;
}
#endif
