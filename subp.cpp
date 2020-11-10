#include <iostream>
#include <vector>
#include <string>
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <fcntl.h>

using std::cout;
using std::endl;
using std::string;
using std::vector;

int read_into(vector<uint8_t>& v, int fd) {
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
  cout << "ENV[" << env << "] = " << ret << endl;
  return ret;
}

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

void make_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  errno = 0;
}

int check_output(string script, vector<unsigned char>& out, vector<unsigned char>& err) {
  const int READ = 0;
  const int WRITE = 1;
  bool exited = false;
  int exit_code = -1;
  int p_in[2];
  int p_out[2];
  int p_err[2];

  if ( pipe(p_in) == -1 ) return errno;
  if ( pipe(p_out) == -1 ) return errno;
  if ( pipe(p_err) == -1 ) return errno;

  make_nonblocking(p_out[READ]);
  make_nonblocking(p_err[READ]);

  int pid = fork();
  if ( pid == -1 ) return -1;
  if ( pid == 0 ) {
    const char* shell = getenvOrDefault("SHELL", "sh");
    if ( dup2(p_in[READ], STDIN_FILENO) == -1 ) return -1;
    if ( dup2(p_out[WRITE], STDOUT_FILENO) == -1 ) return -1;
    if ( dup2(p_err[WRITE], STDERR_FILENO) == -1 ) return -1;
    close(p_in[WRITE]);
    close(p_out[READ]);
    close(p_err[READ]);
    errno = 0;
    return execlp(shell, shell, NULL);
  }

  // Parent process
  close(p_in[READ]);
  close(p_out[WRITE]);
  close(p_err[WRITE]);
  errno = 0;

  write(p_in[WRITE], script.data(), script.size());
  close(p_in[WRITE]);

  const unsigned int MS_1NS_VAL = 1000000;
  const unsigned int MS_30S_VAL = 30 * 1000;
  struct timeval timeout;
  timeout.tv_sec  = 0;
  timeout.tv_usec = 10000; // 0.01 ms
  int64_t timeout_ms = MS_30S_VAL;
  int64_t timeout_ns = MS_1NS_VAL;

  while( !exited && timeout_ms > 0 ) {

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(p_out[READ], &readfds);
    FD_SET(p_err[READ], &readfds);
    struct timeval this_timeout;
    memcpy(&this_timeout, &timeout, sizeof(struct timeval));

    int ready = select(p_err[READ]+1, &readfds, NULL, NULL, &this_timeout);
    if ( ready == 0
      || (ready == -1 && (errno == EINTR || errno == EAGAIN))
    ) {
      // timeout
      timeout_ms -= (timeout.tv_sec * 1000);
      timeout_ns -= timeout.tv_usec;
      if ( timeout_ns <= 0 ) {
        timeout_ms -= 1000;
        timeout_ns = MS_1NS_VAL;
      }
      continue;
    }

    if ( ready == -1 ) {
      cout << "Err: Reading from " << pid << endl;
      break;
    }

    int bytes_read = 0;
    if ( FD_ISSET(p_out[READ], &readfds) )
      bytes_read += read_into(out, p_out[READ]);
    if ( FD_ISSET(p_err[READ], &readfds) )
      bytes_read += read_into(err, p_err[READ]);

    if ( bytes_read > 0 ) {
      // Reset when we read data
      timeout_ms = MS_30S_VAL;
    }

    int ret = checkReturn(pid, &exit_code);
    if ( ret == 0 && exit_code >= 0 ) {
      exited = true;
    } else if ( ret == 0 && errno == EAGAIN ) {
      errno = 0;
    } else if ( ret == -1 ) {
      cout << "Err: Abnormal exit from " << pid << endl;
      exited = true;
    }
  }

  if ( timeout_ms <= 0 && exited == false ) {
    cout << "Err: Timeout exhausted, killing " << pid << endl;
    kill(pid, SIGKILL);
    errno = 0;
  }

  close(p_in[WRITE]);
  close(p_out[READ]);
  close(p_err[READ]);
  if ( exit_code == 0 )
    errno = 0;

  return 0;
}

string argcat(int argc, char **argv) {
  string s;
  for ( int i = 0; i < argc; i++ ) {
    if ( !s.empty() ) s += " ";
    s += string(argv[i]);
  }
  return s;
}

int main(int argc, char **argv) {
  string shell = argcat(argc-1, argv+1);
  vector<uint8_t> err, out;
  int ret = check_output(shell, out, err);
  if ( ret != 0 ) {
    cout << "Err: " << strerror(errno) << " (" << errno << ")" << endl;
  } else {
    cout
    << "[STDOUT]"
      << endl
    << string(out.begin(), out.end())
    //  << endl
    << "[STDERR]"
      << endl
    << string(err.begin(), err.end())
    ; //  << endl;
  }

  return ret;
}
