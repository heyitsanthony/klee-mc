//PRUNECOUNT 0


void *
memset (void *str, int c, int len)
{
  register char *st = str;

  while (len-- > 0)
    *st++ = c;
  return str;
}


int main(char** argv, int argc) {	
	char a[5];
	
	memset(a, 0, 5);

	return 0;
}
