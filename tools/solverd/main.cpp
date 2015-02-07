/* -*- mode: c++; c-basic-offset: 2; -*- */

#include <cassert>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <list>

#include <unistd.h>
#include <netdb.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/file.h>
#include <arpa/inet.h>

#include "klee/SolverFormat.h"

#define SOLVED_BACKLOG     100

#define EXIT_OKAY       0
#define EXIT_ERROR      1
#define EXIT_DISCONNECT 2
#define EXIT_TIMEOUT    3

using namespace std;
using namespace klee;

// global options given from the user
int verbose_level;
const char* stp_bin_path;
const char* stp_bin_name;
const char* cache_dir;

// global parameters obtained during run-time
pid_t listen_pid;
int listen_fd; // listening socket; global so that quit() can close it
int shmid;

struct timeval query_started;

struct Statistics
{
  int nCacheHit;
  int nValid;
  int nInvalid;
  void reset() { nCacheHit = 0; nValid = 0; nInvalid = 0; }
};
Statistics* stats;

void printResult(const char* result)
{
  struct timeval query_finished, diff;
  gettimeofday(&query_finished, NULL);
  timersub(&query_finished, &query_started, &diff);

  char time[sizeof("HH:MM:SS.sss")];
  strftime(time, sizeof(time), "%H:%M:%S", localtime(&query_started.tv_sec));
  sprintf(time + 8, ".%03d", static_cast<int>(query_started.tv_usec / 1000));

  if (verbose_level) {
    printf("%s", time);
    printf("  %3d.%03d", static_cast<int>(diff.tv_sec), static_cast<int>(diff.tv_usec / 1000));

    if (stats)
      printf("  %-10s (C=%d V=%d I=%d)\n",
              result, stats->nCacheHit, stats->nValid, stats->nInvalid);
    else
      printf("  %s\n", result);
  }
  else if (isatty(STDOUT_FILENO)) {
    printf("last query: %s", time);
    printf("  %3d.%03d", static_cast<int>(diff.tv_sec), static_cast<int>(diff.tv_usec / 1000));

    if (stats)
      printf("  (C=%d V=%d I=%d)\r",
              stats->nCacheHit, stats->nValid, stats->nInvalid);
    else
      printf("\r");
  }
}

// signal handler for SIGINT
void quit(int x) {
  fprintf(stderr, "\nCtrl+C received... quitting.\n");
  if (close(listen_fd) < 0)
    perror("close() of listening socket");
  if (shmdt(stats) < 0)
    perror("shmdt()");
  if (getpid() == listen_pid) {
    if (shmctl(shmid, IPC_RMID, NULL) < 0)
      perror("shmctl(IPC_RMID)");
  }
  exit(EXIT_OKAY);
}

inline bool skip_hex_prefix(char*& p)
{
  if (strncmp(p, "0x", 2) == 0) {
    p += 2; return true;
  }
  if (strncmp(p, "0hex", 4) == 0) {
    p += 4; return true;
  }
  return false;
}

char* skip_validity(char* validity)
{
  char* p = strchrnul(validity, '\n');

  // strip the newline and any periods at the end of the line
  *p = '\0';
  for (char* q = p; q > validity && *--q == '.'; )
    *q = '\0';

  // advance cursor past the newline
  ++p;

  return p;
}

// parses STP's output and prepares response packet to send to KLEE client
bool parseResult(const std::string& stpOutput, std::vector<char>& response) {
  // Make a copy of stpOutput so we can pass it through strtok.
  const char* begin = stpOutput.c_str();
  const char* end = begin + stpOutput.length() + 1; // include the null terminator
  std::vector<char> copy(begin, end);
  char* p = copy.data();
  char* validity = NULL;

  // detect the validity string at the beginning
  if (*p != 'A') {
    validity = p;
    p = skip_validity(validity);
  }

  // tokenize and parse counter-examples
  list<CexItem> listCex;
  const char* delim = " =[]);\n";
  // *p == 'A': a very crude way of detecting "ASSERT(" at the line beginnings
  for (p = strtok(p, delim); p && *p == 'A'; p = strtok(NULL, delim)) {
    // allocate another counter-example struct
    listCex.push_back(CexItem());
    CexItem& cex = listCex.back();

    // ignore "ASSERT("

    p = strtok(NULL, delim);
    if (!p)
      return false;

    // parse object ID from "[const_]arrXXXXX"
    static const char* const const_prefix = "const_";
    static const size_t const_prefix_len = strlen(const_prefix);
    bool const_prefixed = false;
    if (strncmp(p, const_prefix, const_prefix_len) == 0) {
      p += const_prefix_len;
      const_prefixed = true;
    }
    if (!sscanf(p, "arr%u", &cex.id))
      return false;
    if (const_prefixed)
      cex.id |= CexItem::const_flag;

    p = strtok(NULL, delim);
    if (!p)
      return false;

    // parse array index
    if (!skip_hex_prefix(p) || !sscanf(p, "%x", &cex.offset))
      return false;

    p = strtok(NULL, delim);
    if (!p)
      return false;

    // parse value
    unsigned short v;
    if (!skip_hex_prefix(p) || !sscanf(p, "%hx", &v) || v > UCHAR_MAX)
      return false;
    cex.value = static_cast<unsigned char>(v);
  }

  // detect the validity string at the end
  if (p) {
    if (validity) {
      // duplicate? -- unexpected
      return false;
    }
    validity = p;
    skip_validity(validity);
  }

  // done parsing; time to assemble the response packet
  response.resize(sizeof(STPResHdr) + sizeof(CexItem) * listCex.size());
  STPResHdr* pktHeader = reinterpret_cast<STPResHdr*>(response.data());

  // parse STP result
  if (!validity)
    return false;
  else if (strcmp(validity, "Valid") == 0)
    pktHeader->result = 'V', ++stats->nValid;
  else if (strcmp(validity, "Invalid") == 0)
    pktHeader->result = 'I', ++stats->nInvalid;
  else // unexpected STP response
    return false;

  printResult(validity);

  pktHeader->rows = htonl(listCex.size());

  // copy counter-examples to packet buffer
  CexItem* cex = reinterpret_cast<CexItem*>(pktHeader + 1);
  for (list<CexItem>::iterator i = listCex.begin(); i != listCex.end(); ++i, ++cex) {
    if (verbose_level >= 2) {
      fprintf(stdout, "Cex item: (id=%u, offset=%u, value=%hu)\n", 
	      i->id, i->offset, i->value);
    }
    *cex = *i;
    cex->id = htonl(cex->id);
    cex->offset = htonl(cex->offset);
  }

  // success
  return true;
}

// wrapper for recv()
void recvall(int fd, void* vp, size_t sz) {
  char* cp = static_cast<char*>(vp);
  ssize_t nl = sz;
  while (nl > 0) {
    ssize_t n = recv(fd, cp, nl, 0);
    if (n < 0) {
      perror("recv()");
      close(fd);
      exit(EXIT_ERROR);
    }
    else if (n == 0) { // client disconnected
      close(fd);
      fputs("client disconnected during recv()\n", stderr);
      exit(EXIT_DISCONNECT);
    }
    nl -= n;
    cp += n;
  }
}

// wrapper for send()
void sendall(int fd, const void* vp, size_t sz) {
  const char* cp = static_cast<const char*>(vp);
  ssize_t nl = sz;
  
  while (nl > 0) {
    ssize_t n = send(fd, cp, nl, 0);
    if (n < 0) {
      perror("send()");
      close(fd);
      exit(EXIT_ERROR);
    }
    else if (n == 0) { // client disconnected
      close(fd);
      fputs("client disconnected during send()\n", stderr);
      exit(EXIT_DISCONNECT);
    }
    nl -= n;
    cp += n;
  }
}

const char* printHexHashPath(const unsigned char* hash, unsigned int hashlen)
{
  static std::vector<char> path(strlen(cache_dir)+1+2*EVP_MAX_MD_SIZE+1);
  assert(hashlen <= EVP_MAX_MD_SIZE);
  char* p = path.data();
  p += sprintf(p, "%s/", cache_dir);
  for (unsigned int i = 0; i < hashlen; i++)
    p += sprintf(p, "%02x", hash[i]);
  *p = '\0';
  return path.data();
}

class FileDescriptor
{
  int fd_;
public:
  FileDescriptor(int n0) : fd_(n0) { }
  int close() { assert(fd_ >= 0); int r = ::close(fd_); fd_ = -1; return r; }
  ~FileDescriptor() { if (fd_ >= 0) close(); }
  operator int() const { return fd_; }
};

bool cacheLookup(const char* path, std::vector<char>& contents) {

  FileDescriptor fd = open(path, O_RDONLY);
  if (fd < 0 && errno == ENOENT) // cache miss
    return false;
  else if (fd < 0 && errno != ENOENT) {
    perror("open()");
    return false;
  }

  // get shared (read) lock
  if (flock(fd, LOCK_SH) < 0) {
    perror("flock()");
    return false;
  }

  uint32_t size;
  ssize_t n = read(fd, &size, sizeof(size));
  if (n < 0) {
    perror("read()");
    return false;
  }
  else if (n != sizeof(size)) {
    fprintf(stderr, "cache entry corrupted\n");
    return false;
  }
  contents.resize(size);

  n = read(fd, contents.data(), size);
  if (n < 0) {
    perror("read()");
    return false;
  }
  else if (static_cast<size_t>(n) != size) {
    fprintf(stderr, "cache entry corrupted\n");
    return false;
  }

  if (flock(fd, LOCK_UN) < 0) {
    perror("flock() to unlock");
    return false;
  }
  
  if (fd.close() < 0) {
    perror("close()");
    return false;
  }

  return true;
}

bool cacheInsert(const char *path, const std::vector<char>& contents) {
  if (mkdir(cache_dir, S_IRWXU | S_IRWXG) == -1 && errno != EEXIST) {
    perror("mkdir()");
    return false;
  }

  FileDescriptor fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0660);
  if (fd < 0) {
    perror("open()");
    return false;
  }

  // get exclusive (write) lock
  if (flock(fd, LOCK_EX) < 0) {
    perror("flock()");
    return false;
  }

  uint32_t size = contents.size();
  ssize_t n = write(fd, &size, sizeof(size));
  if (n < 0 || static_cast<size_t>(n) != sizeof(size)) {
    perror("write()");
    return false;
  }

  n = write(fd, contents.data(), contents.size());
  if (n < 0 || static_cast<size_t>(n) != contents.size()) {
    perror("write()");
    return false;
  }

  if (flock(fd, LOCK_UN) < 0) {
    perror("flock() to unlock");
    return false;
  }
  
  if (close(fd) < 0) {
    perror("close()");
    return false;
  }
  
  return true;
}

bool calculateHash(const std::vector<char>& str, unsigned char* hash, unsigned int& hashlen)
{
  bool success = false;
  EVP_MD_CTX mdctx;
  EVP_MD_CTX_init(&mdctx);

  if (!EVP_DigestInit_ex(&mdctx, EVP_md5(), NULL)) {
    fprintf(stderr, "EVP_DigestInit_ex() failed\n");
    goto exit;
  }
  if (!EVP_DigestUpdate(&mdctx, str.data(), str.size())) {
    fprintf(stderr, "EVP_DigestUpdate() failed\n");
    goto exit;
  }
  if (!EVP_DigestFinal_ex(&mdctx, hash, &hashlen)) {
    fprintf(stderr, "EVP_DigestFinal() failed\n");
    goto exit;
  }
  success = true;

exit:
  EVP_MD_CTX_cleanup(&mdctx);
  return success;
}

int talkToChild(pid_t pid, int infd, int outfd, int sockfd, const timeval& tTimeout,
    const std::vector<char>& query, std::vector<char>& response)
{
  timeval tNow, tExpire;
  gettimeofday(&tNow, NULL);
  timeradd(&tNow, &tTimeout, &tExpire);

  // we have to send the query to STP, but
  // we don't want to overflow STP's STDIN buffer

  const char* head = query.data();
  size_t left = query.size();
  std::string stpOutput;
  const int maxfd = std::max(infd, outfd);

  while (true) {
   // reset file descriptor sets
    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);

    // add STP's STDOUT and (if necessary) STDIN to r/w sets
    FD_SET(infd, &rfds);
    if (left > 0)
      FD_SET(outfd, &wfds);

    // calculate time remaining until timeout
    timeval tToGo, *pToGo;
    if (timerisset(&tTimeout)) {
      timeval tNow;
      gettimeofday(&tNow, NULL);
      timersub(&tExpire, &tNow, &tToGo);
      pToGo = &tToGo;
    }
    else
      pToGo = NULL;

    // block until pipe(s) are ready
    int r = select(maxfd + 1, &rfds, &wfds, NULL, pToGo);
    if (r < 0) {
      perror("select()");
      return EXIT_ERROR;
    }
    else if (r == 0) {
      if (kill(pid, SIGKILL) < 0)
        perror("kill()");
      printResult("timed out");
      return EXIT_TIMEOUT;
    }

    // write to STP's STDIN
    if (FD_ISSET(outfd, &wfds)) {
      ssize_t n = write(outfd, head, std::min<size_t>(left, BUFSIZ));
      if (n < 0) {
        perror("write()");
        return EXIT_ERROR;
      }
      left -= n;
      head += n;
      if (left == 0)
        close(outfd);
    }

    // read from STP's STDOUT
    if (FD_ISSET(infd, &rfds)) {
      char buf[BUFSIZ];
      ssize_t n = read(infd, buf, sizeof(buf));
      if (n < 0) {
        perror("read()");
        return EXIT_ERROR;
      }
      else if (n == 0) { // STP terminated (EOF)
        if (!parseResult(stpOutput, response)) {
          fprintf(stderr, "cannot parse STP output:\n*****\n%s\n*****\n" \
            "On query:\n%.*s\n----------\n", stpOutput.c_str(), static_cast<int>(query.size()), query.data());
          return EXIT_ERROR;
        }

        sendall(sockfd, response.data(), response.size());
        close(infd);
        return EXIT_OKAY;
      }
      else { // EOF not reached yet
        stpOutput.append(buf, n);
      }
    }
  }
}

// main request handler; called after accept()
void processRequest(int fd) {
  // receive length of query
  STPReqHdr stpRequest;
  recvall(fd, &stpRequest, sizeof(stpRequest));
  stpRequest.timeout_sec = ntohl(stpRequest.timeout_sec);
  stpRequest.timeout_usec = ntohl(stpRequest.timeout_usec);
  stpRequest.length = ntohl(stpRequest.length);

  // receive query
  std::vector<char> query(stpRequest.length);
  recvall(fd, query.data(), query.size());

  // calculate md5 hash
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hashlen;
  if (!calculateHash(query, hash, hashlen)) {
    close(fd);
    exit(EXIT_ERROR);
  }
  const char *path = printHexHashPath(hash, hashlen);

  gettimeofday(&query_started, NULL);
  std::vector<char> cache;
  if (cacheLookup(path, cache)) {
    ++stats->nCacheHit;
    printResult("Cache Hit");
    sendall(fd, cache.data(), cache.size());
  }
  else {
    // restore default signal handler before forking
    signal(SIGCHLD, SIG_DFL);

    // prepare pipes to STP process
    // The parent writes to p2c[1], which the child reads from p2c[0].
    // The child writes to c2p[1], which the parent reads from c2p[0].
    int p2c[2], c2p[2];
    if (pipe(p2c) < 0 || pipe(c2p) < 0) {
      perror("pipe()");
      close(fd);
      exit(EXIT_ERROR);
    }

    // fork STP process
    pid_t pid = fork();
    if (pid < 0) {
      perror("fork()");
      close(fd);
      exit(EXIT_ERROR);
    }
    else if (pid == 0) { // child process
      // close child's unused ends of the pipes
      close(p2c[1]);
      close(c2p[0]);

      // redirect stderr to /dev/null
      if (!freopen("/dev/null", "w", stderr)) {
        perror("freopen()");
        exit(EXIT_ERROR);
      }

      // point STDIN/STDOUT to pipes
      if (dup2(p2c[0], STDIN_FILENO) < 0 || dup2(c2p[1], STDOUT_FILENO) < 0) {
        perror("dup2()");
        exit(EXIT_ERROR);
      }

      // run STP; never returns
      if (execl(stp_bin_path, stp_bin_name, "-c", "-p", NULL) < 0) {
        // restore stderr so we can see this error
        if (!freopen( "/dev/tty", "w", stderr)) {
          perror("freopen()");
          exit(EXIT_ERROR);
        }
        perror("execl()");
        exit(EXIT_ERROR);
      }

      // we should never get here
      exit(EXIT_ERROR);
    }
    else { // parent process
      // close parent's unused ends of the pipes
      close(p2c[0]);
      close(c2p[1]);

      // prepare timeout
      timeval tTimeout;
      tTimeout.tv_sec = stpRequest.timeout_sec;
      tTimeout.tv_usec = stpRequest.timeout_usec;

      std::vector<char> response;
      int r = talkToChild(pid, c2p[0], p2c[1], fd, tTimeout, query, response);
      if (r == EXIT_OKAY)
        cacheInsert(path, response);
      close(fd);
      exit(r);
    }
  }

  close(fd);
  exit(EXIT_OKAY);
}

bool parse_args(int argc, char** argv, uint16_t& port)
{
  ++argv, --argc;

  if (argc <= 0)
    return false;

  if ((*argv)[0] == '-') {
    if (strcmp(*argv, "-vv") == 0)
      verbose_level = 2;
    else if (strcmp(*argv, "-v") == 0)
      verbose_level = 1;
    else
      return false;
    ++argv, --argc;
  }

  if (argc <= 0)
    return false;

  unsigned long n = strtoul(*argv, NULL, 0);
  if (n >= 0x10000UL || n == 0) {
    fprintf(stderr, "Invalid port number\n");
    exit(EXIT_ERROR);
  }
  port = n;
  ++argv, --argc;

  if (argc <= 0)
    return false;

  stp_bin_path = *argv;
  const char* p = strrchr(stp_bin_path, '/');
  stp_bin_name = p ? p + 1 : stp_bin_path;
  ++argv, --argc;

  if (argc <= 0)
    return false;

  cache_dir = *argv;
  ++argv, --argc;

  if (argc > 0)
    return false;

  return true;
}

int main(int argc, char **argv)
{
  uint16_t port;
  
  // parse command-line arguments
  if (!parse_args(argc, argv, port)) {
    fprintf(stderr, "Usage: %s [-v|-vv] <port> <stp_bin_path> <cache_dir>\n", argv[0]);
    exit(EXIT_ERROR);
  }

  // create a shared memory for statistics
  shmid = shmget(IPC_PRIVATE, sizeof(Statistics), IPC_CREAT | IPC_EXCL | 0600);
  if (shmid < 0)
    perror("shmget()");
  else {
    void* p = shmat(shmid, NULL, 0);
    if (p == (void*) -1)
      perror("shmat()");
    else {
      stats = static_cast<Statistics*>(p);
      stats->reset();
    }
  }

  // prepare socket
  listen_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (listen_fd == -1) {
    perror("socket()");
    exit(EXIT_ERROR);
  }

  // allow reuse of socket to prevent bind() errors
  const unsigned int val = true;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) == -1) {
    perror("setsockopt()");
    exit(EXIT_ERROR);
  }

  // initialize sockaddr struct
  struct sockaddr_in sin_addr;
  memset(&sin_addr, 0, sizeof(sin_addr));
  sin_addr.sin_family = AF_INET;
  sin_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  sin_addr.sin_port = htons(port);

  // bind socket to port
  if (bind(listen_fd, (const struct sockaddr*) &sin_addr, sizeof(sin_addr)) == -1) {
    perror("bind()");
    exit(EXIT_ERROR);
  }

  if (listen(listen_fd, SOLVED_BACKLOG) == -1) {
    perror("listen()");
    exit(EXIT_ERROR);
  }

  if (verbose_level) {
    fprintf(stderr, "Listening on port %hu...\n", static_cast<unsigned short>(port));
    fflush(stderr);
  }

  listen_pid = getpid();

  // set up ctrl+c (SIGINT) handler
  struct sigaction sigQuit;
  sigQuit.sa_handler = quit;
  sigemptyset(&sigQuit.sa_mask);
  sigQuit.sa_flags = SA_RESTART | SA_ONESHOT;
  if (sigaction(SIGINT, &sigQuit, NULL) < 0)
  {
    perror("sigaction()");
    exit(EXIT_ERROR);
  }

  // prevent children from becoming zombies
  signal(SIGCHLD, SIG_IGN);

  // main loop; never breaks
  while (true) {
    // accept incoming connection
    int conn_fd = accept(listen_fd, NULL, NULL);
    if (conn_fd < 0) {
      perror("accept()");
      exit(EXIT_ERROR);
    }

    // fork to process connection
    pid_t pid = fork();
    if (pid < 0) {
      perror("fork()");
      close(conn_fd);
      exit(EXIT_ERROR);
    }
    else if (pid == 0) // child process
      processRequest(conn_fd); // never returns
    else // parent process
      close(conn_fd);
  }

  return 0;
}
