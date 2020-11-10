#include <iostream>
#include <string>
#include <errno.h>
#include <string.h>

#include "broker.h"

using std::cout;
using std::endl;
using std::string;

string argcat(int argc, char **argv) {
  string s;
  for ( int i = 0; i < argc; i++ ) {
    if ( !s.empty() ) s += " ";
    s += string(argv[i]);
  }
  return s;
}

int main(int argc, char **argv) {
  string script = argcat(argc-1, argv+1);

  shell_broker ipc;
  if ( ipc.start() == 0 ) {

    ipc.write_to(script);

    if ( ipc.read_from() == 0 ) {

      cout << "[RETURN] " << ipc.returncode
        << endl
      << "[STDOUT]"
        << endl
      << string(ipc.data.begin(), ipc.data.end())
      //  << endl
      << "[STDERR]"
        << endl
      << string(ipc.error.begin(), ipc.error.end())
      ; //  << endl;

    } else {
      cout << "Err: " << strerror(errno) << " (" << errno << ")" << endl;
    }

  }

  return ipc.returncode;
}
