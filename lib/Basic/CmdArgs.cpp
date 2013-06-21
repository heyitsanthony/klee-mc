#include <vector>
#include <fstream>
#include <string.h>
#include <assert.h>

#include <klee/Common.h>
#include "static/Sugar.h"

#include "klee/Internal/ADT/CmdArgs.h"

using namespace std;
using namespace klee;

extern string stripEdgeSpaces(string &in);

CmdArgs::CmdArgs(
	const std::string& in_binary_path,
	const string& in_env_path, 
	char** in_env,
	const vector<string>& in_argv)
: in_bin_path(in_binary_path)
, envp_native(in_env)
, symbolic(false)
{
	envp = envFromString(in_env_path);
	loadArgv(in_argv);
}

CmdArgs::CmdArgs()
: in_bin_path("")
, envp_native(NULL)
, symbolic(false)
{
	vector<string> in_argv;
	envp = envFromString("");
	loadArgv(in_argv);
}

CmdArgs::~CmdArgs(void)
{
	clearArgv();

	if (envp != NULL) {
		for (unsigned int i = 0; envp[i]; i++)
			delete [] envp[i];
		delete [] envp;
	}
}

char** CmdArgs::envFromString(const string& in_env_path)
{
	char **pEnvp;

	if (in_env_path == "") {
		env_c = 0;
		if (envp_native == NULL) return NULL;
		while (envp_native[env_c]) env_c++;
		return NULL;
	}

	vector<string> items;
	ifstream f(in_env_path.c_str());

	if (!f.good()) {
		klee::klee_error(
			"unable to open --environ file: %s", 
			in_env_path.c_str());
	}

	while (!f.eof()) {
		string line;

		getline(f, line);
		line = stripEdgeSpaces(line);
		if (!line.empty())
			items.push_back(line);
	}
	f.close();

	pEnvp = new char *[items.size()+1];
	unsigned i=0;
	for (; i != items.size(); ++i)
		pEnvp[i] = strdup(items[i].c_str());
	pEnvp[i] = NULL;
	env_c = i-1;

	return pEnvp;
}

void CmdArgs::print(std::ostream& os) const
{
	os << "cmdargs: " << std::endl;
	for (unsigned int i = 0; i < argc; i++) {
		os << "arg[" << i << "]: " << argv[i] << std::endl;
	}
}

void CmdArgs::clearArgv(void)
{
	for (unsigned i = 0; i < argc; i++)
		delete [] argv[i];
	delete[] argv;
}

void CmdArgs::setArgs(const vector<string>& ptrs)
{
	clearArgv();
	loadArgv(ptrs);
}

void CmdArgs::loadArgv(const vector<string>& in_argv)
{
	unsigned int	i;

	argc = in_argv.size();
	argv = new char*[argc+1];

	i = 0;
	foreach (it, in_argv.begin(), in_argv.end()) {
		string		in_arg;
		unsigned int	size;
		char		*cur_arg;

		in_arg = *it;
		size = in_arg.size() + 1;
		cur_arg = new char[size];

		copy(in_arg.begin(), in_arg.end(), cur_arg);
		cur_arg[size - 1] = 0;

		argv[i] = cur_arg;
		i++;
	}

	argv[i] = NULL;
}

std::string CmdArgs::stripEdgeSpaces(std::string &in)
{
	unsigned len = in.size();
	unsigned lead = 0, trail = len;
	while (lead<len && isspace(in[lead])) ++lead;
	while (trail>lead && isspace(in[trail-1])) --trail;
	return in.substr(lead, trail-lead);
}

void CmdArgs::readArgumentsFromFile(
	char *file, std::vector<std::string> &results)
{
	std::ifstream f(file);
	assert(f.is_open() && "unable to open input for reading arguments");
	while (!f.eof()) {
		std::string line;
		std::getline(f, line);
		line = CmdArgs::stripEdgeSpaces(line);
		if (!line.empty())
			results.push_back(line);
	}
	f.close();
}


