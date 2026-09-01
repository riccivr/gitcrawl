/* See LICENSE file for copyright and license details. */
#include "process_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if !defined(_WIN32)
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>

static int
set_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return -1;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

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

	int in_fd = pipe_in[1];
	int out_fd = pipe_out[0];
	int err_fd = pipe_err[0];

	if (in_fd >= 0) set_nonblocking(in_fd);
	if (out_fd >= 0) set_nonblocking(out_fd);
	if (err_fd >= 0) set_nonblocking(err_fd);

	const char *stdin_ptr = (const char *)stdin_data;
	size_t stdin_rem = stdin_len;

	/* Multiplexed I/O event loop using poll() to prevent pipe-full deadlocks */
	while (in_fd >= 0 || out_fd >= 0 || err_fd >= 0) {
		struct pollfd fds[3];
		nfds_t nfds = 0;
		int in_idx = -1, out_idx = -1, err_idx = -1;

		if (in_fd >= 0) {
			in_idx = (int)nfds;
			fds[nfds].fd = in_fd;
			fds[nfds].events = POLLOUT;
			fds[nfds].revents = 0;
			nfds++;
		}
		if (out_fd >= 0) {
			out_idx = (int)nfds;
			fds[nfds].fd = out_fd;
			fds[nfds].events = POLLIN;
			fds[nfds].revents = 0;
			nfds++;
		}
		if (err_fd >= 0) {
			err_idx = (int)nfds;
			fds[nfds].fd = err_fd;
			fds[nfds].events = POLLIN;
			fds[nfds].revents = 0;
			nfds++;
		}

		if (nfds == 0) break;

		int ret = poll(fds, nfds, -1);
		if (ret < 0) {
			if (errno == EINTR) continue;
			break;
		}

		/* Handle stdin non-blocking write */
		if (in_idx >= 0 && (fds[in_idx].revents & (POLLOUT | POLLERR | POLLHUP))) {
			if (fds[in_idx].revents & POLLOUT) {
				ssize_t w = write(in_fd, stdin_ptr, stdin_rem);
				if (w > 0) {
					stdin_ptr += w;
					stdin_rem -= (size_t)w;
					if (stdin_rem == 0) {
						close(in_fd);
						in_fd = -1;
					}
				} else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
					close(in_fd);
					in_fd = -1;
				}
			} else {
				close(in_fd);
				in_fd = -1;
			}
		}

		/* Handle stdout non-blocking read */
		if (out_idx >= 0 && (fds[out_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
			char buf[8192];
			ssize_t r;
			int had_read = 0;
			while ((r = read(out_fd, buf, sizeof(buf))) > 0) {
				had_read = 1;
				strbuf_append_len(stdout_buf, buf, (size_t)r);
			}
			if (r == 0 || (!had_read && (fds[out_idx].revents & (POLLHUP | POLLERR)))) {
				close(out_fd);
				out_fd = -1;
			}
		}

		/* Handle stderr non-blocking read */
		if (err_idx >= 0 && (fds[err_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
			char buf[8192];
			ssize_t r;
			int had_read = 0;
			while ((r = read(err_fd, buf, sizeof(buf))) > 0) {
				had_read = 1;
				strbuf_append_len(stderr_buf, buf, (size_t)r);
			}
			if (r == 0 || (!had_read && (fds[err_idx].revents & (POLLHUP | POLLERR)))) {
				close(err_fd);
				err_fd = -1;
			}
		}
	}

	if (in_fd >= 0) close(in_fd);
	if (out_fd >= 0) close(out_fd);
	if (err_fd >= 0) close(err_fd);

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
/* Windows implementation using CreateProcessA and overlapped/pipe handles */
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
