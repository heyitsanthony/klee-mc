#ifndef LIMITEDOFSTREAM_H
#define LIMITEDOFSTREAM_H

#include <fstream>
#include <sstream>
#include <string>
#include <string.h>

/* C++ streams are fucking awful */
class limited_strbuf : public std::stringbuf
{
public:
	limited_strbuf(unsigned _max_bytes)
	: max_bytes(_max_bytes), total_bytes(0) {}
	bool overCapacity(void) const { return max_bytes < total_bytes; }

protected:
	std::streamsize xsputn ( const char * s, std::streamsize n )
	{
		total_bytes += n;
		if (overCapacity())
			return -1;
		return std::stringbuf::xsputn(s, n);
	}

	int overflow (int c = EOF )
	{
		total_bytes++;
		if (overCapacity()) return -1;
		return std::stringbuf::overflow(c);
	}

private:
	unsigned		max_bytes;
	unsigned		total_bytes;
};

class limited_sstream : public std::ostream
{
public:
	limited_sstream(unsigned _max_bytes)
	: limitbuf(_max_bytes)
	{ oldbuf = rdbuf(&limitbuf); }

	/* reset buffer */
	virtual ~limited_sstream(void) { rdbuf(oldbuf); }
	bool overCapacity(void) const { return limitbuf.overCapacity(); }
	std::string str(void) const
	{
		if (!overCapacity()) return limitbuf.str();
		return "";
	}
private:
	std::streambuf	*oldbuf;
	limited_strbuf	limitbuf;
};

class limited_ofstream : public limited_sstream
{
public:
	limited_ofstream(const char* _fname, unsigned _max_bytes)
	: limited_sstream(_max_bytes), fname(_fname) {}
	virtual ~limited_ofstream(void)
	{
		if (overCapacity()) return;

		std::ofstream	of(fname.c_str());
		of << str();
	}
private:
	std::string fname;

};

#endif