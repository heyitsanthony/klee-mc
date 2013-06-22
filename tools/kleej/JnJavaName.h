#ifndef JNJAVANAME_H
#define JNJAVANAME_H

#include <string>
#include <iostream>

class JnJavaName
{
public:
	static JnJavaName* create(const std::string& s);
	virtual ~JnJavaName() {}

	const std::string& getPath(void) const { return path; }
	const std::string& getMethod(void) const { return method; }
	const std::string& getInfo(void) const { return info; }
	const std::string& getLLVMName(void) const { return llvm_name; }

	void print(std::ostream& os) const;
protected:
	JnJavaName(
		const std::string& _l,
		const std::string& _p,
		const std::string& _m,
		const std::string& _i)
	: llvm_name(_l), path(_p), method(_m), info(_i) {}
private:
	std::string	llvm_name;
	std::string	path;
	std::string	method;
	std::string	info;
};

#endif
