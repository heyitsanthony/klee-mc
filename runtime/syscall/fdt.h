#pragma once
#include <string>
#include <map>
#include <vector>
#include <sys/socket.h>
#include <klee/klee.h>

//#define METHOD_UNIMPLEMENTED { std::ostringstream o; o << __PRETTY_FUNCTION__ << " unimplemented for file " << identifier_; throw std::runtime_error(o.str()); }
#define METHOD_UNIMPLEMENTED { klee_warning("VIRTUALDEATH"); return -1; }

class FDT;
class FD {
protected:
	FD(const std::string& id);
public:
	virtual ~FD() {}
	const std::string identifier_;
	//core file ops
	virtual long read(void* buf, size_t sz) METHOD_UNIMPLEMENTED;
	virtual long pread(void* buf, size_t sz, off_t off) METHOD_UNIMPLEMENTED;
	virtual long readv(const struct iovec *iov, int iovcnt) METHOD_UNIMPLEMENTED;
	virtual long write(const void* buf, size_t sz) METHOD_UNIMPLEMENTED;
	virtual long pwrite(const void* buf, size_t sz, off_t off) METHOD_UNIMPLEMENTED;
	virtual long writev(const struct iovec *iov, int iovcnt) METHOD_UNIMPLEMENTED;
	virtual long mmap(void* addr, size_t len, int prot, int flags, off_t offset) METHOD_UNIMPLEMENTED;
	virtual long stat(struct stat *buf) METHOD_UNIMPLEMENTED;
	virtual long truncate(off_t len) METHOD_UNIMPLEMENTED;
		
	//core socket ops
	virtual long recvfrom(void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len) METHOD_UNIMPLEMENTED;
	virtual long recvmsg(struct msghdr *message, int flags) METHOD_UNIMPLEMENTED;
	virtual long sendmsg(const struct msghdr *message, int flags) METHOD_UNIMPLEMENTED;
	virtual long sendto(const void *buffer, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len) METHOD_UNIMPLEMENTED;
	
	//generic fd ops
	virtual long fcntl(int cmd, long data) METHOD_UNIMPLEMENTED;
	virtual long ioctl(unsigned long request, long data) METHOD_UNIMPLEMENTED;
	
protected:
	long references_;
protected:
	friend class FDT;
	//returns true if this file should be deleted
	bool freeRef();
	void addRef();
};

class GarbageFD : public FD {
public:
	GarbageFD();

	long read(void* buf, size_t sz);
	long pread(void* buf, size_t sz, off_t off);
	long readv(const struct iovec *iov, int iovcnt);
	long write(const void* buf, size_t sz);
	long pwrite(const void* buf, size_t sz, off_t off);
	long writev(const struct iovec *iov, int iovcnt);
	long mmap(void* addr, size_t len, int prot, int flags, off_t offset);
	long stat(struct stat *buf);
	long truncate(off_t len);
	
	long recvfrom(void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len);
	long recvmsg(struct msghdr *message, int flags);
	long sendmsg(const struct msghdr *message, int flags);
	long sendto(const void *buffer, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
        
	long fcntl(int cmd, long data);
	long ioctl(unsigned long request, long data);
protected:
	friend class FDT;
};

class SymbolicFD : public FD {
public:
	SymbolicFD(const std::string& id);
	virtual long read(void* buf, size_t sz);
	virtual long pread(void* buf, size_t sz, off_t off);
	//implemented in terms of read
	virtual long readv(const struct iovec *iov, int iovcnt);
	virtual long write(const void* buf, size_t sz);
	virtual long pwrite(const void* buf, size_t sz, off_t off);
	//implemented in terms of write
	virtual long writev(const struct iovec *iov, int iovcnt);
	virtual long mmap(void* addr, size_t len, int prot, int flags, off_t offset);
	virtual long stat(struct stat *buf);
	virtual long truncate(off_t len);
	
	virtual long recvfrom(void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len);
	virtual long recvmsg(struct msghdr *message, int flags);
	virtual long sendmsg(const struct msghdr *message, int flags);
	virtual long sendto(const void *buffer, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);

	virtual long fcntl(int cmd, long data);
	virtual long ioctl(unsigned long request, long data);
};

class SymbolicFile : public SymbolicFD {
public:
	SymbolicFile(const std::string& identifier);
	virtual long recvfrom(void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len);
	virtual long recvmsg(struct msghdr *message, int flags);
	virtual long sendmsg(const struct msghdr *message, int flags);
	virtual long sendto(const void *buffer, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
};

class SymbolicSocket : public SymbolicFD {
public:
	SymbolicSocket();
	virtual long pread(void* buf, size_t sz, off_t off);
	virtual long pwrite(const void* buf, size_t sz, off_t off);
	virtual long mmap(void* addr, size_t len, int prot, int flags, off_t offset);
	virtual long stat(struct stat *buf);
	virtual long truncate(off_t len);
};
//either takes it all or errors, once it errors it keeps erroring
class SymbolicPipe : public SymbolicSocket {
public:
	SymbolicPipe();
	virtual long recvfrom(void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len);
	virtual long recvmsg(struct msghdr *message, int flags);
	virtual long sendmsg(const struct msghdr *message, int flags);
	virtual long sendto(const void *buffer, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
	virtual long read(void* buf, size_t sz);
	virtual long write(const void* buf, size_t sz);
private:
	bool done;
};
class NoisyPipe : public SymbolicPipe {
public:
	NoisyPipe();
	virtual long write(const void* buf, size_t sz);
};

class FDT {
	typedef std::vector<FD*> fdmap;
public:
	FDT();
	~FDT();
	long newFile(FD* file);
	long dupFile(long fd);
	long dupFile(long fd, long to);
	FD* getFile(long fd);
	FD* alwaysGetFile(long fd);
	long closeFile(long fd);
private:
	SymbolicPipe stdin;
	NoisyPipe stdout;
	NoisyPipe stderr;
	fdmap files_;
};

class VFS {
public:
	VFS();
	~VFS();
	virtual long access(const char* path, int mode);
	virtual long open(const char* path, int flags, int access);
	virtual long unlink(const char* path);
	virtual long stat(const char* path, struct stat *buf);
	virtual long truncate(const char* path, off_t len);
	virtual long rename(const char* from, const char* to);
	//DIRECTORY anything???
	virtual long chdir(const char* path);
};

//we're gonna keep these alive for the lifetime of the process,
//original will be a mmapped read only copy of the original
class DataFork {
	typedef std::vector<char> data_block;
	typedef std::map<off_t, data_block> delta_map;
public:
	virtual void write(const void* buf, size_t sz, off_t off);
	virtual size_t read(void* buf, size_t sz, off_t off);
	virtual off_t length() const;
	virtual void truncate(off_t len);
	virtual void stat(struct stat* buf);
	const std::string identifier_;
protected:
	friend class ConcreteVFS;
	DataFork(const std::string& id, const void* data, off_t size);
	const void* original_;
	const off_t originalSize_;
	off_t size_;
	delta_map differences_;
};

class SymbolicFork : public DataFork {
protected:
	friend class ConcreteVFS;
	SymbolicFork(const std::string& id);
public:
	void write(const void* buf, size_t sz, off_t off);
	size_t read(void* buf, size_t sz, off_t off);
};

class ConcreteFile : public SymbolicFile {
public:
	ConcreteFile(DataFork* fork);
	virtual long read(void* buf, size_t sz);
	virtual long pread(void* buf, size_t sz, off_t off);
	virtual long write(const void* buf, size_t sz);
	virtual long pwrite(const void* buf, size_t sz, off_t off);
	virtual long mmap(void* addr, size_t len, int prot, int flags, off_t offset);
	virtual long stat(struct stat *buf);
	virtual long truncate(off_t len);
	virtual long fcntl(int cmd, long data);
	virtual long ioctl(unsigned long request, long data);
protected:
	DataFork* fork_;
	off_t offset_;
};

class ConcreteVFS : public VFS {
	typedef std::map<std::string, DataFork*> fork_map;
public:
	ConcreteVFS();
	~ConcreteVFS();
	long access(const char* path, int mode);
	long open(const char* path, int flags, int access);
	long unlink(const char* path);
	long stat(const char* path, struct stat *buf);
	long truncate(const char* path, off_t len);
	long rename(const char* from, const char* to);
	long chdir(const char* path);
protected:
	bool shouldBeConcrete(const char* p, std::string& resolved);
	long getFork(const std::string& path, DataFork*& i);
	fork_map forks_;
	std::string cwd_;
};
extern FDT fdt;
// extern VFS vfs;
extern ConcreteVFS vfs;
