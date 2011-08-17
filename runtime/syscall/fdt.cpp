#include "fdt.h"
#include "kmc.h"
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <klee/klee.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <limits.h>

#define MIN_ERRNO -255

FD::FD(const std::string& id) 
: identifier_(id)
, references_(0) 
{}
void FD::addRef() {
	references_++;
}
bool FD::freeRef() {
	return --references_ == 0;
}

GarbageFD::GarbageFD() 
: FD("BAD file descriptor")
{
}

long GarbageFD::read(void* buf, size_t sz) { return -EBADF; }
long GarbageFD::pread(void* buf, size_t sz, off_t off) { return -EBADF; }
long GarbageFD::readv(const struct iovec *iov, int iovcnt) { return -EBADF; }
long GarbageFD::write(const void* buf, size_t sz) { return -EBADF; }
long GarbageFD::pwrite(const void* buf, size_t sz, off_t off) { return -EBADF; }
long GarbageFD::writev(const struct iovec *iov, int iovcnt) { return -EBADF; }
long GarbageFD::mmap(void* addr, size_t len, int prot, int flags, off_t offset) { return -EBADF; }
long GarbageFD::recvfrom(void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len) { return -EBADF; }
long GarbageFD::recvmsg(struct msghdr *message, int flags) { return -EBADF; }
long GarbageFD::sendmsg(const struct msghdr *message, int flags) { return -EBADF; }
long GarbageFD::sendto(const void *buffer, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len) { return -EBADF; }
long GarbageFD::fcntl(int cmd, long data) { return -EBADF; }
long GarbageFD::ioctl(unsigned long request, long data) { return -EBADF; }
long GarbageFD::stat(struct stat *buf) { return -EBADF; }

SymbolicFD::SymbolicFD(const std::string& id) 
: FD(id)
{
}

long SymbolicFD::read(void* buf, size_t sz) {
	long result;
	kmc_make_range_symbolic((uintptr_t)&result, sizeof(result), "SFD::read res");
	if(result >= sz) {
		kmc_make_range_symbolic((uintptr_t)buf, sz, "SFD::read data");
	} else if(result >= 0) {
		kmc_make_range_symbolic((uintptr_t)buf, result, "SFD::read data");
	} else if(result < MIN_ERRNO) {
		result = MIN_ERRNO;
	}
	return result;
}
long SymbolicFD::pread(void* buf, size_t sz, off_t off) {
	return SymbolicFD::read(buf, sz);
}
long SymbolicFD::readv(const struct iovec *iov, int iovcnt) {
	if(iovcnt <= 0)
		return -EINVAL;
	long bytes = 0;
	for(int i = 0; i < iovcnt; ++i) {
		long result = read(iov[i].iov_base, iov[i].iov_len);
		if(result < 0) {
			if(result < MIN_ERRNO) {
				result = MIN_ERRNO;
			}
			//if we fail on one buf, then return partial read
			if(bytes)
				return bytes;
			return result;
		}
		if(result == 0) {
			//reached the end of file
			return bytes;
		}
		//keep going
		bytes += result;
	}
	return bytes;
}
long SymbolicFD::write(const void* buf, size_t sz) {
	long result;
	kmc_make_range_symbolic((uintptr_t)&result, sizeof(result), "SFD::write res");
	klee_assume(result != 0);
	if(result >= sz) {
		return sz;
	} else if(result >= 0) {
	} else if(result < MIN_ERRNO) {
		result = MIN_ERRNO;
	}
	return result;
}
long SymbolicFD::pwrite(const void* buf, size_t sz, off_t off) {
	return SymbolicFD::write(buf, sz);
}
long SymbolicFD::writev(const struct iovec *iov, int iovcnt) {
	if(iovcnt <= 0)
		return -EINVAL;
	long bytes = 0;
	for(int i = 0; i < iovcnt; ++i) {
		long result = write(iov[i].iov_base, iov[i].iov_len);
		if(result < 0) {
			if(result < MIN_ERRNO) {
				result = MIN_ERRNO;
			}
			//if we fail on one buf, then return partial read
			if(bytes)
				return bytes;
			return result;
		}
		//keep going
		bytes += result;
	}
	return bytes;
}
long SymbolicFD::mmap(void* addr, size_t len, int prot, int flags, off_t offset) {
	klee_warning("symbolic memory mapped");
	//hmm we could make this return errors to punish the program 
	//but that is probably not fruitful
	bool is_himem;

	is_himem = (((intptr_t)addr & ~0x7fffffffffffULL) != 0);
	
	if(addr) {
		addr = (void*)concretize_u64((uint64_t)addr);
		klee_define_fixed_object(addr, len);
		return (long)addr;
	}

	/* toss back whatever */
	addr = kmc_alloc_aligned(len, "mmap");
	if (addr == NULL) addr = MAP_FAILED;
	return (long)addr;
}
long SymbolicFD::stat(struct stat *buf) {
	long result;
	kmc_make_range_symbolic((uintptr_t)&result, sizeof(result), "SFD::stat res");
	if(result >= 0) {
		result = 0;
		kmc_make_range_symbolic((uintptr_t)buf, sizeof(*buf), "SFD::stat data");
	} else if(result < MIN_ERRNO) {
		result = MIN_ERRNO;
	}
	return result;
}
long SymbolicFD::truncate(off_t len) {
	long result;
	kmc_make_range_symbolic((uintptr_t)&result, sizeof(result), "SFD::truncate res");
	if(result >= 0) {
		result = 0;
	} else if(result < MIN_ERRNO) {
		result = MIN_ERRNO;
	}
	return result;
}

long SymbolicFD::recvfrom(void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len) { 
	return SymbolicFD::read(buffer, length);
}
long SymbolicFD::recvmsg(struct msghdr *message, int flags) {
	//TODO: cmsghdr for control stuff
	return SymbolicFD::readv(message->msg_iov, message->msg_iovlen);
}
long SymbolicFD::sendmsg(const struct msghdr *message, int flags) {
	return SymbolicFD::writev(message->msg_iov, message->msg_iovlen);
}
long SymbolicFD::sendto(const void *buffer, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len) {
	return SymbolicFD::write(buffer, length);
}
long SymbolicFD::fcntl(int cmd, long data) { 
	//TODO: handle actual things
	long result;
	kmc_make_range_symbolic((uintptr_t)&result, sizeof(result), "SFD::fcntl res");
	return result;
}
long SymbolicFD::ioctl(unsigned long request, long data) { 
	//TODO: handle actual things
	long result;
	kmc_make_range_symbolic((uintptr_t)&result, sizeof(result), "SFD::ioctl res");
	return result;
}



SymbolicFile::SymbolicFile(const std::string& id) 
: SymbolicFD(id)
{
}

long SymbolicFile::recvfrom(void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len) { return -ENOTSOCK; }
long SymbolicFile::recvmsg(struct msghdr *message, int flags) { return -ENOTSOCK; }
long SymbolicFile::sendmsg(const struct msghdr *message, int flags) { return -ENOTSOCK; }
long SymbolicFile::sendto(const void *buffer, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len) { return -ENOTSOCK; }
long SymbolicFile::stat(struct stat* buf) {
	kmc_make_range_symbolic((uintptr_t)buf, sizeof(*buf), "Data::stat buf ");
	klee_assume(buf->st_nlink > 0);
	buf->st_mode &= ~S_IFSOCK;
	buf->st_mode |= S_IFREG;
	buf->st_blocks = (buf->st_size + 511) >> 9;
	buf->st_blksize = 512;
	return 0;
}

SymbolicSocket::SymbolicSocket() 
: SymbolicFD("Symbolic Socket")
{}

long SymbolicSocket::pread(void* buf, size_t sz, off_t off) { return -ESPIPE; }
long SymbolicSocket::pwrite(const void* buf, size_t sz, off_t off) { return -ESPIPE; }
long SymbolicSocket::mmap(void* addr, size_t len, int prot, int flags, off_t offset) { return -EINVAL; }
long SymbolicSocket::stat(struct stat *buf) { 
	kmc_make_range_symbolic((uintptr_t)buf, sizeof(*buf), "Data::stat buf ");
	buf->st_mode &= ~S_IFREG;
	buf->st_mode |= S_IFSOCK;
	return 0;
}
long SymbolicSocket::truncate(off_t len) { return -EINVAL; }

SymbolicPipe::SymbolicPipe() 
: SymbolicSocket()
, done(false)
{}
long SymbolicPipe::recvfrom(void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len) { return -ENOTSOCK; }
long SymbolicPipe::recvmsg(struct msghdr *message, int flags) { return -ENOTSOCK; }
long SymbolicPipe::sendmsg(const struct msghdr *message, int flags) { return -ENOTSOCK; }
long SymbolicPipe::sendto(const void *buffer, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len) { return -ENOTSOCK; }
long SymbolicPipe::read(void* buf, size_t sz) {
	if(done)
		return 0;
	long result;
	kmc_make_range_symbolic((uintptr_t)&result, sizeof(result), "SFD::read res");
	if(result >= 0) {
		kmc_make_range_symbolic((uintptr_t)buf, sz, "SFD::read data");
		return sz;
	} else {
		done = true;
		return 0;
	}
}
long SymbolicPipe::write(const void* buf, size_t sz) {
	return sz;
}

NoisyPipe::NoisyPipe() 
: SymbolicPipe()
{}
long NoisyPipe::write(const void* buf, size_t sz) {
	klee_warning("noisypipe");
	char* mbuf = new char[sz + 1];
	memcpy(mbuf, buf, sz);
	mbuf[sz] = 0;
	klee_warning(mbuf);
	return SymbolicPipe::write(buf, sz);
}

ConcreteFile::ConcreteFile(DataFork* fork) 
: SymbolicFile(fork->identifier_)
, fork_(fork)
, offset_(0)
{
}
long ConcreteFile::read(void* buf, size_t sz) {	
	long result = ConcreteFile::pread(buf, sz, offset_);
	offset_ += result;
	return result;
}
long ConcreteFile::pread(void* buf, size_t sz, off_t off) {	
	return fork_->read(buf, sz, off);
}
long ConcreteFile::write(const void* buf, size_t sz) {	
	offset_ += ConcreteFile::pwrite(buf, sz, offset_);
	return 0;
}
long ConcreteFile::pwrite(const void* buf, size_t sz, off_t off) {	
	fork_->write(buf, sz, off);
	return sz;
}
long ConcreteFile::mmap(void* addr, size_t len, int prot, int flags, off_t offset) {
	if(flags & MAP_SHARED) {
		klee_warning("trying to memmap data shared, just mapping private");
	}
	klee_warning("concrete memory mapped");
	klee_warning(identifier_.c_str());

	//TODO: real protection?
	//TODO: proper rounding to pages/offsets etc
	//TODO: fail it if it would overlap etc
	if(addr) {
		klee_define_fixed_object(addr, len);
		fork_->read(addr, len, offset);
		return (uint64_t)addr;
	}
	

	/* toss back whatever */
	addr = kmc_alloc_aligned(len, "mmap", false);
	if (addr == NULL) 
		return -ENOMEM;
		
	fork_->read(addr, len, offset);
	klee_warning("copied");
	return (uint64_t)addr;
}
long ConcreteFile::stat(struct stat *buf) { 
	fork_->stat(buf);
	return 0;
}

long ConcreteFile::truncate(off_t len) {
	fork_->truncate(len);
	return 0;
}
long ConcreteFile::fcntl(int cmd, long data) {	
	return SymbolicFD::fcntl(cmd, data);
}
long ConcreteFile::ioctl(unsigned long request, long data) {	
	return SymbolicFD::ioctl(request, data);
}


FDT::FDT() 
: files_(4096)
{
	SymbolicPipe* stdin = new SymbolicPipe();
	files_[0] = stdin;
	stdin->addRef();

	NoisyPipe* stdout = new NoisyPipe();
	files_[1] = stdout;
	stdout->addRef();

	NoisyPipe* stderr = new NoisyPipe();
	files_[2] = stderr;
	stderr->addRef();
}
FDT::~FDT() {
	klee_warning("destructors! fuck off");
}
long FDT::newFile(FD* file) {
	int hole = 0;
	for(; hole < files_.size(); ++hole)
		if(!files_[hole])
			break;
	klee_print_expr("will be", hole);
	file->addRef();
	if(hole < files_.size()) {
		files_[hole] = file;
		return hole;
	} else {
		if(file->freeRef())
			delete file;
		return -EMFILE;
	}
}
GarbageFD g_garbage_bag;
FD* FDT::alwaysGetFile(long fd) {
	FD* i = getFile(fd);
	if(!i) {
		return new GarbageFD;//&g_garbage_bag;
	} else {
		return i;
	}
}
FD* FDT::getFile(long fd) {
	if(fd >= files_.size())
		return NULL;
	return files_[fd];
}
long FDT::closeFile(long fd) {
	FD* i = getFile(fd);
	if(!i) {
		return -EBADF;
	}
	if(i->freeRef())
		delete i;
	files_[fd] = NULL;
	return 0;
}
long FDT::dupFile(long fd) {
	FD* f = getFile(fd);
	if(!f) {
		klee_warning("dup bad f");
		return -EBADF;
	}
	klee_warning("dup ok");
	return newFile(f);
}

long FDT::dupFile(long from, long to) {
	FD* f = getFile(from);
	if(!f) {
		return -EBADF;
	}
	FD* t = getFile(to);
	if(t && t->freeRef()) {
		delete t;
	}
	f->addRef();
	files_[to] = f;
	return to;
}

// typedef std::vector<char> data_block;
// typedef std::map<off_t, data_block> delta_map;
void DataFork::write(const void* buf, size_t sz, off_t off) {
	if(off + sz > size_) {
		size_ = off + sz;
	}
	delta_map::iterator i = differences_.lower_bound(off);
	if(i != differences_.begin())
		--i;
	for(; i != differences_.end() && i->first < off + sz; ) {
		if(i->first >= off + sz || i->first + i->second.size() < off) {
			++i;
			continue;
		}

		// if there is extra after, add it in
		if(i->first + i->second.size() > off + sz) {
			differences_.insert(std::make_pair(
				off + sz, 
				data_block(
					(const char*)&i->second[0] + off - i->first, 
					(const char*)&i->second[0] + i->second.size() - sz)));;
				//^^^^ double check me
		}

		if(i->first < off) {
			//if there is extra before
			i->second.resize(off - i->first);
		} else {
			//otherwise erase the node
			differences_.erase(i++);
		}
	}
	differences_.insert(std::make_pair(off, data_block((const char*)buf, (const char*)buf + sz)));
}
size_t DataFork::read(void* buf, size_t sz, off_t off) {
	klee_print_expr("read @ ", off);
	klee_print_expr(identifier_.c_str(), sz);
	if(off > size_)
		return 0;
	if(off + sz > size_) {
		sz = size_ - off;
	}
	// klee_print_expr("original:", originalSize_);
	// klee_print_expr("originalsz:", original_);
	// klee_check_memory_access(original_, originalSize_);
	// klee_print_expr("buf:", buf);
	// klee_print_expr("sz", sz);
	// klee_check_memory_access(buf, sz);
	if(originalSize_ < off + sz) {
		klee_warning("clearing data");
		memset(buf, 0, sz);
	}
	if(originalSize_ > off) {
		klee_warning("copying original data");
		const char* from = (char*)original_ + off;
		char* to = (char*)buf;
		size_t partial = std::min(originalSize_ - off, (long)sz);
		memcpy(to, from, partial);
	}
	klee_warning("applying deltas");
	delta_map::iterator i = differences_.lower_bound(off);
	if(i != differences_.begin())
		--i;
	for(; i != differences_.end() && i->first < off + sz; ) {
		if(i->first >= off + sz || i->first + i->second.size() < off) {
			++i;
			continue;
		}
		off_t to = std::max(off, i->first);
		off_t sz = std::min(off + sz, i->first + (long)i->second.size()) - to;
		memcpy((char*)buf + to - off, (char*)&i->second[0] + to - i->first, sz);
	}
	return sz;
}
void DataFork::truncate(off_t len) {
	size_ = len;
	delta_map::iterator i = differences_.lower_bound(len);
	if(i != differences_.begin()) {
		--i;
		if(i->first + i->second.size() > len) {
			i->second.resize(len - i->first);
		}
		++i;
	}
	differences_.erase(i, differences_.end());
}
void DataFork::stat(struct stat* buf) {
	kmc_make_range_symbolic((uintptr_t)buf, sizeof(*buf), "Data::stat buf ");
	klee_assume(buf->st_nlink > 0);
	buf->st_mode |= S_IFREG;
	buf->st_size = size_;
	buf->st_blocks = (size_ + 511) >> 9;
	buf->st_blksize = 512;
}

DataFork::DataFork(const std::string& id, const void* data, off_t size)
: identifier_(id) 
, original_(data)
, originalSize_(size)
, size_(size)
{
}
off_t DataFork::length() const {
	return size_;
}

SymbolicFork::SymbolicFork(const std::string& id) 
: DataFork(identifier_, NULL, 0)
{
	kmc_make_range_symbolic((uintptr_t)&size_, sizeof(size_), "SF::size");
}
void SymbolicFork::write(const void* buf, size_t sz, off_t off) {
	//yup you did
	if(sz + off > size_)
		size_ = sz + off;
}
size_t SymbolicFork::read(void* buf, size_t sz, off_t off) {
	if(off > size_) {
		return 0;
	}
	if(off + sz > size_) {
		sz = size_ - off;
	}
	kmc_make_range_symbolic((uintptr_t)buf, sz, "SF::read data");
	return sz;
}

VFS::VFS() 
{
}
VFS::~VFS() {
	
}
long VFS::access(const char* path, int mode) {
	long result;
	kmc_make_range_symbolic((uintptr_t)&result, sizeof(result), "VFS::access res");
	if(result >= 0) {
		result = 0;
	} else if(result < MIN_ERRNO) {
		result = MIN_ERRNO;
	}
	return result;
}
long VFS::open(const char* path, int flags, int access) {
	long result;
	kmc_make_range_symbolic((uintptr_t)&result, sizeof(result), "VFS::open res");
	if(result >= 0) {
		result = fdt.newFile(new SymbolicFile(path));
	} else if(result < MIN_ERRNO) {
		result = MIN_ERRNO;
	}
	return result;
}
long VFS::unlink(const char* path) {
	long result;
	kmc_make_range_symbolic((uintptr_t)&result, sizeof(result), "VFS::unlink res");
	if(result >= 0) {
		result = 0;
	} else if(result < MIN_ERRNO) {
		result = MIN_ERRNO;
	}
	return result;
}
long VFS::truncate(const char* path, off_t len) {
	long result;
	kmc_make_range_symbolic((uintptr_t)&result, sizeof(result), "VFS::truncate res");
	if(result >= 0) {
		result = 0;
	} else if(result < MIN_ERRNO) {
		result = MIN_ERRNO;
	}
	return result;
}
long VFS::stat(const char* path, struct stat *buf) {
	long result;
	kmc_make_range_symbolic((uintptr_t)&result, sizeof(result), "VFS::stat res");
	if(result >= 0) {
		result = 0;
	} else if(result < MIN_ERRNO) {
		result = MIN_ERRNO;
	}
	return result;
}
long VFS::rename(const char* from, const char* to) {
	long result;
	kmc_make_range_symbolic((uintptr_t)&result, sizeof(result), "VFS::rename res");
	if(result >= 0) {
		result = 0;
	} else if(result < MIN_ERRNO) {
		result = MIN_ERRNO;
	}
	return result;
}
long VFS::chdir(const char* path) {
	long result;
	kmc_make_range_symbolic((uintptr_t)&result, sizeof(result), "VFS::chdir res");
	if(result >= 0) {
		result = 0;
	} else if(result < MIN_ERRNO) {
		result = MIN_ERRNO;
	}
	return result;
}

//typedef std::map<std::string, DataFork*> fork_map;
//fork_map forks_;
ConcreteVFS::ConcreteVFS() {
	char buf[PATH_MAX] = {0};
	memset(buf, 0, PATH_MAX);
	sc_get_cwd(&buf[0]);
	cwd_ = "/home/tj/Desktop/kleemc";	
}
ConcreteVFS::~ConcreteVFS() {
	
}
long ConcreteVFS::access(const char* p, int mode) {
	std::string path;
	if(!shouldBeConcrete(p, path)) {
		return VFS::access(p, mode);
	}
	DataFork* fork;
	long result = getFork(path, fork);
	if(!fork)
		return result;
	return syscall(SYS_access, path.c_str(), mode, 0, 0, 0, 0);
}
long ConcreteVFS::open(const char* p, int flags, int access) {
	klee_warning("opening");
	klee_warning(p);
	std::string path;
	if(!shouldBeConcrete(p, path)) {
		klee_warning("!concrete");
		return VFS::open(p, flags, access);
	}
	DataFork* fork;
	long result = getFork(path, fork);
	if(!fork) {
		if(!(flags & O_CREAT) || result != -ENOENT)
			return result;
		//make a new virtual file
		forks_[path] = fork = new DataFork(path, NULL, 0);
	}
	return fdt.newFile(new ConcreteFile(fork));
}
long ConcreteVFS::unlink(const char* p) {
	std::string path;
	if(!shouldBeConcrete(p, path)) {
		return VFS::unlink(p);
	}
	fork_map::iterator i = forks_.find(path);
	if(i != forks_.end()) {
		i->second = NULL;
		return 0;
	} else {
		return -ENOENT;
	}
}
long ConcreteVFS::truncate(const char* p, off_t len) {
	if(len < 0)
		return -EINVAL;

	std::string path;
	if(!shouldBeConcrete(p, path)) {
		return VFS::truncate(p, len);
	}
	DataFork* fork;
	long result = getFork(path, fork);
	if(!fork)
		return result;
	fork->truncate(len);
	return 0;
}
long ConcreteVFS::stat(const char* p, struct stat *buf) {
	std::string path;
	if(!shouldBeConcrete(p, path)) {
		return VFS::stat(p, buf);
	}
	DataFork* fork;
	long result = getFork(path, fork);
	if(!fork)
		return result;
	fork->stat(buf);
	return 0;
}
//TODO: should probably do something to "update" the path name that is embedded?
long ConcreteVFS::rename(const char* f, const char* t) {
	std::string from, to;
	if(!shouldBeConcrete(f, from) && !shouldBeConcrete(t, to)) {
		return VFS::rename(f, t);
	}
	DataFork* fork_from;
	long result = getFork(f, fork_from);
	if(!fork_from)
		return result;

	if(!shouldBeConcrete(f, from) && shouldBeConcrete(t, to)) {
		forks_[to] = new SymbolicFork("ConcreteVFS::rename to concrete " + to);
		forks_[from] = NULL;
		return result;
	}
	if(shouldBeConcrete(f, from) && !shouldBeConcrete(t, to)) {
		//don't create an entry for the dest file if it should be symbolic
		forks_[from] = NULL;
		return result;
	}	
	forks_[to] = fork_from;
	forks_[from] = NULL;
	return 0;
}
long ConcreteVFS::chdir(const char* p) {
	std::string path;
	//TODO: try to make sure the dir exists? or some kind of concrete behavior?
	shouldBeConcrete(p, path);
	long result = VFS::chdir(p);
	if(result != 0)
		return result;
	cwd_ = path;
	return 0;
}

bool ConcreteVFS::shouldBeConcrete(const char* p, std::string& resolved) {
	resolved.clear();
	if(!p)
		return false;
	if(*p == 0)
		return false;
	if(*p == '/') {
		resolved = p;
		return true;
	} else {
		resolved = cwd_ + p;
		//log the symbolic files for fun
		klee_warning(p);
		return false;
	}
	//TODO: lots of path stuff, yick!
}
long ConcreteVFS::getFork(const std::string& path, DataFork*& fork) {
	fork = NULL;
	fork_map::iterator i = forks_.find(path);
	if(i != forks_.end()) {
		if(!i->second)
			return -ENOENT;
		fork = i->second;
		return 0;
	}
	klee_warning(path.c_str());
	long result = sc_concrete_file_snapshot(path.c_str(), path.size());
	if(result < 0) {
		return result;
	}
	long sz = sc_concrete_file_size(path.c_str(), path.size());
	if(result < 0) {
		return sz;
	}
	{
		std::map<std::string, int> a;
		a.insert(std::make_pair(path,1));	
	}
	fork = new DataFork(path, (void*)result, sz);
	forks_[path] = fork;
	return 0;
}

struct ctor {
	ctor() {
		klee_warning("XXXXXXXXXXXX global ctors");
	}
} _ctor;

FDT fdt;
// VFS vfs;
ConcreteVFS vfs;