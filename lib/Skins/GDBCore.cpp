#include <signal.h>
#include <sstream>
#include "static/Sugar.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <llvm/Support/CommandLine.h>

#include <iostream>

#include "../Core/Forks.h"
#include "../Core/Executor.h"
#include "../Searcher/OverrideSearcher.h"
#include "klee/ExecutionState.h"
#include "GDBCore.h"

using namespace klee;

namespace {
	llvm::cl::opt<bool>
	UseBogusGDBMem(
		"use-gdb-bogusmem",
		llvm::cl::desc("Replace every symbolic byte with 0xfe."),
		llvm::cl::init(true));
}


static GDBCore	*ctrlc_gc;
static void handle_ctrlc(int x)
{
	ctrlc_gc->setStopped();
	ctrlc_gc->setPendingBreak();
}

class GDBTimer : public Executor::Timer
{
public:
	GDBTimer(GDBCore* _gc) : gc(_gc) {}
	virtual ~GDBTimer() {}

	void run(void)
	{
		GDBCmd	*gcmd;
		gcmd = gc->getNextCmd();
		if (!gcmd) return;
		delete gcmd;
	}
private:
	GDBCore	*gc;
};

GDBCore::GDBCore(Executor* _exe, unsigned _port)
: port(_port)
, exe(_exe)
, run_state(Stopped)
, pending_break(false)
, sel_thread(0)
, watch_fork_br(false)
{
	fd_listen = opensock();
	fd_client = acceptsock();
	addHandler(new GDBQueryPkt(this, exe));
	addHandler(new GDBPkt(this, exe));

	ctrlc_gc = this;
	signal(SIGINT, handle_ctrlc);
	exe->addTimer(new GDBTimer(this), 1.0);
}

GDBCore::~GDBCore(void)
{
	foreach (it, handlers.begin(), handlers.end())
		delete (*it);
}

GDBCmd* GDBCore::waitNextCmd(void)
{
	GDBCmd	*ret;

	/* waiting; block on packets */
	while ((ret = getNextCmd()) == NULL)
		fcntl(fd_client, F_SETFL, 0);

	/* got the command, don't block on packets */
	fcntl(fd_client, F_SETFL, O_NONBLOCK);

	return ret;
}

GDBCmd* GDBCore::getNextCmd(void)
{
	GDBCmd		*gcmd;
	char		linebuf[1024];
	ssize_t		sz;
	unsigned	i;
	uint8_t		expected_chksum, actual_chksum;

	if (pending_break) {
		writePkt("S02");
		pending_break = false;
	}

	sz = recv(fd_client, &linebuf, 1023, 0);
	if (sz == -1 && errno == EAGAIN)
		/* nothing to receive */
		return NULL;

	linebuf[sz] = '\0';

	if (sz <= 0) {
		/* client is dead, wait on new connection */
		fd_client = acceptsock();
		return NULL;
	}

	for (i = 0; i < sz; i++) {
		if (linebuf[i] != '$' && !incoming_buf.size())
			continue;
		incoming_buf.push_back(linebuf[i]);
	}

	/* this makes it O(n^2)! YAY */
	/* look for end-of-packet market */
	for (i = 0; i < incoming_buf.size(); i++) {
		if (incoming_buf[i] == '#')
			break;
	}

	/* '#' not found or no chksum? no possible waiting packet */
	if ((i+2) >= incoming_buf.size())
		return NULL;

	expected_chksum = (incoming_buf[i+1] > '9')
		? ((incoming_buf[i+1] - 'a')+10)
		: incoming_buf[i+1] - '0';
	expected_chksum <<= 4;

	expected_chksum |= (incoming_buf[i+2] > '9')
		? ((incoming_buf[i+2] - 'a')+10)
		: incoming_buf[i+2] - '0';


	actual_chksum = 0;
	for (i = 1; incoming_buf[i] != '#'; i++) {
		actual_chksum += (uint8_t)incoming_buf[i];
	}

	gcmd = NULL;
	if (expected_chksum == actual_chksum) {
		incoming_buf[i] = '\0';
		gcmd = new GDBCmd(&incoming_buf[1] /* 1 => skip $ */);
		std::cerr << "Processing command: \"" << gcmd->getStr() << "\"\n";
		handleCmd(gcmd);
	} else {
		std::cerr << "[GDBCore] !!!BAD CHKSUM!!!\n";
	}

	incoming_buf = std::vector<char>(
		incoming_buf.begin() + i + 3,
		incoming_buf.end());
	return gcmd;
}

void GDBCore::handleCmd(GDBCmd* gcmd)
{
	foreach (it, handlers.begin(), handlers.end())
		if ((*it)->handle(gcmd))
			return;
}

int GDBCore::opensock(void)
{
	struct sockaddr_in	sockaddr;
	int			v, ret, fd;


	fd = socket(PF_INET, SOCK_STREAM, 0);
	assert (fd > 0);

	v= 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&v, sizeof(v));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(port);
	sockaddr.sin_addr.s_addr = 0;

	ret = bind(fd, (struct sockaddr*)&sockaddr, sizeof(sockaddr));
	assert (ret >= 0);

	ret = listen(fd, 0);
	assert (ret >= 0);

	return fd;
}

int GDBCore::acceptsock(void)
{
	struct sockaddr_in	sa;
	socklen_t		len;
	int			v, fd;
	char			in_host[128];

	incoming_buf.clear();
	len = sizeof(sockaddr);
	std::cerr << "[GDBCore] Accepting client connection..\n";

	fd = accept(fd_listen, (struct sockaddr*)&sa, &len);
	assert (fd);
	fcntl(fd, F_SETFD, FD_CLOEXEC);

	getnameinfo((struct sockaddr*)&sa, len, in_host, 128, NULL, 0, 0);
	std::cerr
		<< "[GDBCore] Got connection from \""
		<< in_host
		<< "\"\n";

	v = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&v, sizeof(v));
	fcntl(fd, F_SETFL, O_NONBLOCK);

	return fd;
}

GDBCmd::GDBCmd(const std::string& _s)
: s(_s)
{
	tokenize();
}

void GDBCmd::tokenize(void)
{
	unsigned cur_off, cur_base = 0;
	for (cur_off = 0; cur_off < s.size(); cur_off++) {
		if (s[cur_base] == ' ' && s[cur_off] != ' ')
			cur_base = cur_off;

		if (s[cur_off] == ' ') {
			tokens.push_back(s.substr(cur_base, cur_off));
			std::cerr << "EH GIRL \"" << tokens.back() << "\"\n";
			cur_base = cur_off;
		}
	}

	if (cur_off != cur_base && s[cur_base] != ' ')
		tokens.push_back(s.substr(cur_base, cur_off));
}

bool GDBQueryPkt::handle(GDBCmd* gcmd)
{
	size_t		colon_idx;
	std::string	first_tok;

	if (gcmd->getStr()[0] != 'q')
		return false;

	std::cerr << "[GDBQueryPkt] " << gcmd->getStr() << '\n';

	colon_idx = gcmd->getStr().find(':');
	if (colon_idx != std::string::npos) {
		first_tok = gcmd->getStr().substr(1, colon_idx-1);
	} else
		first_tok = gcmd->getStr().substr(1);

	gc->ack();

	if (colon_idx && first_tok == "Supported") {
		gc->writePkt("");
		return true;
	}

	if (first_tok == "Offsets") {
		std::cerr << "[GDBCore] TODO: Use actual text offset\n";
		gc->writePkt("TextSeg=400000");
		return true;
	}

	if (first_tok == "Attached") {
		gc->writePkt("1");
		return true;
	}

	if (first_tok == "C") {
		char	tmp[32];
		sprintf(tmp, "%lx", (uint64_t)exe->getCurrentState());
		gc->writePkt(tmp);
		return true;
	}

	if (first_tok == "fThreadInfo") {
		std::stringstream	ss;
		std::string		outstr;
		char			tmp[32];

		/* fake only one thread: 12345 */
		if (exe->beginStates() == exe->endStates()) {
			sprintf(tmp, "%lx", (uint64_t)exe->getCurrentState());
			ss << "m" << tmp;
			gc->writePkt(ss.str().c_str());
			return true;
		}

		ss << "m";
		foreach (it, exe->beginStates(), exe->endStates()) {
			sprintf(tmp, "%lx", (uint64_t)(*it));
			ss << tmp << ",";
		}

		outstr = ss.str();
		outstr = outstr.substr(0, outstr.size() - 1);
		gc->writePkt(outstr.c_str());

		return true;
	}

	if (strncmp(
		"ThreadExtraInfo",
		first_tok.c_str(),
		sizeof("ThreadExtraInfo")-1) == 0)
	{
		void*	p;
		sscanf(first_tok.c_str()+sizeof("ThreadExtraInfo"), "%p", &p);
		/* hey. */
		gc->writePkt("6865792e");
		return true;
	}

	if (first_tok == "sThreadInfo") {
		/* end of the thread list */
		gc->writePkt("l");
		return true;
	}

	/* tracepoint data: NO SYNTAX AVAILABLE THANKS GDB DEVS */
	if (first_tok == "TfV") {
		/* end of the thread list */
		gc->writePkt("l");
		return true;
	}

	if (first_tok == "TfP") {
		/* end of the thread list */
		gc->writePkt("l");
		return true;
	}


	if (first_tok == "TStatus") {
		/* we don't support traces yet */
		gc->writePkt("");
		return true;
	}

	std::cerr << "[GDBQueryPkt] WTF: " << gcmd->getStr() << "\n";
	gc->writePkt("");

	return true;
}

void GDBCore::ack(bool ok_ack)
{
	char	*ack_dat = (char*)((ok_ack) ? "+" : "-");
	while (write(fd_client, ack_dat, 1) != 1);
}

static const char hex2char[] = "0123456789abcdef";

void GDBCore::writePkt(const char* dat)
{
	unsigned		bw_total, to_write;
	std::stringstream	ss;
	std::string		dat_str;
	uint8_t			chksum;

	std::cerr << "WRITING: \"" << dat << "\"\n";
	to_write = strlen(dat);
	bw_total = 0;

	chksum = 0;
	for (unsigned i = 0; i < to_write; i++)
		chksum += dat[i];

	ss	<< "$"
		<< dat
		<< '#'<< hex2char[chksum >> 4] << hex2char[chksum & 0xf];
	dat_str = ss.str();
	to_write = dat_str.size();

	while (bw_total < to_write) {
		ssize_t	bw;

		bw = write(
			fd_client,
			dat_str.c_str() + bw_total,
			to_write - bw_total);
		if (bw < 0) {
			std::cerr
				<< "[GDBCore] Error writing packet \""
				<< dat_str << "\"\n";
			return;
		}
		bw_total += bw;
	}
}

void GDBCore::writeStateRead(
	const std::vector<uint8_t>& v,
	const std::vector<bool>& is_conc)
{
	std::stringstream	ss;
	std::string		out_str;

	assert (v.size() != 0);
	for (unsigned i = 0; i < v.size(); i++) {
		if (is_conc[i] == false) {
			ss << "xx";
			continue;
		}

		ss << hex2char[v[i] >> 4] << hex2char[v[i] & 0xf];
	}

	out_str = ss.str();
	writePkt(out_str.data());
}

bool GDBPkt::handle(GDBCmd* gcmd)
{
	switch (gcmd->getStr()[0]) {
	case 'D':
		std::cerr << "[GDBPkt] Disconnect\n";
		gc->ack();
		gc->writePkt("OK");
		return true;
	case 'H': {
		void	*tid = NULL;

		if (gcmd->getStr()[2] != '-') {
			sscanf(&(gcmd->getStr().c_str()[2]), "%p", &tid);
		}

		OverrideSearcher::setOverride((ExecutionState*)tid);
		if (tid == NULL)
			tid = exe->getCurrentState();

		gc->ack();
		if (exe->hasState((ExecutionState*)tid) == false) {
			gc->overrideThread(NULL);
			gc->writePkt("E01");
			return true;
		}

		gc->setSelThread((ExecutionState*)tid);
		gc->writePkt("OK");
		return true;
	}
	case '?': /* stop reason */
		gc->ack();
		gc->writePkt("S05");	/* sigtrap */
		return true;

	case 'p': {
		std::vector<uint8_t>	v;
		std::vector<bool>	is_conc;
		int			n;

		sscanf(&(gcmd->getStr().c_str()[1]), "%x", &n);

		exe->getCurrentState()->getGDBRegs(v, is_conc);
		gc->ack();
		v = std::vector<uint8_t>(
			v.begin() + n,
			v.begin() + n + 1);
		is_conc = std::vector<bool>(
			is_conc.begin() + n,
			is_conc.begin() + n + 1);


		gc->writeStateRead(v, is_conc);
		return true;
	}

	case 'g': {/* request registers */
		std::vector<uint8_t>	v;
		std::vector<bool>	is_conc;

		gc->getSelThread()->getGDBRegs(v, is_conc);
		gc->ack();
		gc->writeStateRead(v, is_conc);

		return true;
	}

	case 'X': {
		// X addr,length:XX...'
		std::cerr << "[GDBPkt] Faking write (X) command\n";
		gc->ack();
		gc->writePkt("OK");
		return true;
	}

	case 'c':
	case 'C': {
		std::cerr << "[GDBPkt] 'C'ont\n";
		gc->setRunning();
		gc->ack();
		return true;
	}

	case 's': {
		std::cerr << "[GDBPkt] 's'inglestep\n";
		OverrideSearcher::setOverride(gc->getSelThread());
		gc->setSingleStep();
		gc->ack();
		return true;
	}

	case 'S': {
		unsigned sig;
		std::cerr << "[GDBPkt] Signaling process.\n";
		sscanf(&(gcmd->getStr().c_str()[1]), "%x", &sig);
		if (sig == 1)
			gc->setSingleStep();
		else if (sig == 17 || sig == SIGCONT)
			gc->setRunning();
		else
			std::cerr << "[GDBPkt] UNKNOWN S-SIGNAL!\n";
		gc->ack();
		return true;
	}

	case 'Z': {
		std::cerr << "[GDBPkt] IMPLEMENT BREAKPOINT INSERTION!!\n";
		gc->ack();
		gc->writePkt(""); /* not supported */
		return true;
	}

	case 'v' : {
		const char	*fields;
		fields = &gcmd->getStr().c_str()[5];

		if (strncmp(&(gcmd->getStr().c_str())[1], "Cont", 4) != 0)
			break;

		vcont(fields);
		return true;
	}

	case 'm': {/* mADDR,LEN */
		std::vector<uint8_t>	v;
		std::vector<bool>	is_conc;
		uint64_t	addr;
		unsigned	len;
		int		elems;
		bool		bogus_reads;
		AddressSpace	*as;

		elems = sscanf(
			gcmd->getStr().c_str()+1,
			"%p,%x",
			(void**)(&addr), &len);
		assert (elems == 2);

		std::cerr << "[GDBPKT] Doing memory access\n";
		as = &exe->getCurrentState()->addressSpace;
		bogus_reads = as->readConcrete(v, is_conc, addr, len);

		gc->ack();

		if (bogus_reads) {
			if (!UseBogusGDBMem) {
				unsigned ok = 0;
				while (is_conc[ok]) ok++;
				v.resize(ok);
				is_conc.resize(ok);
			} else {
				for (unsigned i = 0; i < v.size(); i++) {
					if (is_conc[i] == false) {
						v[i] = 0xfe;
						is_conc[i] = true;
					}
				}
			}
		}

		if (v.size() == 0) {
			gc->writePkt("");
			break;
		}


		gc->writeStateRead(v, is_conc);
		break;
	}

	/* T thread-id
	 * test if thread is active */
	case 'T': {
		void	*tid;

		gc->ack();

		sscanf(&(gcmd->getStr().c_str())[1], "%p", &tid);

		if (exe->hasState((ExecutionState*)tid)) {
			gc->writePkt("OK");
			return true;
		}

		gc->writePkt("E01");
		return true;
	}

	default:
		return false;
	}

	gc->ack();
	return true;
}

ExecutionState* GDBCore::getSelThread(void)
{
	if (!exe->hasState(sel_thread))
		sel_thread = exe->getCurrentState();

	return sel_thread;
}

void GDBPkt::vcont(const char* fields)
{
	gc->ack();
	if (fields[0] == '?') {
		/* gdb wants *everything */
		gc->writePkt("vCont;s;c;S;C;t");
		return;
	}

	// signal format:
	// C70:-1;c
	if (fields[1] == 's' || fields[1] == 'S') {
		if (checkSignal(&fields[2]))
			return;
		gc->setSingleStep();
		return;
	}

	if (fields[1] == 'C' || fields[1] == 'c') {
		if (checkSignal(&fields[2]))
			return;
		gc->setRunning();
		return;
	}

}

void GDBCore::overrideThread(ExecutionState* es)
{
	sel_thread = es;
	OverrideSearcher::setOverride(sel_thread);
}

#define SIGCMD_TOGGLE_DEBUGINST	108	/* SIG94 */
#define SIGCMD_CONCRETIZE	109	/* SIG95 */
#define SIGCMD_TAKETRUE		110	/* SIG96 */
#define SIGCMD_TAKEFALSE	111	/* SIG97 */
#define SIGCMD_STEPBRANCH	112	/* SIG98 */


bool GDBPkt::checkSignal(const char* sigstr)
{
	Executor::StatePair	sp;
	int			elems;
	unsigned		sig;

	elems = sscanf(sigstr, "%x", &sig);
	if (elems == 0)
		return false;

	std::cerr << "[GDBPkt] CHECK SIG: " << sig << '\n';
	sp = exe->getForking()->getLastFork();

	switch (sig) {
	case SIGCMD_TOGGLE_DEBUGINST:
		DebugPrintInstructions = !DebugPrintInstructions;
		break;
	case SIGCMD_CONCRETIZE:
		exe->concretizeState(*gc->getSelThread());
		gc->overrideThread(gc->getSelThread());
		break;

	case SIGCMD_TAKETRUE:
		if (!gc->watchForkBranch() || sp.first == NULL)
			break;

		gc->overrideThread(sp.first);
		gc->setWatchForkBranch(false);
		gc->setSingleStep();
		return true;

	case SIGCMD_TAKEFALSE:
		if (!gc->watchForkBranch() || sp.second == NULL)
			break;

		gc->overrideThread(sp.second);
		gc->setWatchForkBranch(false);
		gc->setSingleStep();
		return true;

	case SIGCMD_STEPBRANCH:
		gc->overrideThread(gc->getSelThread());
		gc->setRunning();
		gc->setWatchForkBranch(true);
		return true;
	}

	return false;
}

void GDBCore::handleForkBranch(void)
{
	Executor::StatePair	sp;

	sp = exe->getForking()->getLastFork();

	/* not an actual fork?! */
	if (!sp.first || !sp.second)
		return;

	setSelThread(exe->getCurrentState());
	setStopped();
	writePkt("S70");	// STEPBRANCH
}
