#ifndef LIMITEDOFSTREAM_H
#define LIMTIEDOFSTREAM_H

#include <fstream>
#include <sstream>
#include <string>
#include <string.h>

#define IPUT_TYPE(x)			\
virtual std::ostream& operator<< (x v)	\
{ if (overflow()) return *this; 	\
  return std::stringstream::operator<<(v); }
 
class limited_sstream : public std::stringstream
{
public:
	limited_sstream(unsigned _max_bytes)
	: max_bytes(_max_bytes)
	, total_bytes(0) {}

	virtual ~limited_sstream(void) {}
	bool overflow(void) const { return total_bytes > max_bytes; }

	IPUT_TYPE(short);
	IPUT_TYPE(unsigned short);
	IPUT_TYPE(int);
	IPUT_TYPE(unsigned int);
	IPUT_TYPE(long);
	IPUT_TYPE(unsigned long);
	IPUT_TYPE(float);
	IPUT_TYPE(double) 
	IPUT_TYPE(long double);
	IPUT_TYPE(const void*);
	IPUT_TYPE(char)
	IPUT_TYPE(signed char)
	IPUT_TYPE(unsigned char)

	virtual std::ostream& operator<< (const char* s)
	{ return write(s, strlen(s)); }
	virtual std::ostream& operator<< (const std::string& s)
	{ return write(s.c_str(), s.size()); }

	virtual std::ostream& write ( const char* s , std::streamsize n ) 
	{
		if (overflow())
			return *this;

		total_bytes += n;
		return std::stringstream::write(s, n);
	}

private:
	unsigned		max_bytes;
	unsigned		total_bytes;
};

class limited_ofstream : public limited_sstream
{
public:
	limited_ofstream(const char* _fname, unsigned _max_bytes)
	: limited_sstream(_max_bytes)
	, fname(_fname) {}
	virtual ~limited_ofstream(void)
	{
		if (overflow()) return;

		std::ofstream	of(fname.c_str());
		of << str();
	}
private:
	std::string fname;
};

#endif