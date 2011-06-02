#include <vector>
#include <fstream>
#include <string.h>

#include <klee/Common.h>
#include "static/Sugar.h"
#include "cmdargs.h"

using namespace std;

extern string stripEdgeSpaces(string &in);

CmdArgs::CmdArgs(
	const std::string& in_binary_path,
	const string& in_env_path, 
	char** in_env,
	const list<string>& in_argv)
: 	in_bin_path(in_binary_path),
	envp_native(in_env)
{
	envp = envFromString(in_env_path);
	loadArgv(in_argv);
}

CmdArgs::~CmdArgs(void)
{
	for (unsigned i = 0; i < argc; i++)
		delete [] argv[i];
	delete[] argv;

	if (envp != NULL) {
		for (unsigned int i = 0; envp[i]; i++)
			delete [] envp[i];
		delete [] envp;
	}
}

char** CmdArgs::envFromString(const string& in_env_path)
{
	char **pEnvp;

	if (in_env_path == "")
		return NULL;

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

	return pEnvp;
}

void CmdArgs::loadArgv(const list<string>& in_argv)
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
