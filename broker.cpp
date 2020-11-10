#include <inttypes.h>
#include <vector>
#include <sys/select.h>
#include <errno.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <fcntl.h>
// getenv
#include <stdlib.h>

#include "broker.h"

#define FD_READ(x) x[0]
#define FD_WRITE(x) x[1]

using std::string;
using std::vector;

int checkReturn(const int pid, int *exit_code) {
  int status = 0;
  int w_pid = waitpid(pid, &status, WNOHANG);
  if ( w_pid == -1 ) {
    return -1;
  } else if ( w_pid == pid ) {
    if ( WIFEXITED(status) )
      *exit_code = WEXITSTATUS(status);
    else if ( WIFSIGNALED(status) )
      *exit_code = WTERMSIG(status);
  } else {
    *exit_code = -1;
  }
  return 0;
}

int readInto(vector<uint8_t>& v, int fd) {
  const int size = 128;
  uint8_t data[size] = { 0 };
  int total_read = 0;
  do {
    int bytes = read(fd, (void*)data, size);
    if ( bytes <= 0 ) break;
    total_read += bytes;
    v.reserve(v.size() + bytes);
    for ( int i = 0; i < bytes; i++ )
      v.push_back(data[i]);
  } while(1);
  return total_read;
}

const char* getenvOrDefault(const char* env, const char* def) {
  const char* val = getenv(env);
  const char* ret = val ? val : def;
  return ret;
}

void makeNonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if ( (flags | O_NONBLOCK) != O_NONBLOCK ) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
  errno = 0;
}

broker_ipc::broker_ipc() {}

int broker_ipc::start() {
  if ( pipe(in) == -1 ) return errno;
  if ( pipe(out) == -1 ) return errno;
  if ( pipe(err) == -1 ) return errno;
  fd_max = FD_WRITE(err) + 1;

  if ( is_nonblocking ) {
    makeNonblocking(FD_READ(out));
    makeNonblocking(FD_READ(err));
  }

  pid = fork();
  if ( pid == -1 ) return errno;
  if ( pid == 0 ) {
    return at_fork();
  }

  close(FD_READ(in));
  close(FD_WRITE(out));
  close(FD_WRITE(err));
  errno = 0;

  return 0;
}

struct tv_timeout {
  struct timeval tv;
  long tv_sec, tv_usec;
  // carry-over nanos, 1000000 micros = 1ms
  int64_t micros;
  int64_t timeout;

  tv_timeout(int64_t timeout_ms, int64_t sec, int64_t usec) {
    tv_sec = sec;
    tv_usec = usec;
    reset(timeout_ms);
  }

  void reset(int64_t timeout_ms) {
    micros = 0;
    timeout = timeout_ms;
    tv.tv_sec = tv_sec;
    tv.tv_usec = tv_usec;
  }

  void update() {
    // Decrease MS timeout by seconds select() waited
    timeout -= (tv_sec - tv.tv_sec) * 1000;
    // Decrease carry-over-micros
    micros += tv_usec - tv.tv_usec;
    if ( micros > 1000000 ) {
      timeout -= 1;
      micros = 0;
    }

    // reset for next iteration
    tv.tv_sec = tv_sec;
    tv.tv_usec = tv_usec;
  }

  bool expired() {
    return timeout <= 0;
  }
};

int broker_ipc::read_from() {

  // wait for .1ms
  tv_timeout timeout(timeout_ms, 0, 100000);
  int status = 0;

  while( !status && returncode == -1 && !timeout.expired() ) {

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(FD_READ(out), &readfds);
    FD_SET(FD_READ(err), &readfds);

    int ready = select(fd_max, &readfds, NULL, NULL, &timeout.tv);
    if ( ready == 0
      || (ready == -1 && (errno == EINTR || errno == EAGAIN))
    ) {
      // timeout
      timeout.update();
      continue;
    }

    if ( ready == -1 ) {
      status = -1;
      continue;
    }

    int bytes_read = 0;
    if ( FD_ISSET(FD_READ(out), &readfds) )
      bytes_read += readInto(data, FD_READ(out));
    if ( FD_ISSET(FD_READ(err), &readfds) )
      bytes_read += readInto(error, FD_READ(err));
    if ( bytes_read > 0 )
      timeout.reset(timeout_ms);

    int ret = checkReturn(pid, &returncode);
    if ( ret == 0 && errno == EAGAIN ) {
      errno = 0;
    } else if ( ret == -1 ) {
      status = ret;
    }
  }

  if ( timeout.expired() && returncode == -1 ) {
    kill(pid, SIGKILL);
    errno = 0;
  }

  close(FD_WRITE(in));
  close(FD_READ(out));
  close(FD_READ(err));

  return status;
}

int broker_ipc::write_to(string& s) {
  vector<uint8_t> v = vector<uint8_t>(s.begin(), s.end());
  return write_to(v);
}

int broker_ipc::write_to(vector<uint8_t>& v) {
  int fd = FD_WRITE(in);
  int ret = write(fd, v.data(), v.size());
  return ret;
}

/**
 * Never returns.
 */
int shell_broker::at_fork() {
  const char* shell = getenvOrDefault("SHELL", "sh");
  if ( dup2(FD_READ(in), STDIN_FILENO) == -1 ) return -1;
  if ( dup2(FD_WRITE(out), STDOUT_FILENO) == -1 ) return -1;
  if ( dup2(FD_WRITE(err), STDERR_FILENO) == -1 ) return -1;
  close(FD_WRITE(in));
  close(FD_READ(out));
  close(FD_READ(err));
  errno = 0;
  return execlp(shell, shell, NULL);
}

int shell_broker::write_to(vector<uint8_t>& v) {
  int ret = broker_ipc::write_to(v);
  // shell wont run until stdin is closed.
  close(FD_WRITE(in));
  if ( ret == v.size() ) {
    errno = 0;
  }
  return ret;
}
