/* See COPYING file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#ifdef WINVER
#define mkdir(D, M) _mkdir(D)
#endif
#include "util.h"

#define usage "getgbook " VERSION " - a google books downloader\n" \
              "usage: getgbook [-c|-n] bookid\n" \
              "  -c download pages from codes in stdin\n" \
              "  -n download pages from numbers in stdin\n" \
              "  otherwise, all available pages will be downloaded\n"

#define URLMAX 1024
#define STRMAX 1024
#define MAXPAGES 9999
#define COOKIENUM 5

typedef struct {
	int num;
	char url[URLMAX];
	char name[STRMAX];
	char *cookie;
} Page;

Page **pages;
int numpages;
char cookies[COOKIENUM][COOKIEMAX];
char bookid[STRMAX];
char *bookdir;

int getpagelist()
{
	char url[URLMAX], m[STRMAX];
	char *buf = NULL;
	char *s;
	int i;
	Page *p;

	snprintf(url, URLMAX, "/books?id=%s&printsec=frontcover&redir_esc=y", bookid);

	if(!get("books.google.com", url, NULL, NULL, &buf, 1))
		return 1;

	if((s = strstr(buf, "_OC_Run({\"page\":[")) == NULL)
		return 1;
	s+=strlen("_OC_Run({\"page\":[");

	for(i=0, p=pages[0];*s && i<MAXPAGES; s++) {
		if(*s == ']')
			break;
		if(!strncmp(s, "\"pid\"", 5)) {
			p=pages[i++]=malloc(sizeof(**pages));;
			p->url[0] = '\0';
			snprintf(m, STRMAX, "\"%%%d[^\"]\"", STRMAX-1);
			sscanf(s+6, m, p->name);
			for(;*s; s++) {
				if(*s == '}')
					break;
				if(!strncmp(s, "\"order\"", 7))
					sscanf(s+8, "%d,", &(p->num));
			}
		}
	}

	numpages = i;

	free(buf);
	return 0;
}

int getpageurls(char *pagecode, char *cookie)
{
	char url[URLMAX], code[STRMAX], m[STRMAX];
	char *c = NULL, *buf = NULL;
	char *d, *p, *q;
	int i, j;

	snprintf(url, URLMAX, "/books?id=%s&pg=%s&jscmd=click3&q=subject:a&redir_esc=y", bookid, pagecode);

	if(!get("books.google.com", url, cookie, NULL, &buf, 1))
		return 1;

	c = buf;
	while(*c && (c = strstr(c, "\"pid\":"))) {
		snprintf(m, STRMAX, "\"pid\":\"%%%d[^\"]\"", STRMAX-1);
		if(!sscanf(c, m, code))
			break;
		for(; *c; c++) {
			if(*c == '}') {
				break;
			}
			j = -1;
			if(!strncmp(c, "\"src\"", 5)) {
				for(i=0; i<numpages; i++) {
					if(!strncmp(pages[i]->name, code, STRMAX)) {
						j = i;
						break;
					}
				}
				if(j == -1)     /* TODO: it would be good to add new page on the end */
					break;  /*       of structure rather than throw it away. */
				for(p=pages[j]->url, q=(pages[j]->url-(STRMAX-13-1)), d=c+strlen("\"src\":")+1;
				    *d && *d != '"' && p != q;
				    d++, p++) {
					if(!strncmp(d, "\\u0026", 6)) {
						*p = '&';
						d+=5;
					} else
						*p = *d;
				}
				/* w=2500 gets the best available quality pages  */
				/* q=subject:a is needed for robots.txt compliance */
				strncpy(p, "&w=2500&q=subject:a", 20);
				pages[j]->cookie = cookie;
				break;
			}
		}
	}

	free(buf);
	return 0;
}

int getpage(Page *page)
{
	char path[STRMAX];
	snprintf(path, STRMAX, "%s/%04d.png", bookdir, page->num);

	if(page->url[0] == '\0') {
		fprintf(stderr, "%s not found\n", page->name);
		return 1;
	}

	if(gettofile("books.google.com", page->url, page->cookie, NULL, path, 0)) {
		fprintf(stderr, "%s failed\n", page->name);
		return 1;
	}
	renameifjpg(path);

	printf("%d downloaded\n", page->num);
	fflush(stdout);
	return 0;
}

int searchpage(Page *page)
{
	int i, j;

	if(page->url[0] != '\0')
		return 0;

	for(i=0; i<COOKIENUM; i++) {
		if(cookies[i][0] == '\0') /* dead cookie */
			continue;
		getpageurls(page->name, cookies[i]);
		if(page->url[0] != '\0') {
			/* invalidate old cookies if one succeeded */
			for(j=0; j<i; j++)
				cookies[j][0] = '\0';
			return 0;
		}
	}

	return 1;
}

int main(int argc, char *argv[])
{
	char *tmp;
	char buf[BUFSIZ], pgpath[STRMAX], pgpath2[STRMAX];
	char in[16];
	int a, i, n;
	FILE *f;
	DIR *d;

	if(argc < 2 || argc > 3 || (argc == 3 && (argv[1][0]!='-'
	   || (argv[1][1] != 'c' && argv[1][1] != 'n')))
	   || (argc >= 2 && argv[1][0] == '-' && argv[1][1] == 'h')) {
		fputs(usage, stdout);
		return 1;
	}

	/* get cookies */
	for(i=0;i<COOKIENUM;i++) {
		if(get("books.google.com", "/", NULL, cookies[i], &tmp, 0))
			free(tmp);
	}

	strncpy(bookid, argv[argc-1], STRMAX-1);
	bookid[STRMAX-1] = '\0';
	bookdir = argv[argc-1];

	pages = malloc(sizeof(*pages) * MAXPAGES);
	if(getpagelist()) {
		fprintf(stderr, "Could not find any pages for %s\n", bookid);
		return 1;
	}

	if(!((d = opendir(bookdir)) || !mkdir(bookdir, S_IRWXU))) {
		fprintf(stderr, "Could not create directory %s\n", bookdir);
		return 1;
	}
	if(d) closedir(d);

	if(argc == 2) {
		for(i=0; i<numpages; i++) {
			snprintf(pgpath, STRMAX, "%s/%04d.png", bookdir, pages[i]->num);
			snprintf(pgpath2, STRMAX, "%s/%04d.jpg", bookdir, pages[i]->num);
			if((f = fopen(pgpath, "r")) != NULL || (f = fopen(pgpath2, "r")) != NULL) {
				fclose(f);
				continue;
			}
			searchpage(pages[i]);
			getpage(pages[i]);
			printf("%.0f%% done\n", (float)i / (float)numpages * 100);
		}
	} else if(argv[1][0] == '-') {
		while(fgets(buf, BUFSIZ, stdin)) {
			sscanf(buf, "%15s", in);
			i = -1;
			if(argv[1][1] == 'c') {
				for(a=0; a<numpages; a++) {
					if(strncmp(pages[a]->name, in, STRMAX) == 0) {
						i = a;
						break;
					}
				}
			} else if(argv[1][1] == 'n') {
				sscanf(in, "%d", &n);
				for(a=0; a<numpages; a++) {
					if(pages[a]->num == n) {
						i = a;
						break;
					}
				}
			}
			if(i == -1) {
				fprintf(stderr, "%s not found\n", in);
				continue;
			}
			snprintf(pgpath, STRMAX, "%s/%04d.png", bookdir, pages[i]->num);
			snprintf(pgpath2, STRMAX, "%s/%04d.jpg", bookdir, pages[i]->num);
			if((f = fopen(pgpath, "r")) != NULL || (f = fopen(pgpath2, "r")) != NULL) {
				fclose(f);
				continue;
			}
			searchpage(pages[i]);
			getpage(pages[i]);
		}
	}

	for(i=0; i<numpages; i++) free(pages[i]);
	free(pages);

	return EXIT_SUCCESS;
}
