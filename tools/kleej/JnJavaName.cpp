#include <stdlib.h>

#include <sstream>
#include "JnJavaName.h"

JnJavaName* JnJavaName::create(const std::string& s)
{
	std::string	path, method, info;
	size_t		path_suffix_idx;
	size_t		info_idx, method_idx;

	if (s.substr(0, 5) != "JnJVM")
		return NULL;

	
	info_idx = s.find("__");
	if (info_idx == std::string::npos)
		return NULL;

	info = s.substr(info_idx + 2, std::string::npos);
	path = s.substr(6, info_idx - 6);
	if (info.find("Cinit_") == std::string::npos) {
		method_idx = path.find_last_of('_');
		if (method_idx == std::string::npos)
			return NULL;

		method = path.substr(method_idx+1);
		path = path.substr(0, method_idx);
	} else {
		method = "@Constr";
	}

	for (unsigned i = 0; i < path.size(); i++)
		if (path[i] == '_')
			path[i] = '.';

	path_suffix_idx = path.find_last_of(".");
	if (path_suffix_idx != std::string::npos) {
		int			n;
		std::stringstream	ss(path.substr(path_suffix_idx+1));

		if (ss >> n) {
			/* XXX what does the number mean */
			path = path.substr(0, path_suffix_idx);
			path += "$";
		}
	}

	return new JnJavaName(s, path, method, info);
}

void JnJavaName::print(std::ostream& os) const
{ os << path << ":" << method << " (" << info << ')'; }

JnJavaName::JnJavaName(
	const std::string& _l,
	const std::string& _p,
	const std::string& _m,
	const std::string& _i)
: llvm_name(_l), path(_p), method(_m), info(_i)
{
	dirpath = path;
	for (unsigned i = 0; i < dirpath.size(); i++) {
		if (dirpath[i] == '.')
			dirpath[i] = '/';
	}

	bcpath = dirpath + ".class.bc";
	class_name = dirpath.substr(dirpath.find_last_of("/")+1);
	dirpath = dirpath.substr(0, dirpath.find_last_of("/"));

	anonymous =	(path.find("$") != std::string::npos) ||
			(class_name.find("$") != std::string::npos);
}


JnJavaName* JnJavaName::createGlobal(const std::string& s)
{
	std::string	path(s), method, info, suff;
	size_t		idx;

	method = "";
	info = "";

	idx = s.find_last_of("_");
	if (idx == std::string::npos)
		return NULL;

	suff = s.substr(idx + 1);
	if (suff.empty())
		return NULL;

	if (suff == "VT" || suff == "VirtualMethods") {
		path = path.substr(0, path.find_last_of("_"));
		suff = path.substr(path.find_last_of("_") + 1);
	}

	if (suff[0] == '0') {
		path = path.substr(0, path.find_last_of("_"));
		path += "$";
	}

	for (unsigned i = 0; i < path.size(); i++)
		if (path[i] == '_')
			path[i] = '.';


	return new JnJavaName(s, path, method, info);
}
