#ifndef JNJAVANAME_H
#define JNJAVANAME_H

#include <string>
#include <iostream>

class JnJavaName
{
public:
	static JnJavaName* create(const std::string& s);
	/* name from global decl */
	static JnJavaName* createGlobal(const std::string& s);
	virtual ~JnJavaName() {}

	const std::string& getPath(void) const { return path; }
	const std::string& getBCPath(void) const { return bcpath; }
	const std::string& getMethod(void) const { return method; }
	const std::string& getInfo(void) const { return info; }
	const std::string& getLLVMName(void) const { return llvm_name; }
	const std::string& getClass(void) const { return class_name; }
	const std::string& getDir(void) const { return dirpath; }

	void print(std::ostream& os) const;

	bool isAnonymous(void) const { return anonymous; }
protected:
	JnJavaName(
		const std::string& _l,
		const std::string& _p,
		const std::string& _m,
		const std::string& _i);
private:
	std::string	llvm_name;
	std::string	path;
	std::string	bcpath;
	std::string	method;
	std::string	info;
	std::string	dirpath;
	std::string	class_name;
	bool		anonymous;
};

#endif
