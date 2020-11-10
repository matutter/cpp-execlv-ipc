#ifndef SUBPROCESS_BROKER_H
#define SUBPROCESS_BROKER_H

#include <inttypes.h>
#include <vector>
#include <string>

struct broker_ipc {

  int64_t timeout_ms = 30000;
  int returncode = -1;
  bool is_nonblocking = true;
  int pid;
  int in[2];
  int out[2];
  int err[2];
  int fd_max;
  std::vector<uint8_t> data;
  std::vector<uint8_t> error;

  broker_ipc();
  int start();
  virtual int read_from();
  virtual int write_to(std::string&);
  virtual int write_to(std::vector<uint8_t>&);
  virtual int at_fork() = 0;

};

struct shell_broker : broker_ipc {
  int at_fork();
  using broker_ipc::write_to;
  int write_to(std::vector<uint8_t>&);
};

#endif
