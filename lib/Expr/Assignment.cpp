#include <stdio.h>
#include "static/Sugar.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/Assignment.h"

using namespace klee;

Assignment::Assignment(
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values,
	bool _allowFreeValues)
: allowFreeValues(_allowFreeValues)
{
	std::vector< std::vector<unsigned char> >::iterator valIt;

	valIt = values.begin();
	for (std::vector<const Array*>::const_iterator
		it = objects.begin(),
		ie = objects.end(); it != ie; ++it)
	{
		addBinding(*it, *valIt);
		++valIt;
	}
}

Assignment::Assignment(const ref<Expr>& e, bool _allowFreeValues)
: allowFreeValues(_allowFreeValues)
{
	std::vector<const Array*> objects;

	findSymbolicObjects(e, objects);
	foreach (it, objects.begin(), objects.end())
		free_bindings.insert(*it);
}

Assignment::Assignment(
	const std::vector<const Array*>& objects,
	bool _allowFreeValues)
: allowFreeValues(_allowFreeValues)
{
	foreach (it, objects.begin(), objects.end())
		free_bindings.insert(*it);
}

void Assignment::bindFree(const Array* a, const std::vector<unsigned char>& v)
{
	if (!free_bindings.erase(a))
		return;

	addBinding(a, v);
}

void Assignment::bindFreeToZero(void)
{
	foreach (it, free_bindings.begin(), free_bindings.end()) {
		const Array	*arr = *it;
		addBinding(
			arr,
			std::vector<unsigned char>(arr->mallocKey.size, 0));
	}

	free_bindings.clear();
}

void Assignment::save(const char* path) const
{
	FILE	*f;

	f = fopen(path, "w");
	if (f == NULL) {
		fprintf(stderr, "Failed to save assignment to %s\n", path);
		return;
	}

	foreach (it, bindings.begin(), bindings.end()) {
		size_t		sz;
		const Array	*a = it->first;
		const std::vector<unsigned char>& v(it->second);

		fprintf(f, "%s %d\n", a->name.c_str(), (int)v.size());
		sz = fwrite(v.data(), v.size(), 1, f);
		assert (sz == 1);
	}

	/* TODO: GZIP? */
	fclose(f);
}

std::vector<const Array*> Assignment::getObjectVector(void) const
{
	std::vector<const Array*>	ret;

	foreach (it, bindings.begin(), bindings.end())
		ret.push_back(it->first);

	foreach (it, free_bindings.begin(), free_bindings.end())
		ret.push_back(*it);

	return ret;
}


bool Assignment::load(
	const std::vector<const Array*>& objects,
	const char* path)
{
	std::map<const std::string, const Array*>	name2arr;
	std::vector<unsigned char>	tmp_v;
	FILE				*f;
	char				hdr_buf[256];

	/* no bindings to load? */
	if (objects.size() == 0)
		return true;

	foreach (it, objects.begin(), objects.end())
		name2arr[(*it)->name] = *it;

	f = fopen(path, "r");
	if (f == NULL)
		return false;

	while (fgets(hdr_buf, 256, f) != NULL) {
		const Array	*cur_arr;
		char		arr_name[256];
		unsigned int	arr_len;
		int		hdr_len;
		size_t		items;

		hdr_len = strlen(hdr_buf);
		assert (hdr_len > 0);
		assert (hdr_buf[hdr_len-1] == '\n');

		items = sscanf(hdr_buf, "%s %u", arr_name, &arr_len);
		assert (items == 2);

		cur_arr = name2arr[arr_name];
		if (cur_arr == NULL || cur_arr->mallocKey.size != arr_len) {
			/* name not present in object list */
			fclose(f);
			return false;
		}
		assert (arr_len < 16*1024*1024 &&
			"Comical array in assignment");
		tmp_v.resize(arr_len);

		items = fread(tmp_v.data(), 1, arr_len, f);
		assert (items == arr_len);

		addBinding(cur_arr, tmp_v);
	}

	fclose(f);
	return true;
}

void Assignment::resetBindings(void)
{
	foreach (it, bindings.begin(), bindings.end())
		free_bindings.insert((*it).first);
	bindings.clear();
}
