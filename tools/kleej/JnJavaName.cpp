#include "JnJavaName.h"

JnJavaName* JnJavaName::create(const std::string& s)
{
	std::string	path, method, info;
	size_t		info_idx, method_idx;

	if (s.substr(0, 5) != "JnJVM")
		return NULL;

	
	info_idx = s.find("__");
	if (info_idx == std::string::npos)
		return NULL;

	path = s.substr(6, info_idx - 6);
	method_idx = path.find_last_of('_');
	if (method_idx == std::string::npos)
		return NULL;

	method = path.substr(method_idx+1);
	path = path.substr(0, method_idx);

	for (unsigned i = 0; i < path.size(); i++)
		if (path[i] == '_')
			path[i] = '.';

	info = s.substr(info_idx + 2, std::string::npos);
	return new JnJavaName(s, path, method, info);
}

void JnJavaName::print(std::ostream& os) const
{ os << path << ":" << method << " (" << info << ')'; }
