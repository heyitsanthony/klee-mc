#ifndef KLEEMC_CMDARGS_H
#define KLEEMC_CMDARGS_H

#include <iostream>
#include <string>
#include <list>

/* this class handles environment and command line arguments */
class CmdArgs
{
public:
	CmdArgs(const std::string& in_binary,
		const std::string& env_path,
		char** in_env,
		const std::list<std::string>& argv);
	virtual ~CmdArgs();
	unsigned int getArgc(void) const { return argc; }
	char** getArgv(void) const { return argv; }
	char** getEnvp(void) const { return (envp) ? envp : envp_native; }
	const std::string& getBinaryPath(void) const { return in_bin_path; }
	void print(std::ostream& os) const;
private:
	char** envFromString(const std::string& e_path);
	void loadArgv(const std::list<std::string>& argv);
	std::string	in_bin_path;
	unsigned int	argc;
	char		**argv;
	char		**envp;
	char		**envp_native;
};

#endif
