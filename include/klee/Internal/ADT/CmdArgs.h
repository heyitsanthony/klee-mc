#ifndef KLEEMC_CMDARGS_H
#define KLEEMC_CMDARGS_H

#include <iostream>
#include <string>
#include <list>
#include <vector>

/* this class handles environment and command line arguments */
namespace klee
{
class CmdArgs
{
public:
	CmdArgs(const std::string& in_binary,
		const std::string& env_path,
		char** in_env,
		const std::vector<std::string>& argv);
	virtual ~CmdArgs();

	unsigned int getArgc(void) const { return argc; }
	unsigned int getEnvc(void) const { return env_c; }
	char** getArgv(void) const { return argv; }
	char** getEnvp(void) const { return (envp) ? envp : envp_native; }
	const std::string& getBinaryPath(void) const { return in_bin_path; }
	void print(std::ostream& os) const;

	bool isSymbolic(void) const { return symbolic; }
	void setSymbolic(void) { symbolic = true; }
	void setArgs(const std::vector<std::string>& ptrs);

	static std::string stripEdgeSpaces(std::string &in);
	static void readArgumentsFromFile(
		char *file, std::vector<std::string> &results);

private:
	char** envFromString(const std::string& e_path);
	void loadArgv(const std::vector<std::string>& argv);
	void clearArgv(void);
	std::string	in_bin_path;
	unsigned int	argc;
	char		**argv;
	char		**envp;
	char		**envp_native;
	unsigned int	env_c;
	bool		symbolic;
};
}
#endif
